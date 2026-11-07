#include "vnr/stage.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace vnr {

Vec2 Shake::offset() const {
    if (!active) return {};
    const double period = std::max(0.04, static_cast<double>(interval) * 4);
    const double phase = std::fmod(elapsed, period) / period;  // 0..1
    // Same pattern as the CSS keyframes: +x-y, -x+y, +x-y … settling at 0.
    const int step = static_cast<int>(phase * 5.5);
    const float sx = (step % 2 == 0) ? 1.f : -1.f;
    if (step >= 5) return {};
    return {sx * amp_x, -sx * amp_y};
}

void Shake::tick(double dt) {
    if (!active) return;
    elapsed += dt;
    if (duration > 0 && elapsed >= duration) active = false;
}

Rgba Flash::color() const {
    if (!active) return {0, 0, 0, 0};
    double env = 1;
    Rgba c = from;
    const double t = loop ? std::fmod(elapsed, std::max(duration, 1e-6)) : elapsed;
    if (!loop && t < rise && rise > 0) {
        env = t / rise;
    } else {
        const double u = std::clamp((t - (loop ? 0 : rise)) / std::max(duration, 1e-6), 0.0, 1.0);
        auto lerp = [&](std::uint8_t a, std::uint8_t b) { return static_cast<std::uint8_t>(std::lround(a + (b - a) * u)); };
        c = {lerp(from.r, to.r), lerp(from.g, to.g), lerp(from.b, to.b), lerp(from.a, to.a)};
    }
    if (fading_out) env *= out.value();
    c.a = static_cast<std::uint8_t>(std::lround(c.a * env));
    return c;
}

void Flash::tick(double dt) {
    if (!active) return;
    elapsed += dt;
    if (fading_out) {
        out.tick(dt);
        if (!out.active()) active = false;
    } else if (!loop && elapsed >= rise + duration) {
        active = false;
    }
}

Stage::Stage() {
    camera.scale.snap(1);
    background_mix.snap(1);
}

Element* Stage::find(const std::string& code) {
    const auto it = elements.find(code);
    return it == elements.end() ? nullptr : &it->second;
}

SubLayer::SubLayer() {
    scale.snap(1);
    opacity.snap(1);
}

LayerPlacement Stage::placement(const Element& e) const {
    LayerPlacement p;
    if (e.layer.empty()) return p;
    const auto it = sub_layers.find(e.layer);
    if (it == sub_layers.end() || !it->second.shown) {
        p.visible = false;
        return p;
    }
    const SubLayer& l = it->second;
    p.opacity = l.opacity.value();
    const Vec2 off = l.offset.value();
    const bool edge = l.mask_mode == "maskEdge" || l.mask_mode == "maskedge";
    p.scale = l.scale.value();
    p.scale_position = edge;
    if (edge) p.mask = l.mask;
    // Fate Café: a maskEdge layer moves as a group; otherwise its characters shift by x (by y too only
    // for "mask" layers) and scale with the layer.
    p.offset = {off.x, (edge || l.mask_mode == "mask") ? off.y : 0.f};
    const bool moved = l.placed || !l.mask_mode.empty() || off.x != 0 || off.y != 0 || p.scale != 1;
    if (moved) {
        float order = 1;
        if (l.depth) order = *l.depth;
        else if (e.layer.size() == 2 && e.layer[0] == '#' && std::isalpha(static_cast<unsigned char>(e.layer[1])))
            order = static_cast<float>(std::toupper(static_cast<unsigned char>(e.layer[1])) - 'A' + 1);
        // maskEdge surfaces (split-screen windows) cover the main stage, as in Fate Café; other moved
        // layers go under the main characters.
        p.order = edge ? kSurfaceLayerOrder + order : order;
    }
    return p;
}

namespace {
template <class F>
void for_each_anim(Stage& s, F&& f) {
    f(s.background_mix);
    for (auto& [key, l] : s.sub_layers) {
        f(l.offset.x);
        f(l.offset.y);
        f(l.scale);
        f(l.opacity);
    }
    for (auto& [code, e] : s.elements) {
        f(e.swap_mix);
        f(e.opacity);
        f(e.pos.x);
        f(e.pos.y);
        f(e.scale);
        f(e.roll);
        f(e.roll_y);
        f(e.attack);
        if (e.cutin) f(e.cutin->progress);
        if (e.special) f(e.special->progress);
    }
    f(s.fade.opacity);
    f(s.wipe.cover);
    f(s.camera.offset.x);
    f(s.camera.offset.y);
    f(s.camera.scale);
    f(s.camera.roll);
    f(s.camera.blur);
    if (s.communication) f(s.communication->opacity);
}
}  // namespace

void Stage::tick(double dt) {
    for_each_anim(*this, [dt](Anim& a) { a.tick(dt); });
    for (auto& [code, e] : elements) {
        e.shake.tick(dt);
        if (e.cutin && !e.cutin->progress.active() && !e.cutin->reveal) {
            // a finished cut-out leaves the character hidden
            e.shown = false;
            e.opacity.snap(0);
            e.cutin.reset();
        }
        if (e.special && !e.special->progress.active()) {
            // a finished erasure leaves the character hidden; a finished appearance just shows it
            if (!e.special->incoming) {
                e.shown = false;
                e.opacity.snap(0);
            }
            e.special.reset();
        }
    }
    for (auto& [key, l] : sub_layers)
        if (l.hiding && !l.opacity.active()) {
            l.shown = l.hiding = false;
            l.opacity.snap(1);
        }
    shake.tick(dt);
    message_shake.tick(dt);
    flash.tick(dt);
    for (auto& fx : effects) fx.elapsed += dt;
    if (title_card) {
        title_card->elapsed += dt;
        if (title_card->elapsed >= title_card->duration) title_card.reset();
    }
    if (communication && !communication->opacity.active() && communication->opacity.value() <= 0) communication.reset();
}

bool Stage::busy(WaitCat c) const {
    if (c == WaitCat::None || c == WaitCat::Se || c == WaitCat::Voice) return false;
    bool any = false;
    for_each_anim(const_cast<Stage&>(*this), [&](Anim& a) {
        if (a.cat == c && a.active()) any = true;
    });
    if (any) return true;
    switch (c) {
        case WaitCat::Flash: return flash.active && (!flash.loop || flash.fading_out);
        case WaitCat::Effect: return title_card.has_value();
        case WaitCat::CharaMove:
            for (const auto& [code, e] : elements)
                if (e.shake.timed_running()) return true;
            return false;
        default: return false;
    }
}

void Stage::finish_all() {
    for_each_anim(*this, [](Anim& a) { a.finish(); });
    if (flash.active && !flash.loop) flash.active = false;
    if (flash.fading_out) flash.active = false;
    title_card.reset();
    for (auto& [key, l] : sub_layers)
        if (l.hiding) {
            l.shown = l.hiding = false;
            l.opacity.snap(1);
        }
    for (auto& [code, e] : elements) {
        if (e.cutin && !e.cutin->reveal) {
            e.shown = false;
            e.opacity.snap(0);
        }
        e.cutin.reset();
    }
    if (communication && communication->opacity.value() <= 0) communication.reset();
}

}  // namespace vnr
