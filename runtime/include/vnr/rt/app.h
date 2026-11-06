// The shared application: every platform wrapper drives this with input and time, then draws
// the DrawList it returns and uploads the textures it lists.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vnr/player.h"
#include "vnr/rt/asset_cache.h"
#include "vnr/rt/draw_list.h"
#include "vnr/rt/platform.h"
#include "vnr/rt/scene_render.h"
#include "vnr/rt/text.h"

namespace vnr::rt {

struct InputEvent {
    enum class Kind {
        Advance,     // tap / click / Space / Enter / A button
        Back,        // right click / Backspace / B button
        Pointer,     // a click at `pos` (stage units): picks a choice, otherwise advances
        Choose,      // pick choice `index` directly (keyboard 1-9, d-pad + A)
        ToggleAuto,
        SkipScript,
    };
    Kind kind = Kind::Advance;
    Vec2 pos;
    std::size_t index = 0;
};

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

private:
    Platform& platform_;
    AppConfig config_;
    TextureStore textures_;
    AssetCache cache_;
    GlyphAtlas atlas_;
    UiLayout ui_;
    SceneAssets scene_;
    DrawList draw_;

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
    void render();
};

}  // namespace vnr::rt
