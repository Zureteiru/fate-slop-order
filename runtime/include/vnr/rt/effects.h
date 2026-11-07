// Named script effects (effect / fowardEffect / backEffect / charaEffect "bit_talk_…"), redrawn by our own
// programs from a procedural sprite atlas. The game's particle files are not used: each name maps to a
// program plus timing, size and colour (following Fate Café's numbers where it has the name), and
// names we have no entry for fall back by keyword (explosion, thunder, slash, aura, …) or draw nothing.
#pragma once

#include <cstdint>
#include <string_view>

#include "vnr/rt/draw_list.h"

namespace vnr::rt {

enum class FxProgram : std::uint8_t {
    Flare, Streaks, Impact, Charge, Backdrop, Noise, Dust, Blood, Fireball, Tunnel, Ripple, Speedlines,
    Mist, Lightning, Slash, Glow, Sparks,
};

struct EffectDef {
    FxProgram program = FxProgram::Glow;
    bool loop = false;
    float seconds = 1;        // one run (or one loop period)
    float width = 600, height = 600;  // stage units, centred on the anchor
    Vec2 offset;              // from the anchor, y down
    Color color{1, 1, 1, 1};
    Color color2{1, 1, 1, 0}; // second colour (alpha 0 = none)
    Blend blend = Blend::Add; // light by default; smoke, blood and dust draw normally
    bool fullscreen = false;  // covers the view instead of following a character
    float intensity = 1;
};

// The definition for a script name, or nullptr when we draw nothing for it.
const EffectDef* find_effect(std::string_view name);

// Sprites drawn into one atlas at start-up (white shapes with alpha; tinted per quad).
enum class FxSprite : std::uint8_t { Glow, Streak, Puff, Halo, Spark, Flame, Petal, Grain, kCount };
constexpr int kFxSpriteSize = 128;
Image make_effect_atlas();
Rect fx_sprite_rect(FxSprite s);

// Appends the quads for one effect `elapsed` seconds in. `anchor` is the stage point it is centred on
// (the stage centre, or the character it belongs to); `seed` varies the particles between effects.
void draw_effect(const EffectDef& def, double elapsed, Vec2 anchor, bool flip_h, bool flip_v, TextureId atlas,
                 std::uint32_t seed, DrawList& out);

// Stable seed for a name (particle layouts stay the same each frame).
std::uint32_t fx_seed(std::string_view name);

}  // namespace vnr::rt
