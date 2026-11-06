// SDL2 backend shared by all wrappers: draws the runtime's DrawList, uploads its textures, and maps
// SDL input (mouse, touch, keyboard, game controller) to runtime InputEvents.
#pragma once

#include <unordered_map>
#include <vector>

#include <SDL.h>

#include "vnr/rt/app.h"

namespace vnr::sdl {

class Renderer {
public:
    explicit Renderer(SDL_Renderer* r) : r_(r) {}
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void sync(rt::TextureStore& store);  // upload new/changed textures, free released ones
    void draw(const rt::DrawList& list);  // scales the 1344x626 stage to the output height, centred
    Vec2 to_stage(float window_x, float window_y) const;  // for pointer input

private:
    SDL_Renderer* r_;
    std::unordered_map<rt::TextureId, SDL_Texture*> textures_;
    std::unordered_map<rt::TextureId, Vec2> sizes_;
    float scale_ = 1, offset_x_ = 0, offset_y_ = 0;
    std::vector<SDL_Vertex> verts_;
    std::vector<int> indices_;
    void layout();
};

// Translates one SDL event; appends zero or more runtime events. Returns false on quit.
bool translate_event(const SDL_Event& e, const Renderer& renderer, std::vector<rt::InputEvent>& out);

// Opens the first game controller found (Switch Joy-Cons / Pro Controller, Xbox pads on desktop).
void open_controllers();

}  // namespace vnr::sdl
