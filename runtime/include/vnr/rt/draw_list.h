// What to draw each frame, in stage units (1344x626), and the textures it refers to.
// The wrapper's renderer owns GPU textures; the runtime owns pixels and ids.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "vnr/assets.h"
#include "vnr/rt/image.h"

namespace vnr::rt {

using TextureId = std::uint32_t;  // 0 = no texture (solid colour)

// What the game actually shows of the 1344x626 stage: the top 576 rows (the bottom 50 are bleed for
// backgrounds), at least the 1024-wide safe area and up to the full 1344 on wider screens. Measured
// against NA gameplay video: a 16:9 screen shows exactly x 160-1184, y 0-576 (sprites and
// backgrounds line up to the pixel; a centred band would put them 25 too high).
constexpr float kViewTop = 0;
constexpr float kViewHeight = 576;
constexpr float kViewSafeLeft = 160;
constexpr float kViewSafeWidth = 1024;

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};

enum class Blend : std::uint8_t {
    Alpha,  // normal "over" blending
    Add,    // additive (light: glows, sparks, flashes)
};

struct Quad {
    TextureId texture = 0;
    Rect src;         // texture pixels (ignored for solid quads)
    Vec2 pos[4];      // stage units: top-left, top-right, bottom-right, bottom-left
    Color color;      // tint / solid colour, alpha = opacity
    Blend blend = Blend::Alpha;
    // Per-corner colours instead of `color` (gradients: mask bands, title card fades, glow falloff).
    bool has_colors = false;
    Color colors[4];
    // Per-corner texture coordinates (texture pixels) instead of `src`: clipped sprites. A triangle is a
    // quad whose last corner repeats the third.
    bool has_uv = false;
    Vec2 uv[4];
};

struct DrawList {
    std::vector<Quad> quads;
    Color clear{0, 0, 0, 1};

    void clear_all() { quads.clear(); }
    void rect(const Rect& dst, Color c);
    void image(TextureId tex, const Rect& src, const Rect& dst, Color c = {});
};

// Rotates a quad's corners about `origin` (stage units) by `degrees`, clockwise on screen (y down).
void rotate(Quad& q, Vec2 origin, float degrees);
// Colour of corner k (the per-corner colour when set, otherwise the quad colour).
inline Color corner_color(const Quad& q, int k) { return q.has_colors ? q.colors[k] : q.color; }

// Pixel data and ids for every texture the runtime uses. The renderer takes pending uploads each
// frame, creates/updates its GPU textures, then calls uploaded() so CPU pixels can be dropped.
class TextureStore {
public:
    TextureId create(Image image, bool keep_pixels = false);
    void update(TextureId id);  // pixels changed (e.g. glyph atlas): upload again
    Image* pixels(TextureId id);
    int width(TextureId id) const;
    int height(TextureId id) const;
    void release(TextureId id);

    std::vector<TextureId> take_uploads();
    std::vector<TextureId> take_releases();
    void uploaded(TextureId id);  // renderer finished uploading; frees pixels unless kept

private:
    struct Entry {
        Image image;
        int width = 0, height = 0;
        bool keep = false;
    };
    std::unordered_map<TextureId, Entry> entries_;
    std::vector<TextureId> uploads_, releases_;
    TextureId next_ = 1;
};

}  // namespace vnr::rt
