#include "vnr/rt/special_mask.h"

#include <algorithm>
#include <cmath>

#include "vnr/rt/layer_mask.h"

namespace vnr::rt {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kReach = 1.2039215564727783f;  // Fate Café's constants
constexpr float kBand = 0.2f;
constexpr float kScale = 240.f / 255.f;
constexpr float kFar = 1e5f;
constexpr int kSides = 48;

// Clips every quad to `poly` and scales each vertex's alpha by alpha(pos).
template <class F>
void clip_into(const std::vector<Quad>& quads, const std::vector<Vec2>& poly, F alpha, std::vector<Quad>& out) {
    for (const Quad& q : quads) {
        for (Quad& p : clip_quad(q, poly)) {
            p.has_colors = true;
            for (int k = 0; k < 4; ++k) p.colors[k].a *= alpha(p.pos[k]);
            out.push_back(p);
        }
    }
}

std::vector<Vec2> rows(float y0, float y1) { return {{-kFar, y0}, {kFar, y0}, {kFar, y1}, {-kFar, y1}}; }

}  // namespace

float special_t(const SpecialErase& fx) {
    const float k = std::clamp(fx.progress.value(), 0.f, 1.f);
    return 1 - std::cos(k * kPi / 2);
}

Rect quad_bounds(const Quad& q) {
    float x0 = q.pos[0].x, x1 = x0, y0 = q.pos[0].y, y1 = y0;
    for (const Vec2& p : q.pos) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

std::vector<Quad> apply_special_mask(const std::vector<Quad>& quads, const Rect& box, const SpecialErase& fx, float t) {
    std::vector<Quad> out;
    if (box.w <= 0 || box.h <= 0) return out;
    if (!fx.radial) {
        const float a = fx.incoming ? kBand + t : kReach * (1 - t);
        const float start = 1 - a;  // band from `start` (clear) to `start + kBand` (opaque), y from the band's origin
        if (start + kBand <= 0) return quads;
        if (start >= 1) return out;
        // band coordinate (0 = where the band starts its run) of a stage point
        auto v = [&](Vec2 p) {
            const float y = (p.y - box.y) / box.h;
            return fx.reverse ? 1 - y : y;
        };
        auto alpha = [&](Vec2 p) { return std::clamp((v(p) - start) / kBand, 0.f, 1.f); };
        const float ya = box.y + start * box.h, yb = box.y + (start + kBand) * box.h;
        if (!fx.reverse) {
            clip_into(quads, rows(ya, yb), alpha, out);
            clip_into(quads, rows(yb, kFar), [](Vec2) { return 1.f; }, out);
        } else {
            const float ra = box.y + box.h - (ya - box.y), rb = box.y + box.h - (yb - box.y);
            clip_into(quads, rows(rb, ra), alpha, out);
            clip_into(quads, rows(-kFar, rb), [](Vec2) { return 1.f; }, out);
        }
        return out;
    }
    const float n = kReach * (fx.incoming ? t : 1 - t);
    const float far = 0.5f * std::sqrt(box.w * box.w + box.h * box.h);
    const float ri = std::max(0.f, n - kBand) / kScale * far, ro = std::max(1e-4f, n) / kScale * far;
    if (ri >= far) return quads;
    const Vec2 c{box.x + box.w / 2, box.y + box.h / 2};
    auto alpha = [&](Vec2 p) {
        const float d = std::hypot(p.x - c.x, p.y - c.y);
        return ro > ri ? std::clamp((ro - d) / (ro - ri), 0.f, 1.f) : (d <= ri ? 1.f : 0.f);
    };
    auto at = [&](float r, int i) {
        const float ang = 2 * kPi * static_cast<float>(i) / kSides;
        return Vec2{c.x + r * std::cos(ang), c.y + r * std::sin(ang)};
    };
    if (ri > 0) {
        std::vector<Vec2> disc;
        for (int i = 0; i < kSides; ++i) disc.push_back(at(ri, i));
        clip_into(quads, disc, [](Vec2) { return 1.f; }, out);
    }
    for (int i = 0; i < kSides; ++i)
        clip_into(quads, {at(ri, i), at(ri, i + 1), at(ro, i + 1), at(ro, i)}, alpha, out);
    return out;
}

}  // namespace vnr::rt
