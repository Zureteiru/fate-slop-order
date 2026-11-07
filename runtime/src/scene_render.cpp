#include "vnr/rt/scene_render.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "vnr/rt/special_mask.h"

namespace vnr::rt {

namespace {

constexpr float kW = static_cast<float>(kStageWidth);
constexpr float kH = static_cast<float>(kStageHeight);

// Camera + stage shake applied to everything on the stage layer (not UI).
struct StageTransform {
    float scale = 1;
    Vec2 shift;
    Vec2 apply(Vec2 p) const {
        return {kW / 2 + (p.x - kW / 2) * scale + shift.x, kH / 2 + (p.y - kH / 2) * scale + shift.y};
    }
};

void push_quad(DrawList& out, TextureId tex, const Rect& src, const Rect& dst, Color c, const StageTransform& t) {
    Quad q;
    q.texture = tex;
    q.src = src;
    q.pos[0] = t.apply({dst.x, dst.y});
    q.pos[1] = t.apply({dst.x + dst.w, dst.y});
    q.pos[2] = t.apply({dst.x + dst.w, dst.y + dst.h});
    q.pos[3] = t.apply({dst.x, dst.y + dst.h});
    q.color = c;
    out.quads.push_back(q);
}

void draw_background(const std::string& id, float alpha, const SceneAssets& a, const StageTransform& t, DrawList& out) {
    if (id.empty() || alpha <= 0) return;
    const TextureAsset* tex = a.cache->texture(background_url(id, a.region));
    if (!tex || tex->width <= 0 || tex->height <= 0) return;
    // Cover the stage, centred (1344x626 art maps 1:1; older 1024-wide art is scaled up).
    const float s = std::max(kW / static_cast<float>(tex->width), kH / static_cast<float>(tex->height));
    const float w = static_cast<float>(tex->width) * s, h = static_cast<float>(tex->height) * s;
    push_quad(out, tex->id, {0, 0, static_cast<float>(tex->width), static_cast<float>(tex->height)},
              {(kW - w) / 2, (kH - h) / 2, w, h}, {1, 1, 1, alpha}, t);
}

Color element_tint(const Element& e, float opacity) {
    Color c{1, 1, 1, opacity};
    if (e.dimmed) c.r = c.g = c.b = 0.55f;
    const std::string& m = e.filter.mode;
    if (m.find("silhouette") != std::string::npos) {
        c.r = c.g = c.b = 0;
        if (e.filter.color) {
            c.r = e.filter.color->r / 255.f;
            c.g = e.filter.color->g / 255.f;
            c.b = e.filter.color->b / 255.f;
        }
    } else if (m == "dark") {
        c.r *= 0.62f; c.g *= 0.62f; c.b *= 0.62f;
    }
    return c;
}

// Where a masked layer wants a figure's face (Fate Café): its face point on the window's centre row,
// and on its centre column too for narrow windows. NaN = keep the script's position on that axis.
struct FaceAnchor {
    float x = NAN, y = NAN;
};

void draw_figure(int figure_id, int face, Vec2 offset, float scale, Color tint, const SceneAssets& a,
                 const StageTransform& t, DrawList& out, FaceAnchor anchor = {}) {
    const TextureAsset* tex = a.cache->texture(figure_url(figure_id, a.region));
    if (!tex) return;
    FigureInfo info;
    const auto it = a.figures.find(figure_id);
    if (it != a.figures.end()) info = it->second;
    const Vec2 origin = body_origin(info.meta, info.body_width);
    // Element offsets are y-up; the stage is y-down. Scale grows from the body centre.
    const float bw = static_cast<float>(info.body_width), bh = static_cast<float>(std::min(info.body_height, tex->height));
    Vec2 centre{origin.x + bw / 2 + offset.x, origin.y + bh / 2 - offset.y};
    if (!std::isnan(anchor.y) || !std::isnan(anchor.x)) {
        // the face point in body pixels: 72% down the face, or 384 px down a figure without face data
        const bool has_face = info.meta.face_x > 0 || info.meta.face_y > 0;
        const float fsize = static_cast<float>(info.meta.face_w);
        const float fy = has_face ? static_cast<float>(info.meta.face_y) + fsize * 0.72f : 384.f;
        const float fx = has_face ? static_cast<float>(info.meta.face_x) + fsize / 2 : bw / 2;
        if (!std::isnan(anchor.y)) centre.y = anchor.y - (fy - bh / 2) * scale;
        if (!std::isnan(anchor.x)) centre.x = anchor.x - (fx - bw / 2) * scale;
    }
    auto place = [&](float x, float y, float w, float h) -> Rect {  // body-local -> stage
        return {centre.x + (x - bw / 2) * scale, centre.y + (y - bh / 2) * scale, w * scale, h * scale};
    };
    push_quad(out, tex->id, {0, 0, bw, bh}, place(0, 0, bw, bh), tint, t);
    if (const auto crop = face_crop(info.meta, face, info.body_height)) {
        if (crop->src.y + crop->src.h <= static_cast<float>(tex->height))
            push_quad(out, tex->id, crop->src, place(crop->dst.x, crop->dst.y, crop->src.w, crop->src.h), tint, t);
    }
}

}  // namespace

void add_scanlines(const std::vector<Quad>& quads, DrawList& out, float period, float dim) {
    if (quads.empty() || period <= 0) return;
    float y0 = quad_bounds(quads.front()).y, y1 = y0;
    for (const Quad& q : quads) {
        const Rect b = quad_bounds(q);
        y0 = std::min(y0, b.y);
        y1 = std::max(y1, b.y + b.h);
    }
    const float lit = period * 0.75f;  // 3 lit rows, then 1 dim row, on the 4-unit default
    auto band = [&](float a, float b, float alpha) {
        const std::vector<Vec2> rows{{-1e5f, a}, {1e5f, a}, {1e5f, b}, {-1e5f, b}};
        for (const Quad& q : quads)
            for (Quad& p : clip_quad(q, rows)) {
                p.has_colors = true;
                for (Color& c : p.colors) c.a *= alpha;
                out.quads.push_back(p);
            }
    };
    for (float y = std::floor(y0 / period) * period; y < y1; y += period) {
        band(y, y + lit, 1);
        band(y + lit, y + period, dim);
    }
}

// cameraFilter gray (the only mode the scripts use): swap in the greyscale copies, grey solid colours.
// blur: Fate Café's CSS blur(px) on the stage, approximated per quad: each quad is followed by 8 copies
// offset around a circle, copy k at alpha 1/(k+1), so every layer is the average of its copies over
// what is already drawn beneath it (redrawing the whole stage per copy washes sprites out).
void apply_camera_filter(const Camera& cam, const SceneAssets& a, std::size_t first, DrawList& out, bool sepia) {
    if (sepia && a.cache)
        for (std::size_t i = first; i < out.quads.size(); ++i)
            if (out.quads[i].texture && out.quads[i].texture != a.fx_atlas)
                out.quads[i].texture = a.cache->sepia(out.quads[i].texture);
    if (cam.filter == "gray" && a.cache) {
        for (std::size_t i = first; i < out.quads.size(); ++i) {
            Quad& q = out.quads[i];
            if (q.texture) q.texture = a.cache->gray(q.texture);
            auto grey = [](Color& c) { c.r = c.g = c.b = c.r * 0.2126f + c.g * 0.7152f + c.b * 0.0722f; };
            grey(q.color);
            for (Color& c : q.colors) grey(c);
        }
    }
    const float r = cam.blur.value();
    if (r <= 0.05f || first >= out.quads.size()) return;
    const std::vector<Quad> base(out.quads.begin() + static_cast<std::ptrdiff_t>(first), out.quads.end());
    out.quads.resize(first);
    constexpr int kTaps = 8;
    for (const Quad& src : base) {
        out.quads.push_back(src);
        for (int k = 1; k <= kTaps; ++k) {
            const float ang = 2 * 3.14159265f * static_cast<float>(k) / kTaps, w = 1.f / static_cast<float>(k + 1);
            Quad q = src;
            for (Vec2& p : q.pos) p = {p.x + r * std::cos(ang), p.y + r * std::sin(ang)};
            q.color.a *= w;
            for (Color& c : q.colors) c.a *= w;
            out.quads.push_back(q);
        }
    }
}

void draw_effects(const Stage& st, const SceneAssets& a, bool back, DrawList& out) {
    for (const Effect& fx : st.effects) {
        const bool fx_back = fx.layer == EffectLayer::Back || fx.layer == EffectLayer::CharaBack;
        if (fx_back != back) continue;
        const EffectDef* d = find_effect(fx.name);
        if (!d) continue;
        // stage effects centre on the view; character effects on that character's chest
        Vec2 anchor{kViewSafeLeft + kViewSafeWidth / 2, kViewTop + kViewHeight / 2};
        if (fx.layer == EffectLayer::Chara || fx.layer == EffectLayer::CharaBack) {
            const auto it = st.elements.find(fx.target);
            if (it == st.elements.end() || !it->second.shown) continue;
            const Vec2 p = it->second.pos.value();
            anchor = {kW / 2 + p.x, kH * 0.45f - p.y};
        }
        if (fx.pos) anchor = {anchor.x + fx.pos->x, anchor.y - fx.pos->y};
        draw_effect(*d, fx.elapsed, anchor, fx.flip_h, fx.flip_v, a.fx_atlas, fx_seed(fx.name), out);
    }
}

namespace {

void draw_elements(const Stage& st, const SceneAssets& a, const StageTransform& t, DrawList& out) {
    struct Item {
        const Element* e;
        LayerPlacement lp;
    };
    std::vector<Item> order;
    for (const auto& [code, e] : st.elements) {
        if (e.opacity.value() <= 0.001f) continue;
        const LayerPlacement lp = st.placement(e);  // sub-camera layers: hidden, moved or scaled
        if (lp.visible && lp.opacity > 0.001f) order.push_back({&e, lp});
    }
    std::stable_sort(order.begin(), order.end(), [](const Item& x, const Item& y) {
        if (x.e->overlay != y.e->overlay) return y.e->overlay;
        if (x.lp.order != y.lp.order) return x.lp.order < y.lp.order;
        return x.e->depth < y.e->depth;
    });
    for (const Item& item : order) {
        const Element* e = item.e;
        const LayerPlacement& lp = item.lp;
        const float opacity = e->opacity.value() * lp.opacity;
        // maskEdge layers are one surface (Fate Café): the sprites keep their own positions, the surface
        // is clipped to the mask, scaled about the stage centre and moved by offset x 626/1024 (along y
        // only for full-width masks); faces are anchored to the mask's window.
        const bool surface = lp.scale_position;
        const LayerMask* mask = nullptr;
        if (surface && !lp.mask.empty()) {
            const auto mi = a.masks.find(lp.mask);
            if (mi != a.masks.end()) mask = &mi->second;
        }
        const float scale = surface ? e->scale.value() : e->scale.value() * lp.scale;
        Vec2 offset = e->pos.value();
        if (!surface) {
            offset.x += lp.offset.x;
            offset.y += lp.offset.y;
        }
        const Vec2 shake = e->shake.offset();
        offset.x += shake.x;
        offset.y -= shake.y;
        if (e->attack.active()) {  // lunge out and back
            const float k = std::sin(e->attack.value() * 3.14159265f);
            offset.x += e->attack_offset.x * k;
            offset.y += e->attack_offset.y * k;
        }
        const Color tint = element_tint(*e, opacity);
        // Surfaces draw into a scratch list in surface coordinates first; charaSpecialEffect masks the
        // finished sprite over its body's box (the first quad).
        DrawList local;
        const float roll = e->roll.value(), roll_y = e->roll_y.value();
        const bool turned = roll != 0 || roll_y != 0;
        DrawList& target = surface || e->special || turned ? local : out;
        const StageTransform& tt = surface ? StageTransform{} : t;
        if (e->kind == ElementKind::Chara) {
            FaceAnchor anchor;
            if (mask) {
                anchor.y = mask->band_center;
                if (mask->narrow) anchor.x = mask->band_center_x;
            }
            draw_figure(e->figure_id, e->face, offset, scale, tint, a, tt, target, anchor);
        } else {
            const std::string url = e->kind == ElementKind::Scene ? background_url(e->asset, a.region) : image_url(e->asset, a.region);
            const TextureAsset* tex = a.cache->texture(url);
            if (!tex) continue;
            const float w = static_cast<float>(tex->width) * scale, h = static_cast<float>(tex->height) * scale;
            push_quad(target, tex->id, {0, 0, static_cast<float>(tex->width), static_cast<float>(tex->height)},
                      {(kW - w) / 2 + offset.x, (kH - h) / 2 - offset.y, w, h}, tint, tt);
        }
        auto push = [&](std::vector<Quad>& quads, const Rect& box) {
            if (e->special && !quads.empty()) quads = apply_special_mask(quads, box, *e->special, special_t(*e->special));
            out.quads.insert(out.quads.end(), quads.begin(), quads.end());
        };
        // charaRoll turns the sprite (after its mask, as the CSS transform does) about the top centre of
        // its body box, or that point moved by the script's origin (Fate Café).
        auto turn = [&](std::vector<Quad>& quads, const Rect& body) {
            if (!turned) return;
            const Vec2 pivot{body.x + body.w / 2 + e->roll_origin.x, body.y + e->roll_origin.y};
            const float squeeze = std::cos(roll_y * 3.14159265f / 180.f);  // rotateY without perspective
            for (Quad& q : quads) {
                for (Vec2& p : q.pos) p.x = pivot.x + (p.x - pivot.x) * squeeze;
                rotate(q, pivot, roll);
            }
        };
        if (!surface) {
            if (!local.quads.empty() && (e->special || turned)) {
                const Rect body = quad_bounds(local.quads.front());
                std::vector<Quad> quads = std::move(local.quads);
                if (e->special) quads = apply_special_mask(quads, body, *e->special, special_t(*e->special));
                turn(quads, body);
                out.quads.insert(out.quads.end(), quads.begin(), quads.end());
            }
            continue;
        }
        if (!local.quads.empty()) turn(local.quads, quad_bounds(local.quads.front()));
        const Vec2 move{lp.offset.x * kMaskOffsetScale, (mask && mask->full_width) ? -lp.offset.y * kMaskOffsetScale : 0.f};
        auto to_stage = [&](Vec2 p) {
            return t.apply({kW / 2 + (p.x - kW / 2) * lp.scale + move.x, kH / 2 + (p.y - kH / 2) * lp.scale + move.y});
        };
        std::vector<Quad> staged;
        for (const Quad& q : local.quads) {
            std::vector<Quad> parts;
            if (mask) parts = clip_quad(q, mask->polygon);
            else parts.push_back(q);
            for (Quad& p : parts) {
                for (Vec2& v : p.pos) v = to_stage(v);
                staged.push_back(p);
            }
        }
        Rect box;
        if (e->special && !local.quads.empty()) {
            Quad body = local.quads.front();
            for (Vec2& v : body.pos) v = to_stage(v);
            box = quad_bounds(body);
        }
        push(staged, box);
    }
    if (st.communication) {  // hologram figure: pale blue, left of centre, fine scanlines (gameplay video)
        const float o = st.communication->opacity.value();
        DrawList holo;
        draw_figure(st.communication->figure_id, st.communication->face, {-256, 0}, 1, {0.55f, 0.8f, 1.f, 0.85f * o}, a, t, holo);
        add_scanlines(holo.quads, out);
    }
}

Color to_color(Rgba c, float alpha_mul = 1) {
    return {c.r / 255.f, c.g / 255.f, c.b / 255.f, (c.a / 255.f) * alpha_mul};
}

// Draws `src` into `dst` keeping `left`/`right` source columns unscaled and stretching the middle
// (the name tab's point and fade, the choice frame's cut corners).
void draw_hslice(DrawList& out, TextureId tex, const Rect& src, const Rect& dst, float left, float right, Color c) {
    if (dst.w <= left + right) {
        out.image(tex, src, dst, c);
        return;
    }
    out.image(tex, {src.x, src.y, left, src.h}, {dst.x, dst.y, left, dst.h}, c);
    out.image(tex, {src.x + left, src.y, src.w - left - right, src.h}, {dst.x + left, dst.y, dst.w - left - right, dst.h}, c);
    out.image(tex, {src.x + src.w - right, src.y, right, src.h}, {dst.x + dst.w - right, dst.y, right, dst.h}, c);
}

constexpr Color kInk{0.96f, 0.96f, 0.96f, 1};
constexpr Color kShadow{0, 0, 0, 0.75f};

}  // namespace

namespace {
bool inside(const Rect& r, Vec2 p) { return p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h; }

Rect shifted(const Rect& r, Vec2 d) { return {r.x + d.x, r.y + d.y, r.w, r.h}; }

void outline(DrawList& out, const Rect& r, float t, Color c) {
    out.rect({r.x, r.y, r.w, t}, c);
    out.rect({r.x, r.y + r.h - t, r.w, t}, c);
    out.rect({r.x, r.y + t, t, r.h - 2 * t}, c);
    out.rect({r.x + r.w - t, r.y + t, t, r.h - 2 * t}, c);
}

void draw_save_buttons(const UiLayout& ui, Vec2 ms, GlyphAtlas& atlas, DrawList& out) {
    const Color dim{1, 1, 1, ui.button_opacity};
    for (const auto& [rect, name] : {std::pair{ui.save_button, "SAVE"}, std::pair{ui.load_button, "LOAD"}}) {
        const Rect r = shifted(rect, ms);
        out.rect(r, {0, 0, 0, 0.35f});
        outline(out, r, 1.5f, dim);
        const RichText t = parse_rich_text(name, {});
        const float w = text_width(atlas, t, ui.save_label_px);
        TextStyleBox b;
        b.px = ui.save_label_px;
        b.line_height = ui.save_label_px * 1.4f;
        b.max_lines = 1;
        b.first_line = 0;
        b.ink = dim;
        b.box = {r.x + (r.w - w) / 2, r.y + (r.h - b.line_height) / 2, w + 4, b.line_height};
        draw_text(atlas, t, t.glyph_count, b, out);
    }
}

void draw_story_buttons(const SceneAssets& a, const UiLayout& ui, Vec2 ms, DrawList& out) {
    const UiSkin& skin = a.skin;
    const Color dim{1, 1, 1, ui.button_opacity};
    const Rect log = shifted(ui.log_button, ms);
    const Rect autob = shifted(a.auto_mode ? ui.auto_on : ui.auto_off, ms);
    if (skin.has_talk()) {
        out.image(skin.talk, skin::kLogButton, log, dim);
        if (a.auto_mode) out.image(skin.talk, skin::kAutoOn, autob, {1, 1, 1, 1});
        else out.image(skin.talk, skin::kAutoOff, autob, dim);
        return;
    }
    outline(out, log, 1.5f, dim);  // plain fallback in the same places
    if (a.auto_mode) out.rect(autob, {0.3f, 0.8f, 1, 0.9f});
    outline(out, autob, 1.5f, a.auto_mode ? Color{0.6f, 0.95f, 1, 1} : dim);
}
}  // namespace

StoryButton story_button_at(const UiLayout& ui, Vec2 p, bool text_box_visible) {
    if (!text_box_visible) return StoryButton::None;
    if (inside(ui.log_hit, p)) return StoryButton::Log;
    if (inside(ui.auto_hit, p)) return StoryButton::Auto;
    auto grown = [](const Rect& r) { return Rect{r.x - 4, r.y - 6, r.w + 8, r.h + 12}; };
    if (inside(grown(ui.save_button), p)) return StoryButton::Save;
    if (inside(grown(ui.load_button), p)) return StoryButton::Load;
    return StoryButton::None;
}

std::vector<Rect> choice_rects(const ChoiceOp& choice, const UiLayout& ui, bool text_box_visible) {
    std::vector<Rect> rects;
    const float n = static_cast<float>(choice.options.size());
    const float total = n * ui.choice_height + (n - 1) * ui.choice_gap;
    const float centre_y = text_box_visible ? ui.choices_centre_y : kViewTop + kViewHeight / 2;
    float y = centre_y - total / 2;
    for (std::size_t i = 0; i < choice.options.size(); ++i) {
        rects.push_back({(kW - ui.choice_width) / 2, y, ui.choice_width, ui.choice_height});
        y += ui.choice_height + ui.choice_gap;
    }
    return rects;
}

// pictureFrame / pictureFrameTop: an Image asset whose top 626 rows are a stage-sized overlay for
// the 1024-wide view (cut063_cinema: black bars at y 0-92 and 534-626). Drawn in stage units, its
// edge columns repeated out to wider screens. Fate Café draws both commands alike; without the image
// it shades the top and bottom 18% of the view.
void draw_picture_frame(const Stage& st, const SceneAssets& a, DrawList& out) {
    if (st.picture_frame.empty() || a.picture_frame_opacity <= 0) return;
    const float op = std::min(1.f, a.picture_frame_opacity);
    const TextureAsset* tex = a.cache ? a.cache->texture(image_url(st.picture_frame, a.region)) : nullptr;
    if (tex && tex->width > 0) {
        const float w = static_cast<float>(tex->width), sh = kH * w / kViewSafeWidth;
        const float h = std::min(static_cast<float>(tex->height), sh), dh = kH * h / sh;
        const float l = kViewSafeLeft, r = kViewSafeLeft + kViewSafeWidth;
        const Color c{1, 1, 1, op};
        out.image(tex->id, {0, 0, w, h}, {l, 0, kViewSafeWidth, dh}, c);
        out.image(tex->id, {0, 0, 1, h}, {0, 0, l, dh}, c);
        out.image(tex->id, {w - 1, 0, 1, h}, {r, 0, kW - r, dh}, c);
        return;
    }
    const float band = kViewHeight * 0.18f, dark = 0.62f * op;
    Quad top;
    top.pos[0] = {0, kViewTop};
    top.pos[1] = {kW, kViewTop};
    top.pos[2] = {kW, kViewTop + band};
    top.pos[3] = {0, kViewTop + band};
    top.has_colors = true;
    top.colors[0] = top.colors[1] = {0, 0, 0, dark};
    top.colors[2] = top.colors[3] = {0, 0, 0, 0};
    Quad bottom = top;
    const float b = kViewTop + kViewHeight;
    bottom.pos[0] = {0, b - band};
    bottom.pos[1] = {kW, b - band};
    bottom.pos[2] = {kW, b};
    bottom.pos[3] = {0, b};
    bottom.colors[0] = bottom.colors[1] = {0, 0, 0, 0};
    bottom.colors[2] = bottom.colors[3] = {0, 0, 0, dark};
    out.quads.push_back(top);
    out.quads.push_back(bottom);
}

void render_scene(const Player& player, const SceneAssets& a, GlyphAtlas& atlas, const UiLayout& ui, DrawList& out) {
    const Stage& st = player.stage();
    StageTransform t;
    t.scale = std::max(0.01f, st.camera.scale.value());
    const Vec2 cam = st.camera.offset.value();
    const Vec2 shake = st.shake.offset();
    t.shift = {-cam.x + shake.x, cam.y + shake.y};

    // stage layer
    const std::size_t stage_first = out.quads.size();
    if (!st.prev_background.empty() && st.background_mix.active())
        draw_background(st.prev_background, 1, a, t, out);
    draw_background(st.background, st.background_mix.value(), a, t, out);
    draw_effects(st, a, true, out);
    draw_elements(st, a, t, out);
    draw_effects(st, a, false, out);
    bool sepia = false;
    for (const Effect& fx : st.effects)
        if (const EffectDef* d = find_effect(fx.name)) sepia = sepia || d->program == FxProgram::Backdrop;
    apply_camera_filter(st.camera, a, stage_first, out, sepia);
    draw_picture_frame(st, a, out);
    if (st.title_card) draw_title_card(*st.title_card, a.titles, atlas, out);

    // screen layer (no camera)
    const Rect full{0, 0, kW, kH};
    draw_wipe_layer(a.wipe_filter, out);
    const float fade = st.fade.opacity.value();
    if (fade > 0) out.rect(full, to_color(st.fade.color, fade));
    draw_wipe_layer(a.wipe, out);
    if (st.flash.active) {
        const Rgba fc = st.flash.color();
        if (fc.a > 0) out.rect(full, to_color(fc));
    }

    // text box
    const UiSkin& skin = a.skin;
    const TextView tv = player.text();
    if (tv.visible && tv.say) {
        const Vec2 ms = st.message_shake.offset();
        Rect panel = ui.panel;
        panel.x += ms.x;
        panel.y += ms.y;
        if (skin.has_talk())
            out.image(skin.talk, skin::kTextBox, panel, {1, 1, 1, ui.box_opacity});
        else
            out.rect(panel, {0.03f, 0.09f, 0.25f, ui.box_opacity});
        if (!tv.say->speaker.empty()) {
            const float w = std::max(ui.name_plate.w, text_width(atlas, tv.say->speaker, ui.name_px) + ui.name_text_x + 80);
            const Rect tab{ui.name_plate.x + ms.x, ui.name_plate.y + ms.y, w, ui.name_plate.h};
            if (skin.has_talk())
                draw_hslice(out, skin.talk, skin::kNameTab, tab, skin::kNameTabLeft, skin::kNameTabRight, {1, 1, 1, ui.box_opacity});
            else
                out.rect(tab, {0.07f, 0.2f, 0.45f, ui.box_opacity});
            TextStyleBox nb;
            nb.box = {tab.x + ui.name_text_x, tab.y + ui.name_baseline - ui.name_px, w, tab.h};
            nb.px = ui.name_px;
            nb.line_height = tab.h;
            nb.max_lines = 1;
            nb.ink = kInk;
            nb.shadow = kShadow;
            draw_text(atlas, tv.say->speaker, tv.say->speaker.glyph_count, nb, out);
        }
        TextStyleBox tb;
        tb.box = {ui.text.x + ms.x, ui.text.y + ms.y, ui.text.w, ui.text.h};
        tb.px = ui.text_px;
        tb.line_height = ui.line_height;
        tb.max_lines = ui.visible_lines;
        tb.ink = kInk;
        tb.shadow = kShadow;
        tb.boxes = a.text_boxes;
        draw_text(atlas, tv.say->text, tv.shown, tb, out);
        draw_story_buttons(a, ui, ms, out);
        draw_save_buttons(ui, ms, atlas, out);
        // No spinning "next" marker: the user asked for it to be left out.
    }

    // choices
    if (const ChoiceOp* c = player.pending_choice()) {
        const auto rects = choice_rects(*c, ui, tv.visible);
        for (std::size_t i = 0; i < rects.size(); ++i) {
            const Rect& r = rects[i];
            if (skin.has_talk()) {
                draw_hslice(out, skin.talk, skin::kChoice, r, skin::kChoiceEdge, skin::kChoiceEdge, {1, 1, 1, ui.box_opacity});
            } else {
                out.rect(r, {0.02f, 0.02f, 0.04f, ui.box_opacity});
                out.rect({r.x, r.y, r.w, 2}, {0.57f, 0.75f, 0.85f, 1});
                out.rect({r.x, r.y + r.h - 2, r.w, 2}, {0.57f, 0.75f, 0.85f, 1});
            }
            const RichText& label = c->options[i].text;
            const float w = std::min(text_width(atlas, label, ui.choice_px), r.w - 60);
            TextStyleBox lb;
            const float baseline = r.y + ui.choice_baseline;
            lb.box = {r.x + (r.w - w) / 2, baseline - ui.choice_px, r.w - 60, r.h};
            lb.px = ui.choice_px;
            lb.max_lines = 1;
            lb.ink = kInk;
            lb.shadow = kShadow;
            draw_text(atlas, label, label.glyph_count, lb, out);
        }
    }
}

}  // namespace vnr::rt
