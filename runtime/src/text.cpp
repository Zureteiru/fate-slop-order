#include "vnr/rt/text.h"

#include <algorithm>
#include <cmath>

#include "stb_truetype.h"
#include "vnr/script_lines.h"

namespace vnr::rt {

namespace {
constexpr int kAtlasSize = 2048;

bool is_wide(char32_t cp) {  // CJK and full-width: may break between any two characters
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x3000 && cp <= 0x30FF);
}
}  // namespace

struct GlyphAtlas::Impl {
    stbtt_fontinfo info{};
    std::vector<std::uint8_t> font;
    float scale = 1;
};

GlyphAtlas::GlyphAtlas(TextureStore& textures, const std::vector<std::uint8_t>& font, int font_index, float raster_px)
    : impl_(std::make_unique<Impl>()), textures_(textures), raster_px_(raster_px) {
    impl_->font = font;
    if (!impl_->font.empty()) {
        const int offset = stbtt_GetFontOffsetForIndex(impl_->font.data(), font_index);
        if (offset >= 0 && stbtt_InitFont(&impl_->info, impl_->font.data(), offset)) {
            impl_->scale = stbtt_ScaleForMappingEmToPixels(&impl_->info, raster_px_);
            int asc = 0, desc = 0, gap = 0;
            stbtt_GetFontVMetrics(&impl_->info, &asc, &desc, &gap);
            ascent_ = static_cast<float>(asc) * impl_->scale;
            ok_ = true;
        }
    }
    Image img;
    img.width = img.height = kAtlasSize;
    img.rgba.assign(static_cast<std::size_t>(kAtlasSize) * kAtlasSize * 4, 0);
    texture_ = textures_.create(std::move(img), /*keep_pixels=*/true);
}

GlyphAtlas::~GlyphAtlas() = default;

const GlyphAtlas::Glyph& GlyphAtlas::glyph(char32_t cp) {
    const auto it = glyphs_.find(cp);
    if (it != glyphs_.end()) return it->second;
    Glyph g;
    if (!ok_) return glyphs_[cp] = g;
    const int index = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp));
    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&impl_->info, index, &adv, &lsb);
    g.advance = static_cast<float>(adv) * impl_->scale;
    if (index == 0 && cp != ' ') g.advance = raster_px_ * (is_wide(cp) ? 1.f : 0.5f);  // missing glyph: keep spacing
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&impl_->info, index, impl_->scale, impl_->scale, &x0, &y0, &x1, &y1);
    const int w = x1 - x0, h = y1 - y0;
    if (index != 0 && w > 0 && h > 0) {
        if (pen_x_ + w + 1 >= kAtlasSize) {
            pen_x_ = 1;
            pen_y_ += row_h_ + 1;
            row_h_ = 0;
        }
        Image* img = textures_.pixels(texture_);
        if (img && pen_y_ + h + 1 < kAtlasSize) {
            std::vector<unsigned char> bmp(static_cast<std::size_t>(w) * h);
            stbtt_MakeGlyphBitmap(&impl_->info, bmp.data(), w, h, w, impl_->scale, impl_->scale, index);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const std::size_t o = (static_cast<std::size_t>(pen_y_ + y) * kAtlasSize + pen_x_ + x) * 4;
                    img->rgba[o] = img->rgba[o + 1] = img->rgba[o + 2] = 255;
                    img->rgba[o + 3] = bmp[static_cast<std::size_t>(y) * w + x];
                }
            g.src = {static_cast<float>(pen_x_), static_cast<float>(pen_y_), static_cast<float>(w), static_cast<float>(h)};
            g.x0 = static_cast<float>(x0);
            g.y0 = static_cast<float>(y0);
            g.empty = false;
            pen_x_ += w + 1;
            row_h_ = std::max(row_h_, h);
            dirty_ = true;
        }
    }
    return glyphs_[cp] = g;
}

void GlyphAtlas::flush() {
    if (dirty_) {
        textures_.update(texture_);
        dirty_ = false;
    }
}

namespace {

struct Item {          // one drawable unit of a line
    char32_t cp = 0;
    float px = 20;     // font size
    Color color;
    bool counts = true;          // part of the typewriter count
    std::size_t glyph_index = 0; // index in the typewriter order
    std::string ruby;            // set on the first char of a ruby base
    std::size_t ruby_len = 0;    // number of base chars the ruby spans
    bool image = false;
};

using Line = std::vector<Item>;

Color span_color(const TextStyle& s, Color ink) {
    if (!s.color) return ink;
    return {s.color->r / 255.f, s.color->g / 255.f, s.color->b / 255.f, ink.a};
}

float item_width(GlyphAtlas& a, const Item& it) {
    if (it.image) return it.px;
    return a.glyph(it.cp).advance * (it.px / a.raster_px());
}

std::vector<Line> break_lines(GlyphAtlas& atlas, const RichText& text, const TextStyleBox& st) {
    std::vector<Line> lines(1);
    std::size_t gi = 0;
    for (const Span& sp : text.spans) {
        if (sp.kind == Span::Kind::Break) {
            lines.emplace_back();
            continue;
        }
        const float px = st.px * sp.style.scale;
        const Color col = span_color(sp.style, st.ink);
        if (sp.kind == Span::Kind::Image) {
            Item it;
            it.image = true;
            it.px = px;
            it.color = col;
            it.glyph_index = gi++;
            lines.back().push_back(it);
            continue;
        }
        bool first = true;
        const std::size_t base_len = utf8::count(sp.text);
        for (std::size_t i = 0; i < sp.text.size();) {
            Item it;
            it.cp = utf8::next(sp.text, i);
            it.px = px;
            it.color = col;
            it.glyph_index = gi++;
            if (sp.kind == Span::Kind::Ruby && first) {
                it.ruby = sp.ruby;
                it.ruby_len = base_len;
            }
            first = false;
            lines.back().push_back(it);
        }
    }
    // Wrap long lines: prefer breaking after a space; CJK may break anywhere.
    std::vector<Line> wrapped;
    for (Line& line : lines) {
        Line cur;
        float w = 0;
        for (const Item& it : line) {
            const float iw = item_width(atlas, it);
            if (w + iw > st.box.w && !cur.empty()) {
                std::size_t cut = cur.size();
                if (!is_wide(it.cp) && it.cp != ' ') {
                    for (std::size_t k = cur.size(); k-- > 0;) {
                        if (cur[k].cp == ' ') { cut = k + 1; break; }
                        if (is_wide(cur[k].cp)) { cut = k + 1; break; }
                    }
                }
                Line rest(cur.begin() + static_cast<std::ptrdiff_t>(cut), cur.end());
                cur.resize(cut);
                while (!cur.empty() && cur.back().cp == ' ') cur.pop_back();
                wrapped.push_back(std::move(cur));
                cur = std::move(rest);
                w = 0;
                for (const Item& r : cur) w += item_width(atlas, r);
                if (it.cp == ' ' && cur.empty()) continue;  // no leading space on a new line
            }
            cur.push_back(it);
            w += iw;
        }
        wrapped.push_back(std::move(cur));
    }
    return wrapped;
}

}  // namespace

TextLayoutResult draw_text(GlyphAtlas& atlas, const RichText& text, std::size_t shown, const TextStyleBox& st,
                           DrawList& out) {
    TextLayoutResult res;
    if (!atlas.ok()) return res;
    const std::vector<Line> lines = break_lines(atlas, text, st);
    res.total_lines = static_cast<int>(lines.size());
    // Scroll so the line holding the last revealed glyph is the bottom visible line.
    int last_line = 0;
    for (int l = 0; l < res.total_lines; ++l)
        for (const Item& it : lines[static_cast<std::size_t>(l)])
            if (it.glyph_index < shown) last_line = l;
    res.first_visible_line = std::max(0, last_line - st.max_lines + 1);

    const float k_raster = 1.f / atlas.raster_px();
    for (int l = res.first_visible_line; l < res.total_lines && l < res.first_visible_line + st.max_lines; ++l) {
        const Line& line = lines[static_cast<std::size_t>(l)];
        const float baseline = st.box.y + static_cast<float>(l - res.first_visible_line) * st.line_height + st.px;
        float x = st.box.x;
        for (std::size_t i = 0; i < line.size(); ++i) {
            const Item& it = line[i];
            const float scale = it.px * k_raster;
            const float adv = item_width(atlas, it);
            if (it.glyph_index < shown && !it.image) {
                const GlyphAtlas::Glyph& g = atlas.glyph(it.cp);
                if (!g.empty)
                    out.image(atlas.texture(), g.src, {x + g.x0 * scale, baseline + g.y0 * scale, g.src.w * scale, g.src.h * scale},
                              it.color);
                if (!it.ruby.empty()) {  // reading centred over the base, 0.38 of the size
                    float base_w = 0;
                    for (std::size_t k = i; k < line.size() && k < i + it.ruby_len; ++k) base_w += item_width(atlas, line[k]);
                    const float rpx = it.px * 0.38f;
                    const float rscale = rpx * k_raster;
                    float rw = 0;
                    for (std::size_t j = 0; j < it.ruby.size();) rw += atlas.glyph(utf8::next(it.ruby, j)).advance * rscale;
                    float rx = x + (base_w - rw) / 2;
                    const float rbase = baseline - it.px * 0.95f;
                    for (std::size_t j = 0; j < it.ruby.size();) {
                        const GlyphAtlas::Glyph& rg = atlas.glyph(utf8::next(it.ruby, j));
                        if (!rg.empty)
                            out.image(atlas.texture(), rg.src,
                                      {rx + rg.x0 * rscale, rbase + rg.y0 * rscale, rg.src.w * rscale, rg.src.h * rscale},
                                      {0.95f, 0.95f, 0.95f, it.color.a});
                        rx += rg.advance * rscale;
                    }
                }
            }
            x += adv;
        }
    }
    atlas.flush();
    return res;
}

float text_width(GlyphAtlas& atlas, const RichText& text, float px) {
    float w = 0;
    for (const Span& sp : text.spans) {
        if (sp.kind == Span::Kind::Break) break;
        for (std::size_t i = 0; i < sp.text.size();) w += atlas.glyph(utf8::next(sp.text, i)).advance * (px * sp.style.scale / atlas.raster_px());
    }
    return w;
}

}  // namespace vnr::rt
