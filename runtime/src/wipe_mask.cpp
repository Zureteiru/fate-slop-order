#include "vnr/rt/wipe_mask.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace vnr::rt {

namespace {

constexpr int N = kWipeFieldSize;
constexpr float kPi = 3.14159265f;

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// Stable per-cell noise in 0..1.
float hash01(int x, int y) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xffffu) / 65535.f;
}

// Linear direction from the pattern name: 0 at the side the cover starts from.
bool linear(const std::string& k, float u, float y, float& v) {
    const bool l2r = has(k, "lefttoright"), r2l = has(k, "righttoleft"), u2d = has(k, "uptodown"), d2u = has(k, "downtoup");
    if (has(k, "leftuptorightdown")) v = (u + y) / 2;
    else if (has(k, "rightdowntoleftup")) v = 1 - (u + y) / 2;
    else if (has(k, "rightuptoleftdown")) v = (1 - u + y) / 2;
    else if (has(k, "leftdowntorightup")) v = (u + 1 - y) / 2;
    else if (l2r) v = u;
    else if (r2l) v = 1 - u;
    else if (u2d) v = y;
    else if (d2u) v = 1 - y;
    else return false;
    return true;
}

}  // namespace

float wipe_alpha(float v, float p, float g) {
    const float r = p * (1 + g);
    if (g <= 0) return v < r ? 1.f : 0.f;
    return std::clamp((r - v) / g, 0.f, 1.f);
}

std::string wipe_key(std::string_view pattern) {
    std::string k = lower(pattern);
    while (!k.empty() && std::isspace(static_cast<unsigned char>(k.back()))) k.pop_back();
    k.erase(0, std::min(k.size(), k.find_first_not_of(" \t")));
    if (k == "rectanglelefttoright") return "rectanglestriplefttoright";
    if (k == "rectanglerighttoleft") return "rectanglestriprighttoleft";
    return k;
}

const std::vector<std::string>& wipe_texture_names() {
    static const std::vector<std::string> names = {
        "cinema", "circleIn", "circleOut", "clash", "cutAcross", "damage", "downToUp", "fire",
        "heartOutBig", "leftDownToRightUp", "leftToRight", "leftUpToRightDown", "magic", "moya",
        "openEye", "polka02", "polka04", "rectangleStripDownToUp", "rectangleStripLeftToRight",
        "rectangleStripRightToLeft", "rectangleStripUpToDown", "rightDownToLeftUp", "rightToLeft",
        "rightUpToLeftDown", "rollFlashRight", "rollLeft", "rollRight", "sazanami", "sideBlind",
        "upToDown", "uzumaki", "uzumakiBig", "windmill"};
    return names;
}

std::string wipe_texture_file(std::string_view pattern) {
    const std::string k = wipe_key(pattern);
    for (const std::string& n : wipe_texture_names())
        if (lower(n) == k) return "wipes/" + n + ".png";
    return {};
}

WipeField wipe_field_from_image(const Image& img) {
    WipeField f;
    if (img.width <= 0 || img.height <= 0 || img.rgba.size() < static_cast<std::size_t>(img.width) * img.height * 4) return f;
    f.v.resize(static_cast<std::size_t>(N) * N);
    auto red = [&](int x, int y) { return img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4] / 255.f; };
    // bilinear, like the browser's drawImage into the 256x256 canvas
    for (int y = 0; y < N; ++y) {
        const float sy = std::clamp((y + 0.5f) * img.height / N - 0.5f, 0.f, img.height - 1.f);
        const int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, img.height - 1);
        const float ty = sy - y0;
        for (int x = 0; x < N; ++x) {
            const float sx = std::clamp((x + 0.5f) * img.width / N - 0.5f, 0.f, img.width - 1.f);
            const int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, img.width - 1);
            const float tx = sx - x0;
            const float top = red(x0, y0) + (red(x1, y0) - red(x0, y0)) * tx;
            const float bot = red(x0, y1) + (red(x1, y1) - red(x0, y1)) * tx;
            f.v[static_cast<std::size_t>(y) * N + x] = top + (bot - top) * ty;
        }
    }
    return f;
}

// Shapes measured from the installed textures: v is low where the cover arrives first.
WipeField procedural_wipe_field(std::string_view pattern) {
    const std::string k = wipe_key(pattern);
    enum class Kind { None, Linear, Strip, CircleIn, CircleOut, Eye, RollCcw, RollCw, Side, Ver, Noise, Moza, Cinema };
    Kind kind = Kind::None;
    float probe = 0;
    if (has(k, "rectanglestrip") && linear(k, 0, 0, probe)) kind = Kind::Strip;
    else if (linear(k, 0, 0, probe)) kind = Kind::Linear;
    else if (has(k, "circlein")) kind = Kind::CircleIn;
    else if (has(k, "circleout")) kind = Kind::CircleOut;
    else if (has(k, "openeye")) kind = Kind::Eye;
    else if (has(k, "rollleft")) kind = Kind::RollCcw;
    else if (has(k, "rollright") || has(k, "rollflashright")) kind = Kind::RollCw;
    else if (has(k, "sideblind") || has(k, "cutside")) kind = Kind::Side;
    else if (has(k, "verblind") || has(k, "cutver")) kind = Kind::Ver;
    else if (k == "noise") kind = Kind::Noise;
    else if (has(k, "moza")) kind = Kind::Moza;
    else if (has(k, "cinema")) kind = Kind::Cinema;
    WipeField f;
    if (kind == Kind::None) return f;
    f.v.resize(static_cast<std::size_t>(N) * N);
    for (int py = 0; py < N; ++py) {
        for (int px = 0; px < N; ++px) {
            const float u = (px + 0.5f) / N, y = (py + 0.5f) / N;
            const float dx = u - 0.5f, dy = y - 0.5f, d = std::sqrt(dx * dx + dy * dy);
            float v = 0;
            switch (kind) {
                case Kind::Linear: linear(k, u, y, v); break;
                case Kind::Strip: {  // ragged blocks along the direction
                    linear(k, u, y, v);
                    v = 0.65f * v + 0.35f * hash01(px / 32, py / 16);
                    break;
                }
                case Kind::CircleIn: v = 1 - std::min(1.f, d / 0.75f); break;
                case Kind::CircleOut: v = std::min(1.f, d / 0.75f); break;
                case Kind::Eye: v = std::clamp(1 - 3 * std::fabs(dy), 0.f, 1.f); break;
                case Kind::RollCcw:
                case Kind::RollCw: {  // a clock hand sweeping from 12 o'clock
                    float a = std::atan2(dx, -dy);  // clockwise from up, -pi..pi
                    if (a < 0) a += 2 * kPi;
                    v = a / (2 * kPi);
                    if (kind == Kind::RollCcw) v = 1 - v;
                    break;
                }
                case Kind::Side: v = 2 * std::fabs(dx); break;
                case Kind::Ver: v = 2 * std::fabs(dy); break;
                case Kind::Noise: v = hash01(px, py); break;
                case Kind::Moza: v = hash01(px / 16, py / 16); break;
                case Kind::Cinema: v = std::fabs(dy) < 0.365f ? 1.f : 0.f; break;
                case Kind::None: break;
            }
            f.v[static_cast<std::size_t>(py) * N + px] = std::clamp(v, 0.f, 1.f);
        }
    }
    return f;
}

void fill_wipe_mask(const WipeField& f, float p, float g, Rgba color, Image& out) {
    out.width = out.height = N;
    out.rgba.resize(static_cast<std::size_t>(N) * N * 4);
    const float a = color.a / 255.f;
    for (std::size_t i = 0; i < static_cast<std::size_t>(N) * N; ++i) {
        std::uint8_t* px = &out.rgba[i * 4];
        px[0] = color.r;
        px[1] = color.g;
        px[2] = color.b;
        const float v = f.ok() ? f.v[i] : 0.f;
        px[3] = static_cast<std::uint8_t>(std::lround(wipe_alpha(v, p, g) * a * 255));
    }
}

void draw_wipe_layer(const WipeLayer& w, DrawList& out) {
    const float kW = static_cast<float>(kStageWidth), kH = static_cast<float>(kStageHeight);
    const Color c{w.color.r / 255.f, w.color.g / 255.f, w.color.b / 255.f, w.color.a / 255.f};
    if (w.bars) {  // Fate Café's cinema fallback: black bars 13.5% of the view high
        const float h = kViewHeight * 0.135f;
        out.rect({0, kViewTop, kW, h}, c);
        out.rect({0, kViewTop + kViewHeight - h, kW, kH - (kViewTop + kViewHeight - h)}, c);
        return;
    }
    if (w.texture) {
        // The field covers the 16:9 view; wider screens and the bleed repeat its edge pixels.
        const float n = static_cast<float>(N), l = kViewSafeLeft, r = kViewSafeLeft + kViewSafeWidth;
        const float t = kViewTop, b = kViewTop + kViewHeight;
        out.image(w.texture, {0, 0, n, n}, {l, t, kViewSafeWidth, kViewHeight});
        out.image(w.texture, {0, 0, 1, n}, {0, t, l, kViewHeight});
        out.image(w.texture, {n - 1, 0, 1, n}, {r, t, kW - r, kViewHeight});
        out.image(w.texture, {0, n - 1, n, 1}, {0, b, kW, kH - b});
        return;
    }
    if (w.even > 0) out.rect({0, 0, kW, kH}, {c.r, c.g, c.b, c.a * w.even});
}

void WipeMasks::install(std::string_view pattern, WipeField f) {
    if (!f.ok()) return;
    installed_[wipe_key(pattern)] = std::move(f);
    wipe_mask_.key.clear();  // the masks may show this pattern: redraw them
    filter_mask_.key.clear();
}

bool WipeMasks::installed(std::string_view pattern) const { return installed_.count(wipe_key(pattern)) > 0; }

const WipeField* WipeMasks::field(const std::string& key) {
    if (const auto it = installed_.find(key); it != installed_.end()) return &it->second;
    auto it = builtin_.find(key);
    if (it == builtin_.end()) it = builtin_.emplace(key, procedural_wipe_field(key)).first;
    return it->second.ok() ? &it->second : nullptr;
}

void WipeMasks::layer(const std::string& pattern, float p, float g, Rgba color, Mask& m, WipeLayer& out,
                      bool cinema_bars, TextureStore& textures) {
    out = WipeLayer{};
    out.color = color;
    const std::string key = wipe_key(pattern);
    if (cinema_bars && !installed_.count(key) && has(key, "cinema")) {
        out.bars = true;
        return;
    }
    const WipeField* f = field(key);
    if (!f) {  // no shape known: an even fade
        out.even = p;
        return;
    }
    if (p >= 1 && g >= 0) {  // fully covered whatever the field
        out.even = 1;
        return;
    }
    if (!m.texture) {
        Image blank;
        fill_wipe_mask(WipeField{}, 0, 0, color, blank);
        m.texture = textures.create(std::move(blank), true);
    }
    if (m.key != key || m.p != p || m.g != g || !(m.color == color)) {
        if (Image* img = textures.pixels(m.texture)) {
            fill_wipe_mask(*f, p, g, color, *img);
            textures.update(m.texture);
        }
        m.key = key;
        m.p = p;
        m.g = g;
        m.color = color;
    }
    out.texture = m.texture;
}

void WipeMasks::update(const Stage& st, TextureStore& textures) {
    const float cover = st.wipe.cover.value();
    if (cover > 0) layer(st.wipe.pattern, cover, st.wipe.softness, st.wipe.color, wipe_mask_, wipe, false, textures);
    else wipe = WipeLayer{};
    const WipeFilter& wf = st.wipe_filter;
    if (wf.active) layer(wf.pattern, wf.fraction, wf.softness, Rgba{0, 0, 0, 255}, filter_mask_, filter, true, textures);
    else filter = WipeLayer{};
}

}  // namespace vnr::rt
