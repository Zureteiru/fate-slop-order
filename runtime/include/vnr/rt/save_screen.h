// The Save / Load screen: an overlay over the dimmed story (or the picker) with page tabs and a 2x5 grid of slot
// cards (thumbnail, chapter and part, the line, the date). Load also has an "Auto / Quick" page with the autosave
// and the quick save. Overwriting, deleting and loading from the story ask first. The game has no save screen,
// so this follows the LOG screen's look (dimmed stage, game font, white outlines); not measured.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "vnr/rt/asset_cache.h"
#include "vnr/rt/draw_list.h"
#include "vnr/rt/focus_nav.h"
#include "vnr/rt/saves.h"
#include "vnr/rt/text.h"

namespace vnr::rt {

struct InputEvent;

inline constexpr int kAutosaveSlot = -2;
inline constexpr int kQuickSlot = -1;

struct SaveScreenLayout {
    float dim = 0.85f;
    Rect title{180, 14, 300, 40};
    float tabs_y = 22, tab_h = 34, tab_gap = 10;
    float grid_x = 180, grid_y = 72, card_w = 486, card_h = 84, col_gap = 12, row_gap = 10;  // x 180-1164
    Rect thumb{8, 8, 147, 68};  // inside a card (about the 1344:626 stage)
    Rect dialog{372, 210, 600, 170};
};

class SaveScreen {
public:
    enum class Mode { Save, Load };
    struct Action {
        enum class Kind { None, Close, Save, Load, Delete } kind = Kind::None;
        int slot = 0;  // kAutosaveSlot, kQuickSlot or 0..kSaveSlots-1
    };

    // `confirm_load`: loading drops the reading in progress, so ask first (from the story, not the picker).
    void open(Mode mode, const SaveFile& saves, bool confirm_load);
    void close();
    bool is_open() const { return open_; }
    Mode mode() const { return mode_; }
    std::size_t page() const { return page_; }
    std::size_t page_count() const { return mode_ == Mode::Load ? 4 : 3; }  // Load: Auto/Quick, 1, 2, 3
    void set_page(std::size_t page, const SaveFile& saves);
    bool confirming() const { return confirm_.has_value(); }

    // Input while open; the App does what the Action says (and keeps the screen open after Save / Delete).
    Action handle(const InputEvent& e, const SaveFile& saves);
    // The saves changed (a save or a delete): refresh which cards are enabled.
    void refresh(const SaveFile& saves);

    void draw(const SaveFile& saves, AssetCache& cache, GlyphAtlas& atlas, DrawList& out) const;

    // Screen rectangles for tests and hit tests: tab `i`, and card `i` of the current page.
    Rect tab_rect(std::size_t i) const;
    Rect card_rect(std::size_t i) const;
    std::size_t card_count() const { return page_ == 0 && mode_ == Mode::Load ? 2 : kSlotsPerPage; }
    int slot_of_card(std::size_t i) const;
    const FocusNav& nav() const { return nav_; }

private:
    struct Confirm {
        Action action;
        std::string message;
        bool yes = false;  // focus: No by default
    };
    SaveScreenLayout lay_;
    Mode mode_ = Mode::Save;
    bool open_ = false;
    bool confirm_load_ = false;
    std::size_t page_ = 0;
    FocusNav nav_;
    std::optional<Confirm> confirm_;

    static const std::optional<SaveEntry>& entry(const SaveFile& saves, int slot);
    Action activate(std::size_t item, const SaveFile& saves);
    Action answer(bool yes);
    Rect yes_rect() const;
    Rect no_rect() const;
    std::string tab_label(std::size_t i) const;
};

}  // namespace vnr::rt
