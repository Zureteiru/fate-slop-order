#include "vnr/rt/title_card.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

#include "vnr/rich_text.h"
#include "vnr/rt/layer_mask.h"

namespace vnr::rt {

namespace {

constexpr float kCx = kViewSafeLeft + kViewSafeWidth / 2;
constexpr float kCy = kViewTop + kViewHeight / 2;
constexpr float kLabelPx = 26, kLabelSpacing = 0.18f * kLabelPx, kLabelLine = kLabelPx * 1.2f;
const Color kLabelInk{0xea / 255.f, 0xdb / 255.f, 0xb8 / 255.f, 1};
const Color kHairline{1, 244 / 255.f, 214 / 255.f, 0.85f};

// A horizontal gradient strip: colour stops at fractions of [x, x + w].
void strip(float x, float y, float w, float h, std::initializer_list<std::pair<float, Color>> stops, DrawList& out) {
    const auto* prev = stops.begin();
    for (const auto* s = prev + 1; s != stops.end(); prev = s, ++s) {
        Quad q;
        const float x0 = x + w * prev->first, x1 = x + w * s->first;
        q.pos[0] = {x0, y};
        q.pos[1] = {x1, y};
        q.pos[2] = {x1, y + h};
        q.pos[3] = {x0, y + h};
        q.has_colors = true;
        q.colors[0] = q.colors[3] = prev->second;
        q.colors[1] = q.colors[2] = s->second;
        out.quads.push_back(q);
    }
}

RichText plain(const std::string& s) { return parse_rich_text(s, {}); }

// One centred line; `spacing` spreads the letters (CSS letter-spacing) by drawing them one by one.
void line(GlyphAtlas& atlas, const std::string& text, float px, float spacing, float top, Color ink, DrawList& out) {
    if (text.empty()) return;
    TextStyleBox st;
    st.px = px;
    st.line_height = px * 1.25f;
    st.max_lines = 1;
    st.shadow = {0, 0, 0, 1};  // drop-shadow(1px 1px 0 #000)
    st.shadow_offset = {1, 1};
    st.ink = ink;
    const RichText rt = plain(text);
    if (spacing <= 0) {
        const float w = text_width(atlas, rt, px);
        st.box = {kCx - w / 2, top, w + px, st.line_height};
        draw_text(atlas, rt, rt.glyph_count, st, out);
        return;
    }
    std::vector<std::string> chars;  // UTF-8 code points
    for (std::size_t i = 0; i < text.size();) {
        std::size_t n = 1;
        const auto c = static_cast<unsigned char>(text[i]);
        if (c >= 0xf0) n = 4;
        else if (c >= 0xe0) n = 3;
        else if (c >= 0xc0) n = 2;
        chars.push_back(text.substr(i, n));
        i += n;
    }
    std::vector<float> widths;
    float total = 0;
    for (const auto& ch : chars) {
        widths.push_back(text_width(atlas, plain(ch), px));
        total += widths.back() + spacing;
    }
    float x = kCx - (total - spacing) / 2;
    for (std::size_t i = 0; i < chars.size(); ++i) {
        if (chars[i] != " ") {
            const RichText g = plain(chars[i]);
            st.box = {x, top, widths[i] + px, st.line_height};
            draw_text(atlas, g, g.glyph_count, st, out);
        }
        x += widths[i] + spacing;
    }
}

// Greedy word wrap to `width` (the title's max-width: 92%).
std::vector<std::string> wrap(GlyphAtlas& atlas, const std::string& text, float px, float width) {
    std::vector<std::string> lines;
    std::string cur;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        const std::string word = text.substr(i, j - i);
        const std::string next = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && text_width(atlas, plain(next), px) > width) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = next;
        }
        i = j + 1;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

float smooth(float x) { return css_bezier(0.42f, 0, 0.58f, 1, x); }  // CSS ease-in-out

}  // namespace

float css_bezier(float x1, float y1, float x2, float y2, float x) {
    x = std::clamp(x, 0.f, 1.f);
    auto bz = [](float a, float b, float t) { return 3 * a * (1 - t) * (1 - t) * t + 3 * b * (1 - t) * t * t + t * t * t; };
    float lo = 0, hi = 1, t = x;
    for (int i = 0; i < 40; ++i) {  // bisection: x(t) is monotonic for CSS curves
        t = (lo + hi) / 2;
        (bz(x1, x2, t) < x ? lo : hi) = t;
    }
    return bz(y1, y2, t);
}

float title_card_opacity(float t) {
    if (t <= 0 || t >= 1) return 0;
    if (t < 0.2f) return smooth(t / 0.2f);
    if (t <= 0.78f) return 1;
    return 1 - smooth((t - 0.78f) / 0.22f);
}

float title_card_open(float t) { return t >= 0.28f ? 1.f : css_bezier(0.2f, 0.7f, 0.2f, 1, std::max(0.f, t) / 0.28f); }

void draw_title_card(const TitleCard& card, const TitleTexts& texts, GlyphAtlas& atlas, DrawList& out) {
    const std::string& label = card.chapter ? texts.chapter_label : texts.section_label;
    const std::string& title = card.chapter ? texts.chapter_title : texts.section_title;
    if (label.empty() && title.empty()) return;
    const float t = card.duration > 0 ? static_cast<float>(card.elapsed / card.duration) : 1.f;
    const float op = title_card_opacity(t);
    if (op <= 0) return;
    const float title_px = card.chapter ? 54.f : 46.f, title_line = title_px * 1.25f;
    const float pad_top = card.chapter ? 40.f : 26.f, pad_bottom = card.chapter ? 44.f : 30.f;
    const float label_h = label.empty() ? 0 : kLabelLine + 6;
    const std::vector<std::string> title_lines = wrap(atlas, title, title_px, kViewSafeWidth * 0.92f);
    const float h = pad_top + label_h + title_line * static_cast<float>(title_lines.size()) + pad_bottom;
    const float top = kCy - h / 2;

    DrawList card_list;
    const Color clear{0, 0, 0, 0}, dark{0, 0, 0, 0.62f};
    strip(kViewSafeLeft, top, kViewSafeWidth, h, {{0.f, clear}, {0.22f, dark}, {0.78f, dark}, {1.f, clear}}, card_list);
    const float hw = std::min(880.f, kViewSafeWidth * 0.7f);
    const Color gold_clear{kHairline.r, kHairline.g, kHairline.b, 0};
    for (float y : {top, top + h - 1})
        strip(kCx - hw / 2, y, hw, 1, {{0.f, gold_clear}, {0.3f, kHairline}, {0.7f, kHairline}, {1.f, gold_clear}}, card_list);
    float y = top + pad_top;
    if (!label.empty()) {
        line(atlas, label, kLabelPx, kLabelSpacing, y, kLabelInk, card_list);
        y += label_h;
    }
    for (const std::string& tl : title_lines) {
        const std::size_t first = card_list.quads.size();
        line(atlas, tl, title_px, 0, y, {1, 1, 1, 1}, card_list);
        // white down to 30% of the line, #b3b3b3 by 90% (the black shadow quads keep their colour)
        for (std::size_t i = first; i < card_list.quads.size(); ++i) {
            Quad& q = card_list.quads[i];
            if (q.color.r < 0.5f) continue;
            q.has_colors = true;
            for (int k = 0; k < 4; ++k) {
                const float f = std::clamp(((q.pos[k].y - y) / title_line - 0.3f) / 0.6f, 0.f, 1.f);
                const float v = 1 - f * (1 - 0xb3 / 255.f);
                q.colors[k] = {v, v, v, q.color.a};
            }
        }
        y += title_line;
    }
    // opens from the centre (clip-path inset 50% -> 0); the whole card fades
    const float open = title_card_open(t);
    const float half = kStageWidth / 2.f * open;
    const std::vector<Vec2> window{{kCx - half, -1e5f}, {kCx + half, -1e5f}, {kCx + half, 1e5f}, {kCx - half, 1e5f}};
    for (const Quad& q : card_list.quads) {
        std::vector<Quad> parts;
        if (open < 1) parts = clip_quad(q, window);
        else parts.push_back(q);
        for (Quad& p : parts) {
            p.color.a *= op;
            for (Color& c : p.colors) c.a *= op;
            out.quads.push_back(p);
        }
    }
}

}  // namespace vnr::rt
