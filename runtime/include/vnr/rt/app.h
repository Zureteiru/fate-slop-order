// The shared application: every platform wrapper drives this with input and time, then draws
// the DrawList it returns and uploads the textures it lists.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "vnr/player.h"
#include "vnr/rt/asset_cache.h"
#include "vnr/rt/audio.h"
#include "vnr/rt/backlog.h"
#include "vnr/rt/catalogue.h"
#include "vnr/rt/draw_list.h"
#include "vnr/rt/focus_nav.h"
#include "vnr/rt/lookup.h"
#include "vnr/rt/wipe_mask.h"
#include "vnr/rt/platform.h"
#include "vnr/rt/save_screen.h"
#include "vnr/rt/saves.h"
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
        ToggleAuto,   // `A` key / - (Back) button
        OpenLog,      // `L` key / X button
        Cancel,       // Esc / Start: closes the log; leaves the part for the picker (quits without one)
        SkipScript,
        VolumeDown,   // `-` key: master volume -10%
        VolumeUp,     // `=` key: master volume +10%
        Mute,         // `M` key
        OpenSave,     // F2 / L shoulder / SAVE button: the Save screen (switches mode when it is open)
        OpenLoad,     // F3 / R shoulder / LOAD button: the Load screen (also from the picker)
        QuickSave,    // F5
        QuickLoad,    // F9
        DeleteSave,   // Delete: the focused save on the Save / Load screen (X also does it there)
        LookupAt,     // Shift held over `pos` (PC): look up the word under it in the dialogue or the log
        Lookup,       // Y button: opens the dictionary on the first word (closes it when open). While it is
                      // open, left / right hop between words and up / down pick an entry.
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
    // Plays a part now (the picker does this; tests too). With `resume`, starts at that save's line.
    void start_part(const PartChoice& part, std::optional<SaveEntry> resume = std::nullopt);

    // Saves (<user folder>/saves/fgo.json). `slot` is kAutosaveSlot, kQuickSlot or 0..kSaveSlots-1.
    const SaveFile& saves() const { return saves_; }
    const SaveScreen& save_screen() const { return save_screen_; }
    bool save_to(int slot);    // the line being read (false when nothing is being read yet)
    bool load_save(int slot);  // switches part when needed (false when the slot is empty or its part is gone)
    std::optional<SaveEntry> capture() const;  // the current stop as a save (nullopt before the first line)

    // Dictionary lookups (<user folder>/dict.bin, loaded on the first lookup).
    const LookupPopup& lookup() const { return lookup_; }
    const std::vector<GlyphBox>& story_boxes() const { return story_boxes_; }  // as last drawn
    // Opens the popup on the first place `text` (UTF-8) appears in the dialogue as last drawn (smoke runs).
    bool lookup_text(const std::string& text);
    // Autosaves are written at most this often (and always when leaving a part or quitting).
    static constexpr double kAutosaveInterval = 5;

private:
    Platform& platform_;
    AppConfig config_;
    TextureStore textures_;
    WipeMasks wipes_;
    std::string frame_name_;  // pictureFrame shown last frame, and when it appeared (scene clock)
    double frame_start_ = 0;
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

    SaveFile saves_;
    SaveScreen save_screen_;
    std::optional<SaveEntry> pending_resume_;  // applied when the part's first script starts
    std::size_t stop_script_ = SIZE_MAX, stop_pc_ = SIZE_MAX;  // the last stop seen (autosave tracking)
    bool autosave_dirty_ = false;
    double autosave_wait_ = 0;
    Dictionary dict_;
    bool dict_tried_ = false;
    LookupPopup lookup_;
    std::unique_ptr<GlyphAtlas> popup_atlas_;  // the OS UI font, made when the popup first opens
    std::vector<GlyphBox> story_boxes_;      // the dialogue's characters as last drawn
    std::vector<LookupTarget> log_targets_;  // the log's entries as last drawn (while it is open)
    bool swallow_pointer_ = false;           // the press that closed the popup: drop its move / release
    std::string toast_;  // "Saved" etc., shown briefly over the story
    double toast_left_ = 0;

    std::string base_figure_url(int figure_id) const;
    void leave_part(bool finished);
    void load_scripts();
    void request_assets();
    void load_wipes(const std::set<std::string>& patterns);
    void finish_assets();
    void start_script(std::size_t index);
    void handle(const InputEvent& e);
    void handle_log(const InputEvent& e);
    bool handle_lookup(const InputEvent& e);  // true when the event was used by the dictionary popup
    bool ensure_dict();
    LookupTarget story_target() const { return {0, story_boxes_}; }
    void handle_volume(const InputEvent& e);
    void play_audio_events();
    void choose(std::size_t index);
    void render();
    void track_autosave(double dt);
    void write_saves();
    void open_save_screen(SaveScreen::Mode mode);
    void handle_save_screen(const InputEvent& e);
    void toast(std::string text);
    std::optional<SaveEntry>& slot_ref(int slot);
};

}  // namespace vnr::rt
