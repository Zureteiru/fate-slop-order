// The shared application: every platform wrapper drives this with input and time, then draws
// the DrawList it returns and uploads the textures it lists.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vnr/player.h"
#include "vnr/rt/asset_cache.h"
#include "vnr/rt/audio.h"
#include "vnr/rt/backlog.h"
#include "vnr/rt/draw_list.h"
#include "vnr/rt/platform.h"
#include "vnr/rt/scene_render.h"
#include "vnr/rt/text.h"

namespace vnr::rt {

struct InputEvent {
    enum class Kind {
        Advance,      // Space / Enter / A button (closes the log)
        Back,         // right click / Backspace / B button (closes the log)
        Pointer,      // press at `pos` (stage units): LOG / AUTO button, a choice, otherwise advances
        PointerMove,  // the pressed pointer moved to `pos` (drags the log)
        PointerUp,    // released at `pos` (a tap without a drag closes the log)
        Scroll,       // scroll the log by `pos.y` stage units (+ newer, - older): wheel, d-pad, stick
        Choose,       // pick choice `index` directly (keyboard 1-9, d-pad + A)
        ToggleAuto,   // `A` key / Y button
        OpenLog,      // `L` key / X button
        Cancel,       // Esc: closes the log, otherwise asks to quit
        SkipScript,
        VolumeDown,   // `-` key: master volume -10%
        VolumeUp,     // `=` key: master volume +10%
        Mute,         // `M` key
    };
    Kind kind = Kind::Advance;
    Vec2 pos;
    std::size_t index = 0;
};

// The game's dialogue font (FOT-Skip Std B), if the user put it in their folder. Never committed.
inline constexpr const char* kGameFontKey = "fonts/FOT-SkipStd-B.otf";
// Optional user settings (volumes for now), in the user folder.
inline constexpr const char* kSettingsKey = "settings.json";
// The choice click (tools/install_choice_sound.sh), in the user folder. Never committed.
inline constexpr const char* kChoiceSoundKey = "sfx/choice.mp3";
inline constexpr float kChoiceSoundVolume = 0.5f;

struct AppConfig {
    Region region = Region::NA;
    std::vector<std::string> script_ids;  // one reader "part": scripts played back to back
    Settings settings;
};

class App {
public:
    enum class Phase { LoadingScripts, LoadingAssets, Playing, Finished };

    App(Platform& platform, AppConfig config);
    ~App();

    void update(double dt, const std::vector<InputEvent>& input);

    const DrawList& draw_list() const { return draw_; }
    TextureStore& textures() { return textures_; }
    Phase phase() const { return phase_; }
    std::string status() const;               // human-readable progress / position
    const Player* player() const { return player_.get(); }
    std::size_t script_index() const { return script_index_; }
    const UiLayout& ui() const { return ui_; }
    bool game_font() const { return font_.game; }
    const Backlog& backlog() const { return log_; }
    bool quit_requested() const { return quit_; }
    bool auto_mode() const { return scene_.auto_mode; }  // as of the last frame drawn
    // Sound: the platform's audio callback calls audio().render() (thread-safe).
    AudioDirector& audio() { return audio_; }

private:
    Platform& platform_;
    AppConfig config_;
    TextureStore textures_;
    AssetCache cache_;
    AudioDirector audio_;
    SharedBytes choice_sound_;  // null when not installed
    struct FontChoice {
        std::vector<std::uint8_t> bytes;  // freed once the atlas has its own copy
        int index = 0;
        bool game = false;
    };
    static FontChoice pick_font(Platform& p);
    FontChoice font_;  // must be declared before atlas_
    GlyphAtlas atlas_;
    UiLayout ui_;
    SceneAssets scene_;
    DrawList draw_;
    Backlog log_;
    bool quit_ = false;

    Phase phase_ = Phase::LoadingScripts;
    std::vector<std::string> script_text_;
    std::vector<Program> programs_;
    std::size_t script_index_ = 0;
    std::unique_ptr<Player> player_;
    std::vector<int> figure_ids_;
    std::size_t assets_total_ = 0;

    std::string base_figure_url(int figure_id) const;
    void load_scripts();
    void request_assets();
    void finish_assets();
    void start_script(std::size_t index);
    void handle(const InputEvent& e);
    void handle_log(const InputEvent& e);
    void handle_volume(const InputEvent& e);
    void play_audio_events();
    void choose(std::size_t index);
    void render();
};

}  // namespace vnr::rt
