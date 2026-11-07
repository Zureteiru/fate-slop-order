// charaSpecialEffect masks, as Fate Café draws them over the character's box:
//   band (erasure / appearance, top-down or reversed): alpha(y) = clamp((y - (1 - a)) / 0.2),
//     a = incoming ? 0.2 + t : 1.2039 (1 - t)
//   radial (wipe / wipetime): opaque inside max(0, n - 0.2) / (240/255), clear beyond max(1e-4, n) / (240/255),
//     n = 1.2039 (incoming ? t : 1 - t), distances relative to the centre-to-corner length
// with t = easeInSine of the linear progress. Quads are clipped and get per-vertex alpha.
#pragma once

#include <vector>

#include "vnr/rt/draw_list.h"
#include "vnr/stage.h"

namespace vnr::rt {

// easeInSine of the effect's linear progress.
float special_t(const SpecialErase& fx);
// Bounding box of a quad's corners.
Rect quad_bounds(const Quad& q);
// The parts of `quads` the mask leaves, with the mask's alpha in their corner colours.
std::vector<Quad> apply_special_mask(const std::vector<Quad>& quads, const Rect& box, const SpecialErase& fx, float t);

}  // namespace vnr::rt
