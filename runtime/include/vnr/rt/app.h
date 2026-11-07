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
#include "vnr/rt/catalogue.h"
#include "vnr/rt/draw_list.h"
#include "vnr/rt/focus_nav.h"
#include "vnr/rt/platform.h"
#include "vnr/rt/scene_render.h"
#include "vnr/rt/text.h"

namespace vnr::rt {

struct InputEvent {
    enum class Kind {
        Advance,      // Space / Enter / A button (closes the log; picks the focused button in the picker)
        Back,         // right click / Backspace / B button (closes the log; up one screen in the picker)
        Nav,          // arrow keys / d-pad: `index` is a NavDir. Reading: right = advance, left = back,
                      // up / down scroll the log. Picker: moves the focus.
        Pointer,      // press at `pos` (stage units): LOG / AUTO button, a choice, otherwise advances
        PointerMove,  // the pressed pointer moved to `pos` (drags the log)
        PointerUp,    // released at `pos` (a tap without a drag closes the log)
        Scroll,       // scroll the log / picker by `pos.y` stage units (+ newer / down): wheel, PgUp/PgDn, stick
        Choose,       // pick choice `index` directly (keyboard 1-9, d-pad + A)
        ToggleAuto,   // `A` key / Y button
        OpenLog,      // `L` key / X button
        Cancel,       // Esc / Start: closes the log; leaves the part for the picker (quits without one)
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
// Up / down while reading scroll the log by one line (as the arrow keys always did).
inline constexpr float kNavScrollStep = 47;

struct AppConfig {
    Region region = Region::NA;
    std::vector<std::string> script_ids;  // one reader "part": scripts played back to back
    Settings settings;
    // Start on the chapter picker instead of `script_ids` (the parts then come from the catalogue).
    bool picker = false;
    std::string picker_screen;  // optional first screen: "home", "chapter=<id>", "end=<id>:<section>"
};

class Picker;

class App {
public:
    // Picker: the chapter picker (only with AppConfig::picker). Finished: the part ended and there is
    // no picker to go back to.
    enum class Phase { Picker, LoadingScripts, LoadingAssets, Playing, Finished };

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
    // The chapter picker, when the App was started with one (null otherwise).
    const Picker* picker() const { return picker_.get(); }
    const AppConfig& config() const { return config_; }  // region and scripts of the current part
    // Plays a part now (the picker does this; tests too).
    void start_part(const PartChoice& part);

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
    std::unique_ptr<Picker> picker_;
    PartChoice part_;             // the part playing (picker mode)
    DrawList backdrop_;           // the part's last frame, under the end card

    Phase phase_ = Phase::LoadingScripts;
    std::vector<std::string> script_text_;
    std::vector<Program> programs_;
    std::size_t script_index_ = 0;
    std::unique_ptr<Player> player_;
    std::vector<int> figure_ids_;
    std::vector<std::string> mask_names_;  // subCameraFilter masks of the part
    std::size_t assets_total_ = 0;

    std::string base_figure_url(int figure_id) const;
    void leave_part(bool finished);
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
