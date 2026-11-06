// Font rasterising into a glyph atlas, and laying out dialogue text (wrap, ruby, typewriter cut-off).
#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "vnr/rich_text.h"
#include "vnr/rt/draw_list.h"

namespace vnr::rt {

class GlyphAtlas {
public:
    struct Glyph {
        Rect src;              // in the atlas texture (pixels)
        float x0 = 0, y0 = 0;  // offset of the bitmap from the pen position (raster px, y down from baseline)
        float advance = 0;     // raster px
        bool empty = true;     // whitespace / missing glyph: nothing to draw
    };

    // `raster_px` is the size glyphs are rasterised at; drawing scales them to the wanted size.
    GlyphAtlas(TextureStore& textures, const std::vector<std::uint8_t>& font, int font_index, float raster_px = 48);
    ~GlyphAtlas();
    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    bool ok() const { return ok_; }
    static bool usable(const std::vector<std::uint8_t>& font, int font_index);  // TTF/OTF (incl. CFF) stb can open
    const Glyph& glyph(char32_t cp);
    float raster_px() const { return raster_px_; }
    float ascent() const { return ascent_; }  // raster px above the baseline
    TextureId texture() const { return texture_; }
    void flush();  // queue a texture upload if new glyphs were added this frame

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TextureStore& textures_;
    TextureId texture_ = 0;
    float raster_px_;
    float ascent_ = 0;
    bool ok_ = false;
    bool dirty_ = false;
    int pen_x_ = 1, pen_y_ = 1, row_h_ = 0;
    std::unordered_map<char32_t, Glyph> glyphs_;
};

struct TextStyleBox {
    Rect box;                 // stage units
    float px = 20;            // font size for scale 1
    float line_height = 32.4f;
    int max_lines = 2;        // the box shows the last `max_lines` lines that contain revealed text
    Color ink{1, 1, 1, 1};
    Color shadow{0, 0, 0, 0};  // drop shadow under each glyph; alpha 0 = none
    Vec2 shadow_offset{2, 2};
};

struct TextLayoutResult {
    int total_lines = 0;
    int first_visible_line = 0;
};

// Appends glyph quads for `text` (only the first `shown` glyphs) to `out`.
TextLayoutResult draw_text(GlyphAtlas& atlas, const RichText& text, std::size_t shown, const TextStyleBox& style,
                           DrawList& out);

// Width of a single line of plain text in stage units (for name plates and buttons).
float text_width(GlyphAtlas& atlas, const RichText& text, float px);

}  // namespace vnr::rt
