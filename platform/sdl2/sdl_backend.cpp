#include "sdl_backend.h"

#include <algorithm>
#include <cstdlib>

namespace vnr::sdl {

using rt::InputEvent;
using rt::NavDir;

Renderer::~Renderer() {
    for (auto& [id, t] : textures_) SDL_DestroyTexture(t);
}

void Renderer::sync(rt::TextureStore& store) {
    for (rt::TextureId id : store.take_releases()) {
        const auto it = textures_.find(id);
        if (it != textures_.end()) {
            SDL_DestroyTexture(it->second);
            textures_.erase(it);
            sizes_.erase(id);
        }
    }
    for (rt::TextureId id : store.take_uploads()) {
        rt::Image* img = store.pixels(id);
        if (!img || img->width <= 0 || img->height <= 0) continue;
        SDL_Texture*& tex = textures_[id];
        if (tex) {
            int w = 0, h = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
            if (w != img->width || h != img->height) {
                SDL_DestroyTexture(tex);
                tex = nullptr;
            }
        }
        if (!tex) {
            // RGBA bytes in memory order = ABGR8888 on little-endian, which is every target we ship.
            tex = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, img->width, img->height);
            if (!tex) {
                SDL_Log("texture %u: %s", static_cast<unsigned>(id), SDL_GetError());
                textures_.erase(id);
                continue;
            }
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        }
        SDL_UpdateTexture(tex, nullptr, img->rgba.data(), img->width * 4);
        sizes_[id] = {static_cast<float>(img->width), static_cast<float>(img->height)};
        store.uploaded(id);
    }
}

void Renderer::layout() {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(r_, &w, &h);
    // Fit the game's visible band (576 tall) to the window height; if the window is narrower than
    // the safe area, fit the safe area instead. Wider windows show more of the stage sideways.
    scale_ = static_cast<float>(h) / rt::kViewHeight;
    if (static_cast<float>(w) < rt::kViewSafeWidth * scale_) scale_ = static_cast<float>(w) / rt::kViewSafeWidth;
    offset_x_ = (static_cast<float>(w) - kStageWidth * scale_) / 2;
    offset_y_ = (static_cast<float>(h) - rt::kViewHeight * scale_) / 2 - rt::kViewTop * scale_;
}

Vec2 Renderer::to_stage(float x, float y) const {
    return {(x - offset_x_) / scale_, (y - offset_y_) / scale_};
}

void Renderer::draw(const rt::DrawList& list) {
    layout();
    SDL_SetRenderDrawColor(r_, static_cast<Uint8>(list.clear.r * 255), static_cast<Uint8>(list.clear.g * 255),
                           static_cast<Uint8>(list.clear.b * 255), 255);
    SDL_RenderClear(r_);
    SDL_Rect clip{static_cast<int>(offset_x_), static_cast<int>(offset_y_ + rt::kViewTop * scale_ + 0.5f),
                  static_cast<int>(kStageWidth * scale_ + 0.5f), static_cast<int>(rt::kViewHeight * scale_ + 0.5f)};
    SDL_RenderSetClipRect(r_, &clip);

    // One batch per run of quads with the same texture and blend mode.
    auto flush = [&](SDL_Texture* tex, rt::Blend blend) {
        if (!indices_.empty()) {
            const SDL_BlendMode mode = blend == rt::Blend::Add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND;
            if (tex) SDL_SetTextureBlendMode(tex, mode);
            else SDL_SetRenderDrawBlendMode(r_, mode);
            SDL_RenderGeometry(r_, tex, verts_.data(), static_cast<int>(verts_.size()), indices_.data(),
                               static_cast<int>(indices_.size()));
        }
        verts_.clear();
        indices_.clear();
    };
    auto to_sdl = [](const rt::Color& c) {
        return SDL_Color{static_cast<Uint8>(std::clamp(c.r, 0.f, 1.f) * 255), static_cast<Uint8>(std::clamp(c.g, 0.f, 1.f) * 255),
                         static_cast<Uint8>(std::clamp(c.b, 0.f, 1.f) * 255), static_cast<Uint8>(std::clamp(c.a, 0.f, 1.f) * 255)};
    };
    rt::TextureId current = 0;
    rt::Blend current_blend = rt::Blend::Alpha;
    SDL_Texture* current_tex = nullptr;
    for (const rt::Quad& q : list.quads) {
        SDL_Texture* tex = nullptr;
        Vec2 size{1, 1};
        if (q.texture) {
            const auto it = textures_.find(q.texture);
            if (it == textures_.end()) continue;  // not uploaded (yet)
            tex = it->second;
            size = sizes_[q.texture];
        }
        if (q.texture != current || q.blend != current_blend) {
            flush(current_tex, current_blend);
            current = q.texture;
            current_tex = tex;
            current_blend = q.blend;
        }
        const float u0 = q.src.x / size.x, v0 = q.src.y / size.y;
        const float u1 = (q.src.x + q.src.w) / size.x, v1 = (q.src.y + q.src.h) / size.y;
        SDL_FPoint uv[4] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        if (q.has_uv)
            for (int k = 0; k < 4; ++k) uv[k] = {q.uv[k].x / size.x, q.uv[k].y / size.y};
        const int base = static_cast<int>(verts_.size());
        for (int k = 0; k < 4; ++k) {
            SDL_Vertex v;
            v.position = {offset_x_ + q.pos[k].x * scale_, offset_y_ + q.pos[k].y * scale_};
            v.color = to_sdl(rt::corner_color(q, k));
            v.tex_coord = uv[k];
            verts_.push_back(v);
        }
        for (int i : {0, 1, 2, 0, 2, 3}) indices_.push_back(base + i);
    }
    flush(current_tex, current_blend);
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_RenderSetClipRect(r_, nullptr);
}

namespace {
std::vector<SDL_GameController*> g_pads;
constexpr float kWheelStep = 60;    // stage units per wheel notch
constexpr float kPageStep = 400;
constexpr float kStickSpeed = 900;  // stage units per second at full tilt
constexpr int kStickDeadzone = 8000;

void open_pad(int index) {
    if (SDL_GameController* c = SDL_GameControllerOpen(index)) g_pads.push_back(c);
}

InputEvent scroll(float dy) { return {InputEvent::Kind::Scroll, {0, dy}, 0}; }
InputEvent nav(NavDir d) { return {InputEvent::Kind::Nav, {}, static_cast<std::size_t>(d)}; }

Vec2 finger_pos(const SDL_TouchFingerEvent& f, const Renderer& renderer) {
    int w = 0, h = 0;
    if (SDL_Window* win = SDL_GetWindowFromID(f.windowID)) SDL_GetWindowSize(win, &w, &h);
    return renderer.to_stage(f.x * static_cast<float>(w), f.y * static_cast<float>(h));
}

Vec2 mouse_pos(int x, int y, const Renderer& renderer) {
    return renderer.to_stage(static_cast<float>(x), static_cast<float>(y));
}
}  // namespace

void open_controllers() {
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) open_pad(i);
}

void poll_held(double dt, std::vector<InputEvent>& out) {
    int best = 0;
    for (SDL_GameController* c : g_pads) {
        if (!SDL_GameControllerGetAttached(c)) continue;
        for (SDL_GameControllerAxis a : {SDL_CONTROLLER_AXIS_RIGHTY, SDL_CONTROLLER_AXIS_LEFTY}) {
            const int v = SDL_GameControllerGetAxis(c, a);
            if (std::abs(v) > std::abs(best)) best = v;
        }
    }
    if (std::abs(best) <= kStickDeadzone) return;
    const float tilt = static_cast<float>(best) / 32767.f;  // down = + = newer lines
    out.push_back(scroll(tilt * kStickSpeed * static_cast<float>(dt)));
}

bool translate_event(const SDL_Event& e, const Renderer& renderer, std::vector<InputEvent>& out) {
    switch (e.type) {
        case SDL_QUIT:
            return false;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.which == SDL_TOUCH_MOUSEID) break;  // handled as a finger event
            if (e.button.button == SDL_BUTTON_LEFT)
                out.push_back({InputEvent::Kind::Pointer, mouse_pos(e.button.x, e.button.y, renderer), 0});
            else if (e.button.button == SDL_BUTTON_RIGHT)
                out.push_back({InputEvent::Kind::Back, {}, 0});
            break;
        case SDL_MOUSEMOTION:
            if (e.motion.which == SDL_TOUCH_MOUSEID) break;
            if (e.motion.state & SDL_BUTTON_LMASK)
                out.push_back({InputEvent::Kind::PointerMove, mouse_pos(e.motion.x, e.motion.y, renderer), 0});
            else if (SDL_GetModState() & KMOD_SHIFT)  // Shift + hover: dictionary lookup
                out.push_back({InputEvent::Kind::LookupAt, mouse_pos(e.motion.x, e.motion.y, renderer), 0});
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.which == SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT) break;
            out.push_back({InputEvent::Kind::PointerUp, mouse_pos(e.button.x, e.button.y, renderer), 0});
            break;
        case SDL_MOUSEWHEEL: {
            const float y = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.preciseY : e.wheel.preciseY;
            if (y != 0) out.push_back(scroll(-y * kWheelStep));  // wheel up = older lines
            break;
        }
        case SDL_FINGERDOWN:
            out.push_back({InputEvent::Kind::Pointer, finger_pos(e.tfinger, renderer), 0});
            break;
        case SDL_FINGERMOTION:
            out.push_back({InputEvent::Kind::PointerMove, finger_pos(e.tfinger, renderer), 0});
            break;
        case SDL_FINGERUP:
            out.push_back({InputEvent::Kind::PointerUp, finger_pos(e.tfinger, renderer), 0});
            break;
        case SDL_KEYDOWN:
            switch (e.key.keysym.sym) {
                case SDLK_SPACE:
                case SDLK_RETURN:
                case SDLK_KP_ENTER: out.push_back({InputEvent::Kind::Advance, {}, 0}); break;
                case SDLK_BACKSPACE: out.push_back({InputEvent::Kind::Back, {}, 0}); break;
                // Arrows: the App reads them as advance / back / log scroll while reading, focus moves in the picker.
                case SDLK_RIGHT: out.push_back(nav(NavDir::Right)); break;
                case SDLK_LEFT: out.push_back(nav(NavDir::Left)); break;
                case SDLK_UP: out.push_back(nav(NavDir::Up)); break;
                case SDLK_DOWN: out.push_back(nav(NavDir::Down)); break;
                case SDLK_PAGEUP: out.push_back(scroll(-kPageStep)); break;
                case SDLK_PAGEDOWN: out.push_back(scroll(kPageStep)); break;
                case SDLK_a: out.push_back({InputEvent::Kind::ToggleAuto, {}, 0}); break;
                case SDLK_l: out.push_back({InputEvent::Kind::OpenLog, {}, 0}); break;
                case SDLK_s: out.push_back({InputEvent::Kind::SkipScript, {}, 0}); break;
                case SDLK_ESCAPE: out.push_back({InputEvent::Kind::Cancel, {}, 0}); break;
                case SDLK_MINUS:
                case SDLK_KP_MINUS: out.push_back({InputEvent::Kind::VolumeDown, {}, 0}); break;
                case SDLK_EQUALS:
                case SDLK_PLUS:
                case SDLK_KP_PLUS: out.push_back({InputEvent::Kind::VolumeUp, {}, 0}); break;
                case SDLK_m: out.push_back({InputEvent::Kind::Mute, {}, 0}); break;
                case SDLK_F2: out.push_back({InputEvent::Kind::OpenSave, {}, 0}); break;
                case SDLK_F3: out.push_back({InputEvent::Kind::OpenLoad, {}, 0}); break;
                case SDLK_F5: out.push_back({InputEvent::Kind::QuickSave, {}, 0}); break;
                case SDLK_F9: out.push_back({InputEvent::Kind::QuickLoad, {}, 0}); break;
                case SDLK_DELETE: out.push_back({InputEvent::Kind::DeleteSave, {}, 0}); break;
                case SDLK_LSHIFT:
                case SDLK_RSHIFT:  // pressing Shift looks up the word already under the mouse
                    if (!e.key.repeat) {
                        int x = 0, y = 0;
                        SDL_GetMouseState(&x, &y);
                        out.push_back({InputEvent::Kind::LookupAt, mouse_pos(x, y, renderer), 0});
                    }
                    break;
                default:
                    if (e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_9)
                        out.push_back({InputEvent::Kind::Choose, {}, static_cast<std::size_t>(e.key.keysym.sym - SDLK_1)});
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A: out.push_back({InputEvent::Kind::Advance, {}, 0}); break;
                case SDL_CONTROLLER_BUTTON_B: out.push_back({InputEvent::Kind::Back, {}, 0}); break;
                case SDL_CONTROLLER_BUTTON_Y: out.push_back({InputEvent::Kind::Lookup, {}, 0}); break;
                case SDL_CONTROLLER_BUTTON_BACK: out.push_back({InputEvent::Kind::ToggleAuto, {}, 0}); break;
                case SDL_CONTROLLER_BUTTON_X: out.push_back({InputEvent::Kind::OpenLog, {}, 0}); break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: out.push_back({InputEvent::Kind::OpenSave, {}, 0}); break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: out.push_back({InputEvent::Kind::OpenLoad, {}, 0}); break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP: out.push_back(nav(NavDir::Up)); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: out.push_back(nav(NavDir::Down)); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: out.push_back(nav(NavDir::Left)); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: out.push_back(nav(NavDir::Right)); break;
                // Start: back to the chapter list while reading; quits from the picker's home (or without a picker).
                case SDL_CONTROLLER_BUTTON_START: out.push_back({InputEvent::Kind::Cancel, {}, 0}); break;
                default: break;
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
            open_pad(e.cdevice.which);
            break;
        default:
            break;
    }
    return true;
}

AudioDevice::AudioDevice(rt::AudioDirector& audio) : audio_(audio) {
    SDL_AudioSpec want{}, have{};
    want.freq = 48000;  // Switch native; SDL converts on devices that want something else
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &AudioDevice::callback;
    want.userdata = this;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (dev_ == 0) return;
    rate_ = have.freq;
    audio_.set_output_rate(rate_);
    SDL_PauseAudioDevice(dev_, 0);
}

AudioDevice::~AudioDevice() {
    if (dev_ == 0) return;
    audio_.stop_all(0.05);  // no click on quit
    SDL_Delay(80);
    SDL_CloseAudioDevice(dev_);  // waits for the callback to return
}

void SDLCALL AudioDevice::callback(void* user, Uint8* stream, int len) {
    auto* self = static_cast<AudioDevice*>(user);
    self->audio_.render(reinterpret_cast<float*>(stream), static_cast<std::size_t>(len) / (2 * sizeof(float)));
}

}  // namespace vnr::sdl
