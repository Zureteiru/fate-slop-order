// Masks for sub-camera layers (`subCameraFilter #A maskEdge cut359_mask10 …`), after Fate Café: the mask
// image's brightness is the window, stretched to the stage width and 1024 stage units tall from the top.
// The game's masks are convex shapes (diagonal halves, bands), so the window is kept as a convex polygon
// and sprites are clipped to it on the CPU.
#pragma once

#include <vector>

#include "vnr/rt/draw_list.h"
#include "vnr/rt/image.h"

namespace vnr::rt {

inline constexpr float kMaskHeight = 1024;                 // stage units the mask's height covers
inline constexpr float kMaskOffsetScale = 626.f / 1024.f;  // Fate Café moves masked layers by offset x this

struct LayerMask {
    bool ok = false;
    std::vector<Vec2> polygon;    // stage units, clockwise on screen (y down)
    float band_center = 313;      // mean bright row (stage y): faces are anchored here
    float band_center_x = 672;    // mean bright column (stage x)
    bool narrow = false;          // no row is more than 45% bright: faces are centred horizontally too
    bool full_width = false;      // every bright row is (almost) fully bright: the layer also moves along y
};

// Pixels brighter than 20 (luma) count as the window, as in Fate Café.
LayerMask build_layer_mask(const Image& mask);

// Clips a quad (texture coordinates from its src rect, or its own uv) to a convex polygon. Returns
// triangles, each as a quad whose last corner repeats the third, with per-corner texture coordinates.
std::vector<Quad> clip_quad(const Quad& q, const std::vector<Vec2>& convex);

}  // namespace vnr::rt
