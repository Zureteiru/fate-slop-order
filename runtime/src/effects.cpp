#include "vnr/rt/effects.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <string>

namespace vnr::rt {

namespace {

constexpr float kPi = 3.14159265f;
constexpr int S = kFxSpriteSize;

Color rgb(int r, int g, int b) { return {r / 255.f, g / 255.f, b / 255.f, 1}; }

EffectDef def(FxProgram p, bool loop, float seconds, float w, float h, Color c, Blend blend = Blend::Add,
              bool fullscreen = false) {
    EffectDef d;
    d.program = p;
    d.loop = loop;
    d.seconds = seconds;
    d.width = w;
    d.height = h;
    d.color = c;
    d.blend = blend;
    d.fullscreen = fullscreen;
    return d;
}

// Our table, most used names first (timing, size and colour follow Fate Café's entries).
const std::map<std::string, EffectDef, std::less<>>& table() {
    static const std::map<std::string, EffectDef, std::less<>> t = [] {
        std::map<std::string, EffectDef, std::less<>> m;
        using P = FxProgram;
        const Blend N = Blend::Alpha;
        m["bit_talk_10_lowlevel"] = def(P::Flare, false, 0.18f, 2600, 120, rgb(190, 225, 255));
        m["bit_talk_10_lowlevel_depth"] = m["bit_talk_10_lowlevel"];
        m["bit_talk_quickmove"] = def(P::Streaks, false, 0.18f, 600, 1000, rgb(220, 240, 255));
        m["bit_talk_quickmove"].offset = {0, 40};
        m["bit_talk_14"] = def(P::Impact, false, 0.6f, 1100, 1100, rgb(255, 222, 170));
        m["bit_talk_14"].intensity = 1.25f;
        m["bit_talk_13"] = def(P::Impact, false, 0.5f, 900, 900, rgb(255, 236, 205));
        m["bit_talk_06"] = def(P::Charge, true, 1.5f, 1100, 480, rgb(255, 214, 110));
        m["bit_talk_06"].offset = {0, 40};
        m["bit_talk_06_blue"] = m["bit_talk_06"];
        m["bit_talk_06_blue"].color = rgb(150, 225, 235);
        m["bit_talk_36"] = def(P::Charge, true, 1.5f, 1700, 1100, rgb(170, 215, 255));
        m["bit_talk_17_bl"] = def(P::Charge, true, 1.5f, 1700, 900, rgb(80, 140, 255), Blend::Add, true);
        m["bit_talk_charge_red"] = def(P::Charge, true, 1.5f, 1100, 460, rgb(255, 45, 60));
        m["bit_sepia01"] = def(P::Backdrop, true, 1, 0, 0, rgb(112, 80, 48), N, true);
        m["bit_sepia01_depth_fs"] = m["bit_sepia01"];
        m["bit_sepia01_depth_fs_02"] = m["bit_sepia01"];
        m["bit_sepia01_fade_fs"] = m["bit_sepia01"];
        m["bit_talk_noise_gr"] = def(P::Noise, true, 1, 1600, 900, rgb(200, 200, 200), N, true);
        m["bit_talk_impactlanding"] = def(P::Dust, false, 2.4f, 2200, 1400, rgb(122, 114, 106), N);
        m["bit_talk_35"] = def(P::Blood, false, 1.7f, 1600, 1000, rgb(138, 6, 8), N);
        m["bit_talk_explosion_triple02_bk"] = def(P::Fireball, false, 1.8f, 1800, 1100, rgb(190, 135, 122), N, true);
        m["bit_talk_explosion_triple02"] = def(P::Fireball, false, 3.4f, 1800, 1100, rgb(118, 104, 92), N, true);
        m["bit_talk_explosion_continuous"] = def(P::Fireball, true, 1.2f, 1800, 1100, rgb(255, 170, 90), Blend::Add, true);
        m["bit_talk_29"] = def(P::Fireball, false, 0.7f, 1800, 1100, rgb(255, 220, 90));
        m["bit_talk_07"] = def(P::Tunnel, true, 1, 1300, 1000, rgb(70, 150, 255));
        m["bit_talk_41"] = def(P::Flare, false, 0.15f, 2000, 120, rgb(205, 225, 255));
        m["bit_talk_41_fs"] = m["bit_talk_41"];
        m["bit_talk_41_fs"].fullscreen = true;
        m["bit_talk_20"] = def(P::Ripple, false, 1.1f, 500, 500, rgb(190, 170, 255));
        m["bit_talk_42"] = def(P::Speedlines, true, 2, 1700, 700, rgb(220, 235, 255));
        m["bit_talk_noble_light_r"] = def(P::Mist, true, 5, 1800, 1100, rgb(255, 252, 248), N, true);
        m["bit_talk_43h_fs"] = def(P::Mist, true, 5, 1700, 1000, rgb(70, 60, 190), N, true);
        m["bit_talk_black_aura_tsk"] = def(P::Mist, false, 1.9f, 1800, 1100, rgb(172, 165, 176), N, true);
        m["bit_talk_thunder_01"] = def(P::Lightning, false, 0.9f, 1700, 1000, rgb(120, 220, 255), Blend::Add, true);
        m["bit_talk_thunder_02"] = m["bit_talk_thunder_01"];
        m["bit_talk_h_slash02_fs"] = def(P::Slash, false, 0.55f, 1600, 90, rgb(150, 15, 15), N, true);
        m["bit_talk_12_red_l"] = def(P::Slash, false, 0.4f, 1000, 1000, rgb(255, 92, 64));
        m["bit_talk_spotlight"] = def(P::Glow, true, 3, 700, 1100, rgb(255, 248, 225));
        m["bit_talk_spotlight"].offset = {0, -150};
        m["bit_talk_spotlight02"] = m["bit_talk_spotlight"];
        m["bit_talk_4elements_light"] = def(P::Glow, true, 2, 1200, 1200, rgb(255, 240, 200));
        m["bit_talk_sparks_fs"] = def(P::Sparks, true, 1.6f, 1400, 900, rgb(255, 200, 120), Blend::Add, true);
        m["bit_talk_urd_threads"] = def(P::Speedlines, true, 3, 1600, 1000, rgb(235, 225, 255));
        m["bit_talk_barrier02_fs"] = def(P::Ripple, true, 1.6f, 1100, 1100, rgb(170, 220, 255), Blend::Add, true);
        m["bit_talk_magic_com_w"] = def(P::Charge, true, 1.5f, 900, 900, rgb(240, 245, 255));
        m["bit_talk_tsk_shadow"] = def(P::Mist, true, 4, 1400, 1000, rgb(40, 30, 50), N);
        return m;
    }();
    return t;
}

// Keyword fallbacks for names the table does not have (checked in order).
const EffectDef* by_keyword(const std::string& k) {
    using P = FxProgram;
    static const EffectDef explosion = def(P::Fireball, false, 1.6f, 1600, 1000, rgb(255, 170, 90), Blend::Add, true);
    static const EffectDef thunder = def(P::Lightning, false, 0.9f, 1600, 1000, rgb(150, 220, 255), Blend::Add, true);
    static const EffectDef slash = def(P::Slash, false, 0.4f, 1200, 1200, rgb(235, 240, 255));
    static const EffectDef aura = def(P::Mist, true, 3, 1200, 1000, rgb(160, 150, 200), Blend::Alpha);
    static const EffectDef sparks = def(P::Sparks, true, 1.6f, 1200, 900, rgb(255, 210, 140));
    static const EffectDef light = def(P::Glow, true, 2, 1000, 1000, rgb(255, 245, 220));
    static const EffectDef charge = def(P::Charge, true, 1.5f, 1000, 700, rgb(200, 220, 255));
    static const EffectDef ripple = def(P::Ripple, true, 1.6f, 900, 900, rgb(180, 220, 255));
    static const EffectDef dust = def(P::Dust, false, 2, 1800, 1200, rgb(130, 120, 110), Blend::Alpha);
    static const EffectDef blood = def(P::Blood, false, 1.5f, 1400, 900, rgb(140, 8, 10), Blend::Alpha);
    static const EffectDef noise = def(P::Noise, true, 1, 1600, 900, rgb(200, 200, 200), Blend::Alpha, true);
    static const EffectDef impact = def(P::Impact, false, 0.5f, 1000, 1000, rgb(255, 235, 200));
    static const EffectDef sepia = def(P::Backdrop, true, 1, 0, 0, rgb(112, 80, 48), Blend::Alpha, true);
    const std::pair<const char*, const EffectDef*> keys[] = {
        {"sepia", &sepia}, {"explosion", &explosion}, {"bomb", &explosion}, {"fire", &explosion},
        {"thunder", &thunder}, {"lightning", &thunder}, {"slash", &slash}, {"cut", &slash},
        {"aura", &aura}, {"shadow", &aura}, {"smoke", &aura}, {"mist", &aura}, {"fog", &aura},
        {"spark", &sparks}, {"light", &light}, {"spot", &light}, {"shine", &light}, {"charge", &charge},
        {"magic", &charge}, {"barrier", &ripple}, {"shield", &ripple}, {"dust", &dust}, {"landing", &dust},
        {"blood", &blood}, {"noise", &noise}, {"hit", &impact}, {"impact", &impact}, {"damage", &impact},
    };
    for (const auto& [key, d] : keys)
        if (k.find(key) != std::string::npos) return d;
    return nullptr;
}

// Deterministic noise in 0..1.
float rnd(std::uint32_t seed, std::uint32_t i, std::uint32_t j = 0) {
    std::uint32_t h = seed ^ (i * 0x9e3779b9u) ^ (j * 0x85ebca6bu);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return static_cast<float>(h & 0xffffffu) / static_cast<float>(0xffffff);
}

float ease_out(float x) { return 1 - (1 - x) * (1 - x) * (1 - x); }
float clamp01(float x) { return std::clamp(x, 0.f, 1.f); }

struct Ctx {
    const EffectDef& d;
    Vec2 a;  // centre (stage units)
    bool fh, fv;
    TextureId tex;
    DrawList& out;

    // A sprite of size w x h centred at c (relative to the anchor), turned by `deg`, colour c, alpha k.
    void sprite(FxSprite s, Vec2 c, float w, float h, float deg, Color col, float k, Blend blend) const {
        if (k <= 0.002f || w <= 0 || h <= 0) return;
        if (fh) {
            c.x = -c.x;
            deg = -deg;
        }
        if (fv) {
            c.y = -c.y;
            deg = -deg;
        }
        const Vec2 p{a.x + c.x, a.y + c.y};
        Quad q;
        q.texture = tex;
        q.src = fx_sprite_rect(s);
        q.pos[0] = {p.x - w / 2, p.y - h / 2};
        q.pos[1] = {p.x + w / 2, p.y - h / 2};
        q.pos[2] = {p.x + w / 2, p.y + h / 2};
        q.pos[3] = {p.x - w / 2, p.y + h / 2};
        q.color = {col.r, col.g, col.b, col.a * clamp01(k)};
        q.blend = blend;
        rotate(q, p, deg);
        out.quads.push_back(q);
    }
    void sprite(FxSprite s, Vec2 c, float w, float h, float deg, Color col, float k) const {
        sprite(s, c, w, h, deg, col, k, d.blend);
    }
};

Color mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
const Color kWhite{1, 1, 1, 1};

// Programs: u = 0..1 through one run (or one loop period), e = seconds elapsed.
void run(const Ctx& c, float u, float e, std::uint32_t seed) {
    const EffectDef& d = c.d;
    const float W = d.width, H = d.height, I = d.intensity;
    switch (d.program) {
        case FxProgram::Flare: {  // a horizontal light streak that flashes across
            const float k = (u < 0.3f ? u / 0.3f : 1 - (u - 0.3f) / 0.7f) * I;
            c.sprite(FxSprite::Streak, {0, 0}, W * (0.6f + 0.4f * ease_out(u)), H * 0.7f, 0, d.color, k);
            c.sprite(FxSprite::Streak, {0, 0}, W * 0.8f, H * 0.22f, 0, kWhite, k);
            c.sprite(FxSprite::Glow, {0, 0}, H * 3, H * 3, 0, d.color, 0.6f * k);
            break;
        }
        case FxProgram::Streaks: {  // a handful of fast vertical light streaks
            for (std::uint32_t i = 0; i < 10; ++i) {
                const float x = (rnd(seed, i) - 0.5f) * W, len = H * (0.3f + 0.4f * rnd(seed, i, 1));
                const float y = -H / 2 - len / 2 + (H + len) * clamp01(u * 1.4f - rnd(seed, i, 2) * 0.4f);
                c.sprite(FxSprite::Streak, {x, y}, len, 14 + 10 * rnd(seed, i, 3), 90, d.color, (1 - u) * I);
            }
            break;
        }
        case FxProgram::Impact: {  // flash, a widening ring and rays
            const float o = ease_out(u), k = (1 - u) * I;
            c.sprite(FxSprite::Glow, {0, 0}, std::min(W, H) * (0.3f + 0.5f * u), std::min(W, H) * (0.3f + 0.5f * u), 0,
                     d.color, (1 - u) * (1 - u) * I);
            c.sprite(FxSprite::Halo, {0, 0}, W * (0.2f + 0.8f * o), H * (0.2f + 0.8f * o), 0, d.color, k);
            for (std::uint32_t i = 0; i < 12; ++i) {
                const float ang = (static_cast<float>(i) + rnd(seed, i) * 0.5f) / 12 * 360, len = W * 0.45f * o;
                const float r = len / 2 + W * 0.05f;
                const float rad = ang * kPi / 180;
                c.sprite(FxSprite::Streak, {r * std::cos(rad), r * std::sin(rad)}, len, 16, ang, d.color, k);
            }
            break;
        }
        case FxProgram::Charge: {  // sparks drawn into a pulsing glow
            const Color glow = d.color2.a > 0 ? d.color2 : d.color;
            const float pulse = 0.85f + 0.15f * std::sin(u * 2 * kPi);
            c.sprite(FxSprite::Glow, {0, 0}, W * 0.45f * pulse, H * 0.6f * pulse, 0, glow, 0.55f * I);
            for (std::uint32_t i = 0; i < 18; ++i) {
                const float p = std::fmod(u + rnd(seed, i), 1.f), ang = rnd(seed, i, 1) * 2 * kPi;
                const float r = (1 - p) * 0.5f;
                c.sprite(FxSprite::Spark, {r * W * std::cos(ang), r * H * std::sin(ang)}, 34, 34, p * 90, d.color,
                         std::sin(p * kPi) * I);
            }
            break;
        }
        case FxProgram::Backdrop: break;  // a screen filter (sepia), applied by the scene
        case FxProgram::Noise: {  // film grain over the view, a new pattern 24 times a second
            const auto frame = static_cast<std::uint32_t>(e * 24);
            const float tile = S * 2.f, ox = -rnd(seed, frame) * tile, oy = -rnd(seed, frame, 1) * tile;
            for (float y = oy; y < kViewTop + kViewHeight; y += tile)
                for (float x = ox; x < static_cast<float>(kStageWidth); x += tile)
                    c.sprite(FxSprite::Grain, {x + tile / 2 - c.a.x, y + tile / 2 - c.a.y}, tile, tile, 0, d.color, 0.16f * I);
            break;
        }
        case FxProgram::Dust: {  // a cloud kicked up along the ground, settling
            const float k = (u < 0.08f ? u / 0.08f : 1 - (u - 0.08f) / 0.92f) * 0.95f * I;
            for (std::uint32_t i = 0; i < 16; ++i) {
                const float side = rnd(seed, i) * 2 - 1, sz = (180 + 220 * rnd(seed, i, 1)) * (0.6f + 0.8f * ease_out(u));
                const Vec2 p{side * W * 0.45f * ease_out(u) * (0.3f + 0.7f * rnd(seed, i, 2)),
                             H * 0.2f - H * 0.25f * ease_out(u) * rnd(seed, i, 3)};
                c.sprite(FxSprite::Puff, p, sz, sz * 0.7f, rnd(seed, i, 4) * 360, d.color, k);
            }
            break;
        }
        case FxProgram::Blood: {  // dark splatter thrown out from the hit
            const float k = std::pow(1 - u, 1.5f) * I;
            for (std::uint32_t i = 0; i < 18; ++i) {
                const float ang = rnd(seed, i) * 2 * kPi, r = W * 0.35f * ease_out(clamp01(u * 3)) * rnd(seed, i, 1);
                const float sz = 50 + 110 * rnd(seed, i, 2);
                c.sprite(FxSprite::Puff, {r * std::cos(ang), r * std::sin(ang) * H / W}, sz, sz * 0.8f, ang * 57.3f, d.color, k);
            }
            break;
        }
        case FxProgram::Fireball: {  // billowing puffs from a bright core
            const float o = ease_out(u);
            c.sprite(FxSprite::Glow, {0, 0}, W * 0.5f * (0.4f + o), H * 0.6f * (0.4f + o), 0, rgb(255, 210, 140),
                     (1 - u) * (1 - u) * I, Blend::Add);
            for (std::uint32_t i = 0; i < 16; ++i) {
                const float ang = rnd(seed, i) * 2 * kPi, r = W * 0.3f * o * rnd(seed, i, 1);
                const float sz = (200 + 300 * rnd(seed, i, 2)) * (0.5f + u);
                const Color col = mix(rgb(255, 220, 150), d.color, clamp01(u * 2));
                c.sprite(FxSprite::Puff, {r * std::cos(ang), r * std::sin(ang) * H / W}, sz, sz, ang * 57.3f, col,
                         (1 - u) * 0.9f * I);
            }
            break;
        }
        case FxProgram::Tunnel: {  // rings rushing outwards
            for (std::uint32_t i = 0; i < 6; ++i) {
                const float p = std::fmod(u + static_cast<float>(i) / 6, 1.f);
                c.sprite(FxSprite::Halo, {0, 0}, W * p * p, H * p * p, 0, d.color, std::sin(p * kPi) * 0.7f * I);
            }
            break;
        }
        case FxProgram::Ripple: {  // three expanding rings
            for (std::uint32_t i = 0; i < 3; ++i) {
                const float p = clamp01((u - static_cast<float>(i) * 0.15f) / 0.7f);
                if (p <= 0) continue;
                c.sprite(FxSprite::Halo, {0, 0}, W * ease_out(p), H * ease_out(p), 0, d.color, (1 - p) * I);
            }
            break;
        }
        case FxProgram::Speedlines: {  // flickering radial lines around the edge
            const auto frame = static_cast<std::uint32_t>(e * 20);
            for (std::uint32_t i = 0; i < 40; ++i) {
                if (rnd(seed, i, frame) < 0.45f) continue;
                const float ang = rnd(seed, i) * 360, rad = ang * kPi / 180, len = 150 + 250 * rnd(seed, i, 1);
                const float r = 0.5f * std::max(W, H) * (0.55f + 0.3f * rnd(seed, i, 2));
                c.sprite(FxSprite::Streak, {r * std::cos(rad), r * std::sin(rad) * H / W}, len, 6, ang, d.color, 0.8f * I);
            }
            break;
        }
        case FxProgram::Mist: {  // large soft puffs drifting
            const float k = (d.loop ? 1.f : (u < 0.2f ? u / 0.2f : 1 - (u - 0.2f) / 0.8f)) * 0.35f * I;
            for (std::uint32_t i = 0; i < 10; ++i) {
                const float drift = std::fmod(rnd(seed, i, 1) + u * 0.25f, 1.f) - 0.5f;
                const Vec2 p{(rnd(seed, i) - 0.5f) * W + drift * 120, (rnd(seed, i, 2) - 0.5f) * H * 0.8f};
                const float sz = 500 + 400 * rnd(seed, i, 3);
                c.sprite(FxSprite::Puff, p, sz, sz * 0.75f, rnd(seed, i, 4) * 360 + u * 20, d.color, k);
            }
            break;
        }
        case FxProgram::Lightning: {  // a jagged bolt, redrawn every 60 ms, and a flash
            const auto shape = static_cast<std::uint32_t>(e / 0.06f);
            const float k = (1 - u) * I;
            c.sprite(FxSprite::Glow, {0, 0}, W, H, 0, d.color, (u < 0.15f ? 0.5f : 0.f) * I);
            Vec2 prev{(rnd(seed, shape) - 0.5f) * W * 0.3f, -H / 2};
            for (std::uint32_t i = 1; i <= 10; ++i) {
                const Vec2 next{prev.x + (rnd(seed, shape, i) - 0.5f) * W * 0.1f, -H / 2 + H * static_cast<float>(i) / 10};
                const float dx = next.x - prev.x, dy = next.y - prev.y;
                const float len = std::sqrt(dx * dx + dy * dy), ang = std::atan2(dy, dx) * 180 / kPi;
                const Vec2 mid{(prev.x + next.x) / 2, (prev.y + next.y) / 2};
                c.sprite(FxSprite::Streak, mid, len * 1.15f, 22, ang, d.color, k);
                c.sprite(FxSprite::Streak, mid, len * 1.1f, 8, ang, kWhite, k);
                prev = next;
            }
            break;
        }
        case FxProgram::Slash: {  // a blade stroke drawn out, then fading
            const float grow = ease_out(clamp01(u / 0.3f)), k = (u < 0.3f ? 1 : 1 - (u - 0.3f) / 0.7f) * I;
            const bool tall = H > W;
            const float len = (tall ? H : W) * grow, thick = std::max(30.f, (tall ? W : H) * 0.25f);
            const float ang = tall ? 60.f : -25.f;
            c.sprite(FxSprite::Streak, {0, 0}, len, thick, ang, d.color, k);
            c.sprite(FxSprite::Streak, {0, 0}, len * 0.9f, thick * 0.3f, ang, d.blend == Blend::Add ? kWhite : d.color, k);
            break;
        }
        case FxProgram::Glow: {  // a soft light, gently breathing
            const float k = (0.75f + 0.25f * std::sin(u * 2 * kPi)) * 0.6f * I;
            c.sprite(FxSprite::Glow, {0, 0}, W, H, 0, d.color, k);
            break;
        }
        case FxProgram::Sparks: {  // embers rising
            for (std::uint32_t i = 0; i < 24; ++i) {
                const float p = std::fmod(u + rnd(seed, i), 1.f);
                const Vec2 pos{(rnd(seed, i, 1) - 0.5f) * W + std::sin(p * 6 + static_cast<float>(i)) * 20, H / 2 - p * H};
                c.sprite(FxSprite::Spark, pos, 22, 22, p * 180, d.color, std::sin(p * kPi) * I);
            }
            break;
        }
    }
}

void put(Image& img, int sprite, int x, int y, float a) {
    std::uint8_t* p = &img.rgba[(static_cast<std::size_t>(y) * img.width + sprite * S + x) * 4];
    p[0] = p[1] = p[2] = 255;
    p[3] = static_cast<std::uint8_t>(std::lround(clamp01(a) * 255));
}

}  // namespace

const EffectDef* find_effect(std::string_view name) {
    std::string k(name);
    for (char& ch : k) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (const auto it = table().find(k); it != table().end()) return &it->second;
    return by_keyword(k);
}

Rect fx_sprite_rect(FxSprite s) {
    return {static_cast<float>(static_cast<int>(s) * S) + 0.5f, 0.5f, S - 1.f, S - 1.f};
}

Image make_effect_atlas() {
    Image img;
    img.width = S * static_cast<int>(FxSprite::kCount);
    img.height = S;
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 0);
    std::uint32_t grain = 12345;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            const float u = (x + 0.5f) / S * 2 - 1, v = (y + 0.5f) / S * 2 - 1, d = std::sqrt(u * u + v * v);
            const float fall = clamp01(1 - d);
            put(img, static_cast<int>(FxSprite::Glow), x, y, fall * fall);
            put(img, static_cast<int>(FxSprite::Streak), x, y, std::exp(-v * v * 9) * clamp01(1 - u * u));
            const float wob = 0.15f * (std::sin(u * 7 + v * 3) + std::sin(v * 9 - u * 5)) / 2;
            put(img, static_cast<int>(FxSprite::Puff), x, y, clamp01(1 - d - wob) * clamp01(1 - d - wob) * 1.6f);
            put(img, static_cast<int>(FxSprite::Halo), x, y, std::exp(-((d - 0.82f) / 0.09f) * ((d - 0.82f) / 0.09f)));
            const float star = std::max(std::exp(-std::fabs(u) * 14) * clamp01(1 - std::fabs(v)),
                                        std::exp(-std::fabs(v) * 14) * clamp01(1 - std::fabs(u)));
            put(img, static_cast<int>(FxSprite::Spark), x, y, std::max(star, fall * fall * fall * 1.4f));
            const float fy = (v + 1) / 2, fw = 0.5f * std::sqrt(clamp01(fy)) * (1.2f - fy);
            put(img, static_cast<int>(FxSprite::Flame), x, y, clamp01(1 - std::fabs(u) / std::max(0.01f, fw)) * clamp01(fy * 3));
            put(img, static_cast<int>(FxSprite::Petal), x, y, clamp01((1 - (u * u / 0.25f + v * v)) * 4));
            grain = grain * 1664525u + 1013904223u;
            std::uint8_t* g = &img.rgba[(static_cast<std::size_t>(y) * img.width + static_cast<int>(FxSprite::Grain) * S + x) * 4];
            g[0] = g[1] = g[2] = static_cast<std::uint8_t>(grain >> 24);
            g[3] = 255;
        }
    }
    return img;
}

std::uint32_t fx_seed(std::string_view name) {
    std::uint32_t h = 2166136261u;
    for (char ch : name) h = (h ^ static_cast<unsigned char>(ch)) * 16777619u;
    return h;
}

void draw_effect(const EffectDef& def, double elapsed, Vec2 anchor, bool flip_h, bool flip_v, TextureId atlas,
                 std::uint32_t seed, DrawList& out) {
    if (!atlas || def.program == FxProgram::Backdrop) return;
    const float period = std::max(0.01f, def.seconds);
    const auto e = static_cast<float>(elapsed);
    if (!def.loop && e >= period) return;
    const float u = def.loop ? std::fmod(e, period) / period : e / period;
    const Vec2 a = def.fullscreen ? Vec2{kViewSafeLeft + kViewSafeWidth / 2, kViewTop + kViewHeight / 2}
                                  : Vec2{anchor.x + def.offset.x, anchor.y + def.offset.y};
    run(Ctx{def, a, flip_h, flip_v, atlas, out}, u, e, seed);
}

}  // namespace vnr::rt
