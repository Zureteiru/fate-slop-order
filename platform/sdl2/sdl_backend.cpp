#include "sdl_backend.h"

#include <algorithm>

namespace vnr::sdl {

using rt::InputEvent;

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

    auto flush = [&](SDL_Texture* tex) {
        if (!indices_.empty())
            SDL_RenderGeometry(r_, tex, verts_.data(), static_cast<int>(verts_.size()), indices_.data(),
                               static_cast<int>(indices_.size()));
        verts_.clear();
        indices_.clear();
    };
    rt::TextureId current = 0;
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
        if (q.texture != current) {
            flush(current_tex);
            current = q.texture;
            current_tex = tex;
        }
        const SDL_Color c{static_cast<Uint8>(std::clamp(q.color.r, 0.f, 1.f) * 255),
                          static_cast<Uint8>(std::clamp(q.color.g, 0.f, 1.f) * 255),
                          static_cast<Uint8>(std::clamp(q.color.b, 0.f, 1.f) * 255),
                          static_cast<Uint8>(std::clamp(q.color.a, 0.f, 1.f) * 255)};
        const float u0 = q.src.x / size.x, v0 = q.src.y / size.y;
        const float u1 = (q.src.x + q.src.w) / size.x, v1 = (q.src.y + q.src.h) / size.y;
        const SDL_FPoint uv[4] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        const int base = static_cast<int>(verts_.size());
        for (int k = 0; k < 4; ++k) {
            SDL_Vertex v;
            v.position = {offset_x_ + q.pos[k].x * scale_, offset_y_ + q.pos[k].y * scale_};
            v.color = c;
            v.tex_coord = uv[k];
            verts_.push_back(v);
        }
        for (int i : {0, 1, 2, 0, 2, 3}) indices_.push_back(base + i);
    }
    flush(current_tex);
    SDL_RenderSetClipRect(r_, nullptr);
}

void open_controllers() {
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);
}

bool translate_event(const SDL_Event& e, const Renderer& renderer, std::vector<InputEvent>& out) {
    switch (e.type) {
        case SDL_QUIT:
            return false;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.which == SDL_TOUCH_MOUSEID) break;  // handled as a finger event
            if (e.button.button == SDL_BUTTON_LEFT)
                out.push_back({InputEvent::Kind::Pointer, renderer.to_stage(static_cast<float>(e.button.x), static_cast<float>(e.button.y)), 0});
            else if (e.button.button == SDL_BUTTON_RIGHT)
                out.push_back({InputEvent::Kind::Back, {}, 0});
            break;
        case SDL_FINGERDOWN: {
            int w = 0, h = 0;
            SDL_Window* win = SDL_GetWindowFromID(e.tfinger.windowID);
            if (win) SDL_GetWindowSize(win, &w, &h);
            out.push_back({InputEvent::Kind::Pointer, renderer.to_stage(e.tfinger.x * static_cast<float>(w), e.tfinger.y * static_cast<float>(h)), 0});
            break;
        }
        case SDL_KEYDOWN:
            switch (e.key.keysym.sym) {
                case SDLK_SPACE:
                case SDLK_RETURN:
                case SDLK_RIGHT:
                case SDLK_KP_ENTER: out.push_back({InputEvent::Kind::Advance, {}, 0}); break;
                case SDLK_BACKSPACE:
                case SDLK_LEFT: out.push_back({InputEvent::Kind::Back, {}, 0}); break;
                case SDLK_a: out.push_back({InputEvent::Kind::ToggleAuto, {}, 0}); break;
                case SDLK_s: out.push_back({InputEvent::Kind::SkipScript, {}, 0}); break;
                case SDLK_ESCAPE: return false;
                default:
                    if (e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_9)
                        out.push_back({InputEvent::Kind::Choose, {}, static_cast<std::size_t>(e.key.keysym.sym - SDLK_1)});
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A: out.push_back({InputEvent::Kind::Advance, {}, 0}); break;
                case SDL_CONTROLLER_BUTTON_B: out.push_back({InputEvent::Kind::Back, {}, 0}); break;
                case SDL_CONTROLLER_BUTTON_Y: out.push_back({InputEvent::Kind::ToggleAuto, {}, 0}); break;
                case SDL_CONTROLLER_BUTTON_START: return false;
                default: break;
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
            SDL_GameControllerOpen(e.cdevice.which);
            break;
        default:
            break;
    }
    return true;
}

}  // namespace vnr::sdl
