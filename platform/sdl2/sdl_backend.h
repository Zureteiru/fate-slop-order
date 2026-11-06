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
    void draw(const rt::DrawList& list);  // fits the game's visible band (rt::kView*) to the output, centred
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

// Held input that has no event of its own: the sticks scroll the log. Call once per frame.
void poll_held(double dt, std::vector<rt::InputEvent>& out);

// The SDL audio device. It only pulls mixed samples from the runtime: SDL's callback thread calls
// AudioDirector::render() (which locks the mixer); no other threads, no audio logic here.
class AudioDevice {
public:
    explicit AudioDevice(rt::AudioDirector& audio);  // needs SDL_INIT_AUDIO; ok() is false without a device
    ~AudioDevice();                                  // fades out briefly, then closes the device
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    bool ok() const { return dev_ != 0; }
    int rate() const { return rate_; }

private:
    rt::AudioDirector& audio_;
    SDL_AudioDeviceID dev_ = 0;
    int rate_ = 0;
    static void SDLCALL callback(void* user, Uint8* stream, int len);
};

// Opens the first game controller found (Switch Joy-Cons / Pro Controller, Xbox pads on desktop).
void open_controllers();

}  // namespace vnr::sdl
