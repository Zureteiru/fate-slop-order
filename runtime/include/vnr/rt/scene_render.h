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

// Layout of the story UI (stage units), shared with input hit-testing. Every number is measured
// from 1080p NA gameplay video (game view = stage x 160-1184, y 0-576, see kView* in draw_list.h).
// The game draws the sheet pieces at native width but squashed vertically (151 -> 139, 51 -> 45,
// 82 -> 71); the name tab is drawn over the box's top-left corner.
struct UiLayout {
    Rect panel{179.5f, 425, 984, 139};          // ends 12 above the bottom of the view
    // Text wraps at the game's width: from the text's left edge to just short of the LOG / AUTO
    // buttons (video: x 1093). Scripts break some lines themselves ([r]/[sr]); the rest the game
    // wraps, so this width, not per-line fixes, decides how every chapter's text fits.
    Rect text{232, 463.6f, 860, 100};           // first baseline = text.y + text_px (492.3)
    Rect name_plate{165.5f, 393.4f, 283, 45};   // width is the minimum; longer names stretch the tab
    float name_text_x = 24.5f, name_baseline = 36.2f;  // inside the tab
    float text_px = 28.7f, line_height = 48, name_px = 29;  // FOT-Skip Std B em sizes (text widths match the video)
    int visible_lines = 2;
    float choice_width = 777.5f, choice_height = 71, choice_gap = 30.4f, choice_px = 28.5f;
    float choice_baseline = 49.2f;              // from the frame's top
    float choices_centre_y = 217.1f;            // centre of the choice stack while the text box shows
    Rect next_marker{1100, 497, 28, 56};        // bottom right of the box, shown while waiting for a tap
    float box_opacity = 0.88f;
};

std::vector<Rect> choice_rects(const ChoiceOp& choice, const UiLayout& ui, bool text_box_visible);

void render_scene(const Player& player, const SceneAssets& assets, GlyphAtlas& atlas, const UiLayout& ui,
                  DrawList& out);

}  // namespace vnr::rt
