// Animated values with easing, tagged with the [wait …] category they belong to.
#pragma once

#include <string_view>

#include "vnr/command.h"

namespace vnr {

enum class Ease {
    Linear,
    Ease,  // CSS "ease" = cubic-bezier(.25,.1,.25,1)
    EaseOutSine,
    EaseOutQuad,
    EaseOutCubic,
    EaseOutQuart,
    EaseOutQuint,
    EaseOutExpo,
    EaseOutCirc,
    EaseInOutCubic,
    EaseInOutQuint,
    EaseInOutBack,
};
// Script easing name (e.g. "easeOutQuint"); unknown or empty -> fallback.
Ease parse_ease(std::string_view name, Ease fallback = Ease::Linear);
double apply_ease(Ease e, double t);  // t in [0,1]

// What a [wait X] command waits for.
enum class WaitCat {
    None,
    Fade,          // fadein/fadeout/fadeMove/maskin/maskout  ([wait fade], [wait mask])
    Wipe,          // wipein/wipeout/charaCutin             ([wait wipe])
    Flash,         // flashin/flashout                       ([wait flash])
    Camera,        // cameraMove/cameraHome/cameraRoll/blur  ([wait camera])
    Scene,         // background cross-fade                  ([wait scene])
    CharaFade,     // charaFadein/charaFadeout/charaFadeTime/charaChange/charaFaceFade ([wait chara], [wait charaFade…])
    CharaMove,     // charaMove/charaScale/charaRoll         ([wait charaMove])
    Effect,        // title cards and timed effects          ([wait effect], [wait fowardEffect])
    Communication, // communicationChara in/out              ([wait communicationChara])
    Voice,         // the current voice line (reported by the app) ([wait voice], [wait tVoice])
    Se,            // sound effects: the core cannot know their length, never blocks ([wait se])
};
WaitCat parse_wait(std::string_view target);

struct Anim {
    float from = 0, to = 0;
    double elapsed = 0, duration = 0;
    Ease ease = Ease::Linear;
    WaitCat cat = WaitCat::None;

    float value() const;
    bool active() const { return duration > 0 && elapsed < duration; }
    void snap(float v) { from = to = v; elapsed = duration = 0; }
    // Animate from the current value to `target`. duration <= 0 snaps.
    void go(float target, double seconds, Ease e, WaitCat c);
    void tick(double dt) { if (active()) elapsed += dt; }
    void finish() { snap(to); }
};

struct Anim2 {
    Anim x, y;
    Vec2 value() const { return {x.value(), y.value()}; }
    bool active() const { return x.active() || y.active(); }
    void snap(Vec2 v) { x.snap(v.x); y.snap(v.y); }
    void go(Vec2 target, double seconds, Ease e, WaitCat c) { x.go(target.x, seconds, e, c); y.go(target.y, seconds, e, c); }
    void tick(double dt) { x.tick(dt); y.tick(dt); }
    void finish() { x.finish(); y.finish(); }
    WaitCat cat() const { return x.cat; }
};

}  // namespace vnr
