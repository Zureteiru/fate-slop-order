#include "vnr/rt/scene_render.h"

#include <algorithm>
#include <cmath>

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

void draw_figure(int figure_id, int face, Vec2 offset, float scale, Color tint, const SceneAssets& a,
                 const StageTransform& t, DrawList& out) {
    const TextureAsset* tex = a.cache->texture(figure_url(figure_id, a.region));
    if (!tex) return;
    FigureInfo info;
    const auto it = a.figures.find(figure_id);
    if (it != a.figures.end()) info = it->second;
    const Vec2 origin = body_origin(info.meta, info.body_width);
    // Element offsets are y-up; the stage is y-down. Scale grows from the body centre.
    const float bw = static_cast<float>(info.body_width), bh = static_cast<float>(std::min(info.body_height, tex->height));
    const Vec2 centre{origin.x + bw / 2 + offset.x, origin.y + bh / 2 - offset.y};
    auto place = [&](float x, float y, float w, float h) -> Rect {  // body-local -> stage
        return {centre.x + (x - bw / 2) * scale, centre.y + (y - bh / 2) * scale, w * scale, h * scale};
    };
    push_quad(out, tex->id, {0, 0, bw, bh}, place(0, 0, bw, bh), tint, t);
    if (const auto crop = face_crop(info.meta, face, info.body_height)) {
        if (crop->src.y + crop->src.h <= static_cast<float>(tex->height))
            push_quad(out, tex->id, crop->src, place(crop->dst.x, crop->dst.y, crop->src.w, crop->src.h), tint, t);
    }
}

void draw_elements(const Stage& st, const SceneAssets& a, const StageTransform& t, DrawList& out) {
    std::vector<const Element*> order;
    for (const auto& [code, e] : st.elements)
        if (e.opacity.value() > 0.001f) order.push_back(&e);
    std::stable_sort(order.begin(), order.end(), [](const Element* x, const Element* y) {
        if (x->overlay != y->overlay) return y->overlay;
        return x->depth < y->depth;
    });
    for (const Element* e : order) {
        const float opacity = e->opacity.value();
        Vec2 offset = e->pos.value();
        const Vec2 shake = e->shake.offset();
        offset.x += shake.x;
        offset.y -= shake.y;
        if (e->attack.active()) {  // lunge out and back
            const float k = std::sin(e->attack.value() * 3.14159265f);
            offset.x += e->attack_offset.x * k;
            offset.y += e->attack_offset.y * k;
        }
        const Color tint = element_tint(*e, opacity);
        if (e->kind == ElementKind::Chara) {
            draw_figure(e->figure_id, e->face, offset, e->scale.value(), tint, a, t, out);
        } else {
            const std::string url = e->kind == ElementKind::Scene ? background_url(e->asset, a.region) : image_url(e->asset, a.region);
            const TextureAsset* tex = a.cache->texture(url);
            if (!tex) continue;
            const float w = static_cast<float>(tex->width) * e->scale.value(), h = static_cast<float>(tex->height) * e->scale.value();
            push_quad(out, tex->id, {0, 0, static_cast<float>(tex->width), static_cast<float>(tex->height)},
                      {(kW - w) / 2 + offset.x, (kH - h) / 2 - offset.y, w, h}, tint, t);
        }
    }
    if (st.communication) {  // hologram figure: tinted, left of centre
        const float o = st.communication->opacity.value();
        draw_figure(st.communication->figure_id, st.communication->face, {-256, 0}, 1, {0.55f, 0.8f, 1.f, 0.85f * o}, a, t, out);
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

void render_scene(const Player& player, const SceneAssets& a, GlyphAtlas& atlas, const UiLayout& ui, DrawList& out) {
    const Stage& st = player.stage();
    StageTransform t;
    t.scale = std::max(0.01f, st.camera.scale.value());
    const Vec2 cam = st.camera.offset.value();
    const Vec2 shake = st.shake.offset();
    t.shift = {-cam.x + shake.x, cam.y + shake.y};

    // stage layer
    if (!st.prev_background.empty() && st.background_mix.active())
        draw_background(st.prev_background, 1, a, t, out);
    draw_background(st.background, st.background_mix.value(), a, t, out);
    draw_elements(st, a, t, out);

    // screen layer (no camera)
    const Rect full{0, 0, kW, kH};
    const float fade = st.fade.opacity.value();
    if (fade > 0) out.rect(full, to_color(st.fade.color, fade));
    const float cover = st.wipe.cover.value();  // wipe masks come later: approximate with an even cover
    if (cover > 0) out.rect(full, to_color(st.wipe.color, cover));
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
        draw_text(atlas, tv.say->text, tv.shown, tb, out);
        if (skin.has_next() && player.status() == Status::AwaitAdvance && !player.pending_choice()) {
            const int frame = static_cast<int>(a.clock * skin::kNextFps) % skin::kNextFrames;
            out.image(skin.next, skin::next_frame(frame),
                      {ui.next_marker.x + ms.x, ui.next_marker.y + ms.y, ui.next_marker.w, ui.next_marker.h});
        }
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
