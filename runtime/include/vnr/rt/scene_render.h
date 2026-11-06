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
};

// Layout of the text box and choices (stage units), shared with input hit-testing.
struct UiLayout {
    Rect panel{322, 479, 700, 139};
    Rect text{395, 513, 554, 88};
    Rect name_plate{325, 443, 0, 38};
    float text_px = 20, line_height = 32.4f, name_px = 19;
    int visible_lines = 2;
    float choice_width = 620, choice_height = 64, choice_gap = 18;
};

std::vector<Rect> choice_rects(const ChoiceOp& choice, const UiLayout& ui, bool text_box_visible);

void render_scene(const Player& player, const SceneAssets& assets, GlyphAtlas& atlas, const UiLayout& ui,
                  DrawList& out);

}  // namespace vnr::rt
