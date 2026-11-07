// The chapter picker, after Fate Café's reader home (fatecafe.com/leitor): arcs with a grid of
// chapter cards, a chapter page with its parts, and the end-of-part card. It loads the catalogue
// (reader.json), remembers progress, and tells the App which part to play.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "vnr/rt/asset_cache.h"
#include "vnr/rt/catalogue.h"
#include "vnr/rt/draw_list.h"
#include "vnr/rt/focus_nav.h"
#include "vnr/rt/platform.h"
#include "vnr/rt/text.h"

namespace vnr::rt {

struct InputEvent;

// Layout of the picker in stage units (the game view is x 160-1184, y 0-576 at 16:9).
struct PickerLayout {
    float left = 192, width = 960;          // content column inside the 1024-wide safe area
    float view_top = 0, view_height = 576;
    int columns = 3;
    float card_gap = 20, card_row_gap = 20;
    float banner_aspect = 125.f / 450.f;    // Atlas questboard banners are 450x125
    float card_text_height = 84;
    float row_height = 62;
};

class Picker {
public:
    enum class Screen { Home, Chapter, End };
    struct Action {
        enum class Kind { None, Play, Quit } kind = Kind::None;
        PartChoice part;
    };

    Picker(Platform& platform, AssetCache& cache, GlyphAtlas& atlas);

    // `spec` opens a screen directly (smoke captures and tests): "home", "chapter=<id>" or
    // "end=<id>:<section index>". Unknown chapters fall back to the home screen.
    void open(const std::string& spec);

    void update(double dt);                 // catalogue download, banners
    Action handle(const InputEvent& e);
    // The App finished the part (the end card shows) or left it early (back to its chapter page).
    void part_finished(const PartChoice& part);
    void part_left(const PartChoice& part);

    // `backdrop` is the story's last frame, drawn dimmed under the end card (may be null).
    void draw(DrawList& out, const DrawList* backdrop);

    Screen screen() const { return screen_; }
    const Catalogue* catalogue() const { return cat_ ? &*cat_ : nullptr; }
    const Progress& progress() const { return progress_; }
    Region region() const { return progress_.region; }
    // The catalogue is in and nothing it shows is still downloading (smoke captures wait for this).
    bool ready() const;
    std::string status() const;
    const FocusNav& nav() const { return nav_; }
    std::string chapter_id() const;         // the chapter page / end card's chapter ("" on home)
    std::string focused_label() const;      // what the focused button says (tests, logs)
    // Screen rectangle (stage units) of button `i` in the current layout, for tests and hit-testing.
    Rect item_rect(std::size_t i) const;
    std::size_t item_count() const { return items_.size(); }

private:
    enum class Target { Region, Start, Chapter, BackLink, Section, Continue, BackToIndex, Retry };
    struct Item {
        Target target;
        std::size_t index = 0;   // chapter or section index
        Rect rect;               // content coordinates
        bool enabled = true;
        std::string label;
    };

    Platform& platform_;
    AssetCache& cache_;
    GlyphAtlas& atlas_;
    PickerLayout lay_;
    std::optional<Catalogue> cat_;
    bool from_user_ = false;      // the user's own reader.json: never refreshed
    bool refreshing_ = false;
    bool failed_ = false;         // no catalogue at all
    Progress progress_;
    Screen screen_ = Screen::Home;
    std::size_t chapter_ = 0;     // chapter page / end card
    std::size_t section_ = 0;     // end card: the part that just finished
    std::vector<Item> items_;
    float content_height_ = 0;
    FocusNav nav_;

    void load_catalogue();
    void refresh();
    bool adopt(const std::vector<std::uint8_t>& bytes);
    void load_progress();
    void save_progress();
    void go(Screen s, std::optional<std::size_t> focus_target = std::nullopt);
    void build();
    std::optional<std::size_t> find_item(Target t, std::size_t index) const;
    Action activate(std::size_t item);
    Action play(std::size_t chapter, std::size_t section);
    void back();
    std::optional<std::size_t> next_playable(std::size_t chapter, std::size_t after) const;

    void draw_home(DrawList& out);
    void draw_chapter(DrawList& out);
    void draw_end(DrawList& out, const DrawList* backdrop);
    void draw_banner(DrawList& out, const std::string& url, const Rect& screen);
    void draw_focus(DrawList& out, const Rect& screen);
    float text(DrawList& out, const std::string& s, float x, float baseline, float px, Color c, float max_w = 0,
               bool right = false);
    float width_of(const std::string& s, float px);
    Rect to_screen(const Rect& content) const;
    bool on_screen(const Rect& content) const;
};

}  // namespace vnr::rt
