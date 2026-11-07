// Turns the player's stage and text into a DrawList (stage units, 1344x626).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "vnr/assets.h"
#include "vnr/player.h"
#include "vnr/rt/asset_cache.h"
#include "vnr/rt/draw_list.h"
#include "vnr/rt/layer_mask.h"
#include "vnr/rt/text.h"
#include "vnr/rt/ui_skin.h"
#include "vnr/rt/effects.h"
#include "vnr/rt/title_card.h"
#include "vnr/rt/wipe_mask.h"

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
    bool auto_mode = false;  // AUTO button lit
    std::map<std::string, LayerMask> masks;  // subCameraFilter mask images, by name (decoded by the App)
    WipeLayer wipe, wipe_filter;             // this frame's wipe masks (WipeMasks, owned by the App)
    TextureId fx_atlas = 0;                  // effect sprites (make_effect_atlas)
    TitleTexts titles;                       // what bit_chapterstart / bit_queststart cards say
    float picture_frame_opacity = 1;         // pictureFrame fade-in (Fate Café: 0.2 s), kept by the App
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
    Rect text{227.5f, 463.6f, 866, 100};      // left edge from the game frame (4.5 left of the old 232)           // first baseline = text.y + text_px (492.3)
    Rect name_plate{165.5f, 393.4f, 283, 45};   // width is the minimum; longer names stretch the tab
    float name_text_x = 24.5f, name_baseline = 36.2f;  // inside the tab
    float text_px = 28.7f, line_height = 48, name_px = 29;  // FOT-Skip Std B em sizes (text widths match the video)
    int visible_lines = 2;
    float choice_width = 777.5f, choice_height = 71, choice_gap = 30.4f, choice_px = 28.5f;
    float choice_baseline = 49.2f;              // from the frame's top
    float choices_centre_y = 217.1f;            // centre of the choice stack while the text box shows
    Rect next_marker{1100, 497, 28, 56};        // bottom right of the box, shown while waiting for a tap
    float box_opacity = 0.82f;                  // text box, name tab, choices (game frame vs ours)
    // LOG above AUTO inside the box's right edge, both centred on x 1139.2 (video pcFfZvZqbmU 6:40:
    // LOG bar centre y 446.1, drawn 1.14x the sheet piece; AUTO box centre y 486.3, 1.10x).
    Rect log_button{1125.5f, 431.3f, 27.4f, 30.8f};
    Rect auto_on{1121.6f, 467.6f, 36.3f, 49.5f};
    Rect auto_off{1123.8f, 473.1f, 28.6f, 40.7f};  // its box centred where AUTO on's box is (x 1138.6)
    float button_opacity = 0.62f;               // LOG and AUTO off are see-through white; AUTO on is opaque
    Rect log_hit{1108, 425, 62, 40};            // tap areas: the top and bottom of the box's right edge
    Rect auto_hit{1108, 465, 62, 64};
};

enum class StoryButton { None, Log, Auto };

// Which story button (if any) a tap at `p` (stage units) hits. Buttons exist only while the text box shows.
StoryButton story_button_at(const UiLayout& ui, Vec2 p, bool text_box_visible);

std::vector<Rect> choice_rects(const ChoiceOp& choice, const UiLayout& ui, bool text_box_visible);

// Redraws `quads` in horizontal bands: `period`-tall stripes whose last quarter is drawn at `dim` alpha
// (the communication hologram's scanlines).
void add_scanlines(const std::vector<Quad>& quads, DrawList& out, float period = 4, float dim = 0.4f);

// cameraFilter gray and camera blur over the stage quads drawn from index `first` on.
// `sepia`: a backdrop effect (bit_sepia01…) is on.
void apply_camera_filter(const Camera& cam, const SceneAssets& assets, std::size_t first, DrawList& out, bool sepia = false);
// The named effects on one side of the characters (back: backEffect / charaBackEffect).
void draw_effects(const Stage& st, const SceneAssets& assets, bool back, DrawList& out);

// The pictureFrame overlay (part of render_scene).
void draw_picture_frame(const Stage& st, const SceneAssets& assets, DrawList& out);

void render_scene(const Player& player, const SceneAssets& assets, GlyphAtlas& atlas, const UiLayout& ui,
                  DrawList& out);

}  // namespace vnr::rt
