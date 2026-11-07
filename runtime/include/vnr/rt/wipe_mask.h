// wipein / wipeout / wipeFilter masks, the way Fate Café draws them: a grey pattern texture is scaled
// to a 256x256 field v, and each pixel of the cover colour gets alpha
//   r = p * (1 + g);  alpha = g <= 0 ? (v < r) : clamp((r - v) / g)
// where p is the cover (0 = clear, 1 = covered) and g the script's gradation (softness). The field is
// stretched over the 16:9 view. The textures come from tools/install_wipes.sh (user files
// wipes/<name>.png); without one, a built-in field of the same shape stands in, and a pattern with
// neither falls back to an even fade.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "vnr/rt/draw_list.h"
#include "vnr/rt/image.h"
#include "vnr/stage.h"

namespace vnr::rt {

constexpr int kWipeFieldSize = 256;

struct WipeField {
    std::vector<float> v;  // kWipeFieldSize^2 values in 0..1, row-major; empty = no field
    bool ok() const { return v.size() == static_cast<std::size_t>(kWipeFieldSize) * kWipeFieldSize; }
};

// Fate Café's threshold for one pixel.
float wipe_alpha(float v, float p, float g);
// Lower-case lookup key with Fate Café's aliases (rectangleLeftToRight -> rectangleStripLeftToRight).
std::string wipe_key(std::string_view pattern);
// The 33 texture names install_wipes.sh installs, as spelled in the file names.
const std::vector<std::string>& wipe_texture_names();
// The installed file name for a pattern ("wipes/openEye.png"), or "" when no texture exists for it.
std::string wipe_texture_file(std::string_view pattern);
// The red channel of a decoded texture, resampled to the field size.
WipeField wipe_field_from_image(const Image& img);
// A built-in field shaped like the pattern's texture; empty when there is none (even fade).
WipeField procedural_wipe_field(std::string_view pattern);
// Writes the mask for cover p into a kWipeFieldSize^2 RGBA image.
void fill_wipe_mask(const WipeField& f, float p, float g, Rgba color, Image& out);

// What the scene draws for one wipe layer.
struct WipeLayer {
    TextureId texture = 0;  // mask to stretch over the view (0 = none)
    float even = 0;         // no mask: an even cover of this opacity
    bool bars = false;      // cinema bars (wipeFilter cinema without its texture)
    Rgba color{0, 0, 0, 255};
};

// Draws a layer over the stage (screen layer, no camera).
void draw_wipe_layer(const WipeLayer& w, DrawList& out);

// Keeps the fields (installed and built-in) and the two mask textures, and rewrites the masks when
// the wipe or the wipe filter changes.
class WipeMasks {
public:
    void install(std::string_view pattern, WipeField f);
    bool installed(std::string_view pattern) const;
    void update(const Stage& st, TextureStore& textures);

    WipeLayer wipe, filter;

private:
    struct Mask {
        TextureId texture = 0;
        std::string key;
        float p = -1, g = -1;
        Rgba color{};
    };
    const WipeField* field(const std::string& key);
    void layer(const std::string& pattern, float p, float g, Rgba color, Mask& m, WipeLayer& out,
               bool cinema_bars, TextureStore& textures);

    std::map<std::string, WipeField> installed_, builtin_;
    Mask wipe_mask_, filter_mask_;
};

}  // namespace vnr::rt
