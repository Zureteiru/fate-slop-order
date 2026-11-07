#include "vnr/rt/layer_mask.h"

#include <algorithm>
#include <cmath>

namespace vnr::rt {

namespace {

float cross(Vec2 o, Vec2 a, Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

// Andrew's monotone chain; drops collinear points.
std::vector<Vec2> convex_hull(std::vector<Vec2> pts) {
    std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    if (pts.size() < 3) return pts;
    std::vector<Vec2> h(2 * pts.size());
    std::size_t k = 0;
    for (const Vec2& p : pts) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], p) <= 0) --k;
        h[k++] = p;
    }
    for (std::size_t i = pts.size() - 1, t = k + 1; i-- > 0;) {
        while (k >= t && cross(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
        h[k++] = pts[i];
    }
    h.resize(k - 1);
    return h;
}

struct Vert {
    Vec2 p, uv;
    Color c;
};

float mix(float a, float b, float t) { return a + (b - a) * t; }

Vert lerp(const Vert& a, const Vert& b, float t) {
    return {{mix(a.p.x, b.p.x, t), mix(a.p.y, b.p.y, t)},
            {mix(a.uv.x, b.uv.x, t), mix(a.uv.y, b.uv.y, t)},
            {mix(a.c.r, b.c.r, t), mix(a.c.g, b.c.g, t), mix(a.c.b, b.c.b, t), mix(a.c.a, b.c.a, t)}};
}

}  // namespace

LayerMask build_layer_mask(const Image& img) {
    LayerMask m;
    if (img.width <= 0 || img.height <= 0 || img.rgba.size() < static_cast<std::size_t>(img.width) * img.height * 4) return m;
    const float sx = static_cast<float>(kStageWidth) / static_cast<float>(img.width);
    const float sy = kMaskHeight / static_cast<float>(img.height);
    std::vector<Vec2> edge;
    double bright = 0, row_sum = 0, col_sum = 0;
    double min_cover = 1, max_cover = 0;
    for (int y = 0; y < img.height; ++y) {
        int first = -1, last = -1, count = 0;
        long long cols = 0;
        for (int x = 0; x < img.width; ++x) {
            const std::uint8_t* px = &img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4];
            // luma weighted by alpha, so a transparent PNG and a black-and-white one read the same
            const int luma = static_cast<int>((px[0] * 0.2126 + px[1] * 0.7152 + px[2] * 0.0722) * px[3] / 255.0);
            if (luma > 20) {
                if (first < 0) first = x;
                last = x;
                ++count;
                cols += x;
            }
        }
        if (count == 0) continue;
        const double cover = static_cast<double>(count) / img.width;
        min_cover = std::min(min_cover, cover);
        max_cover = std::max(max_cover, cover);
        bright += count;
        row_sum += static_cast<double>(count) * y;
        col_sum += static_cast<double>(cols);
        // pixel edges, so a fully bright row reaches from 0 to the full width
        edge.push_back({first * sx, y * sy});
        edge.push_back({(last + 1) * sx, y * sy});
        edge.push_back({first * sx, (y + 1) * sy});
        edge.push_back({(last + 1) * sx, (y + 1) * sy});
    }
    if (bright <= 0) return m;
    m.polygon = convex_hull(std::move(edge));
    m.band_center = static_cast<float>(row_sum / bright) * sy;
    m.band_center_x = static_cast<float>(col_sum / bright) * sx;
    m.narrow = max_cover <= 0.45;
    m.full_width = min_cover >= 0.99;
    m.ok = m.polygon.size() >= 3;
    return m;
}

std::vector<Quad> clip_quad(const Quad& q, const std::vector<Vec2>& convex) {
    std::vector<Vert> poly(4);
    for (int k = 0; k < 4; ++k) {
        poly[k].p = q.pos[k];
        poly[k].c = corner_color(q, k);
    }
    if (q.has_uv) {
        for (int k = 0; k < 4; ++k) poly[k].uv = q.uv[k];
    } else {
        poly[0].uv = {q.src.x, q.src.y};
        poly[1].uv = {q.src.x + q.src.w, q.src.y};
        poly[2].uv = {q.src.x + q.src.w, q.src.y + q.src.h};
        poly[3].uv = {q.src.x, q.src.y + q.src.h};
    }
    if (convex.size() < 3) return {};
    // Orientation of the clip polygon: inside is where cross() has this sign.
    float area = 0;
    for (std::size_t i = 0; i < convex.size(); ++i) {
        const Vec2 a = convex[i], b = convex[(i + 1) % convex.size()];
        area += a.x * b.y - b.x * a.y;
    }
    const float sign = area >= 0 ? 1.f : -1.f;
    for (std::size_t i = 0; i < convex.size() && !poly.empty(); ++i) {
        const Vec2 a = convex[i], b = convex[(i + 1) % convex.size()];
        auto side = [&](Vec2 p) { return cross(a, b, p) * sign; };
        std::vector<Vert> out;
        for (std::size_t j = 0; j < poly.size(); ++j) {
            const Vert& cur = poly[j];
            const Vert& nxt = poly[(j + 1) % poly.size()];
            const float sc = side(cur.p), sn = side(nxt.p);
            if (sc >= 0) out.push_back(cur);
            if ((sc >= 0) != (sn >= 0)) out.push_back(lerp(cur, nxt, sc / (sc - sn)));
        }
        poly = std::move(out);
    }
    std::vector<Quad> tris;
    for (std::size_t j = 1; j + 1 < poly.size(); ++j) {
        Quad t = q;
        t.has_uv = true;
        t.has_colors = q.has_colors;
        const Vert* v[4] = {&poly[0], &poly[j], &poly[j + 1], &poly[j + 1]};
        for (int k = 0; k < 4; ++k) {
            t.pos[k] = v[k]->p;
            t.uv[k] = v[k]->uv;
            t.colors[k] = v[k]->c;
        }
        tris.push_back(t);
    }
    return tris;
}

}  // namespace vnr::rt
