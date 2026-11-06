#include "vnr/tween.h"

#include <algorithm>
#include <cmath>

namespace vnr {

namespace {
// x(t) and y(t) of a cubic bezier with P0=(0,0), P3=(1,1); solve x(t)=x by Newton then bisection.
double cubic_bezier(double x1, double y1, double x2, double y2, double x) {
    auto bx = [&](double t) { return 3 * (1 - t) * (1 - t) * t * x1 + 3 * (1 - t) * t * t * x2 + t * t * t; };
    auto by = [&](double t) { return 3 * (1 - t) * (1 - t) * t * y1 + 3 * (1 - t) * t * t * y2 + t * t * t; };
    double lo = 0, hi = 1, t = x;
    for (int i = 0; i < 40; ++i) {
        const double v = bx(t);
        if (std::fabs(v - x) < 1e-7) break;
        if (v < x) lo = t; else hi = t;
        t = (lo + hi) / 2;
    }
    return by(t);
}
}  // namespace

Ease parse_ease(std::string_view n, Ease fallback) {
    if (n == "linear") return Ease::Linear;
    if (n == "ease") return Ease::Ease;
    if (n == "easeOutSine") return Ease::EaseOutSine;
    if (n == "easeOutQuad") return Ease::EaseOutQuad;
    if (n == "easeOutCubic") return Ease::EaseOutCubic;
    if (n == "easeOutQuart") return Ease::EaseOutQuart;
    if (n == "easeOutQuint") return Ease::EaseOutQuint;
    if (n == "easeOutExpo") return Ease::EaseOutExpo;
    if (n == "easeOutCirc") return Ease::EaseOutCirc;
    if (n == "easeInOutCubic") return Ease::EaseInOutCubic;
    if (n == "easeInOutQuint") return Ease::EaseInOutQuint;
    if (n == "easeInOutBack") return Ease::EaseInOutBack;
    return fallback;
}

double apply_ease(Ease e, double t) {
    t = std::clamp(t, 0.0, 1.0);
    const double pi = 3.14159265358979323846;
    switch (e) {
        case Ease::Linear: return t;
        case Ease::Ease: return cubic_bezier(0.25, 0.1, 0.25, 1.0, t);
        case Ease::EaseOutSine: return std::sin(t * pi / 2);
        case Ease::EaseOutQuad: return 1 - (1 - t) * (1 - t);
        case Ease::EaseOutCubic: return 1 - std::pow(1 - t, 3);
        case Ease::EaseOutQuart: return 1 - std::pow(1 - t, 4);
        case Ease::EaseOutQuint: return 1 - std::pow(1 - t, 5);
        case Ease::EaseOutExpo: return t >= 1 ? 1 : 1 - std::pow(2, -10 * t);
        case Ease::EaseOutCirc: return std::sqrt(1 - std::pow(t - 1, 2));
        case Ease::EaseInOutCubic: return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
        case Ease::EaseInOutQuint: return t < 0.5 ? 16 * std::pow(t, 5) : 1 - std::pow(-2 * t + 2, 5) / 2;
        case Ease::EaseInOutBack: {
            const double c1 = 1.70158, c2 = c1 * 1.525;
            return t < 0.5 ? (std::pow(2 * t, 2) * ((c2 + 1) * 2 * t - c2)) / 2
                           : (std::pow(2 * t - 2, 2) * ((c2 + 1) * (t * 2 - 2) + c2) + 2) / 2;
        }
    }
    return t;
}

WaitCat parse_wait(std::string_view w) {
    if (w == "fade" || w == "mask" || w == "fadeMove") return WaitCat::Fade;
    if (w == "wipe" || w == "cutin" || w == "charaCutin") return WaitCat::Wipe;
    if (w == "flash") return WaitCat::Flash;
    if (w == "camera" || w == "blur") return WaitCat::Camera;
    if (w == "scene") return WaitCat::Scene;
    if (w == "chara" || w == "charaFade" || w == "charaFadein" || w == "charaFadeout" || w == "charaFadeTime" ||
        w == "charaChange" || w == "charaCrossFade" || w == "charaFace" || w == "charaFaceFade")
        return WaitCat::CharaFade;
    if (w == "charaMove" || w == "charaScale" || w == "charaRoll" || w == "charaMoveScale") return WaitCat::CharaMove;
    if (w == "effect" || w == "fowardEffect" || w == "forwardEffect" || w == "backEffect") return WaitCat::Effect;
    if (w == "communicationChara") return WaitCat::Communication;
    if (w == "voice" || w == "tVoice" || w == "tvoice") return WaitCat::Voice;
    if (w == "se") return WaitCat::Se;
    return WaitCat::None;
}

float Anim::value() const {
    if (!active()) return to;
    const double p = apply_ease(ease, elapsed / duration);
    return static_cast<float>(from + (to - from) * p);
}

void Anim::go(float target, double seconds, Ease e, WaitCat c) {
    const float now = value();
    if (seconds <= 0) {
        snap(target);
        cat = c;
        return;
    }
    from = now;
    to = target;
    elapsed = 0;
    duration = seconds;
    ease = e;
    cat = c;
}

}  // namespace vnr
