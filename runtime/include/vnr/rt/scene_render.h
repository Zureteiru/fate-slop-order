// Turns the player's stage and text into a DrawList (stage units, 1344x626).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "vnr/assets.h"
#include "vnr/player.h"
#include "vnr/rt/asset_cache.h"
#include "vnr/rt/draw_list.h"
#include "vnr/rt/text.h"
#include "vnr/rt/ui_skin.h"

namespace vnr::rt {

// What the renderer needs to know about one figure (svtScript row + body image size).
struct FigureInfo {
    FigureMeta meta;
    int body_width = 1024, body_height = 1024;
};

// Lookups the renderer uses; the App fills these as downloads finish.
struct SceneAssets {
    Region region = Region::NA;
    const AssetCache* cache = nullptr;
    std::map<int, FigureInfo> figures;
    UiSkin skin;       // the game's story UI art, when the user supplied the sheets
    double clock = 0;  // seconds since start, for UI animation (the "next" marker)
};

// Layout of the story UI (stage units), shared with input hit-testing. Matches the game: the text
// box is drawn at its native size, centred in the 1024-wide safe area and flush with the bottom of
// the screen (measured from NA gameplay video), two lines of text.
struct UiLayout {
    Rect panel{180, 475, 984, 151};
    Rect text{231, 499, 800, 110};
    Rect name_plate{180, 427, 430, 50};  // width is the minimum; longer names stretch the tab
    float name_text_x = 32, name_baseline = 35;  // inside the tab
    float text_px = 34, line_height = 53, name_px = 28;
    int visible_lines = 2;
    float choice_width = 752, choice_height = 82, choice_gap = 20, choice_px = 30;
    Rect next_marker{1094, 551, 32, 64};  // bottom right of the box, shown while waiting for a tap
    float box_opacity = 0.88f;
};

std::vector<Rect> choice_rects(const ChoiceOp& choice, const UiLayout& ui, bool text_box_visible);

void render_scene(const Player& player, const SceneAssets& assets, GlyphAtlas& atlas, const UiLayout& ui,
                  DrawList& out);

}  // namespace vnr::rt
