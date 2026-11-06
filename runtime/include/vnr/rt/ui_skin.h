// The game's own story-screen art, cut from user-supplied UI sheets (never committed or downloaded:
// see apps/README.md). Without the sheets the renderer falls back to plain boxes of the same layout.
#pragma once

#include "vnr/assets.h"
#include "vnr/rt/draw_list.h"
#include "vnr/rt/platform.h"

namespace vnr::rt {

// Pieces on the sheets (texture pixels), measured from the FGO "User Interface" rip.
namespace skin {
constexpr int kSheetTalkSize = 2048;              // ui/003.png
constexpr int kSheetNextSize = 256;               // ui/030.png
constexpr Rect kTextBox{21, 6, 984, 151};         // navy box, diamond pattern bottom right
constexpr Rect kChoice{1138, 6, 752, 82};         // checkered frame, light blue edge, cut corners
constexpr Rect kNameTab{540, 339, 283, 51};       // blue tab, pointed left end, fades out right
constexpr float kNameTabLeft = 24, kNameTabRight = 56;  // fixed ends when the tab is stretched
constexpr float kChoiceEdge = 40;                 // fixed ends when the choice frame is stretched
constexpr Rect kLogButton{2020, 504, 24, 27};     // up arrow, bar, "LOG" (bottom right of the sheet)
constexpr Rect kAutoOff{1951, 613, 25, 37};       // outlined play box + "AUTO"
constexpr Rect kAutoOn{2012, 644, 33, 45};        // the same box glowing cyan
constexpr int kNextFrames = 8;                    // double diamond spinning: 4 x 2 frames of 64 x 128
constexpr float kNextFrameW = 64, kNextFrameH = 128;
constexpr float kNextFps = 12;
inline Rect next_frame(int i) {
    return {static_cast<float>(i % 4) * kNextFrameW, static_cast<float>(i / 4) * kNextFrameH, kNextFrameW, kNextFrameH};
}
}  // namespace skin

struct UiSkin {
    TextureId talk = 0;  // sheet 003: text box, choice frame, name tab
    TextureId next = 0;  // sheet 030: "next line" marker frames
    bool has_talk() const { return talk != 0; }
    bool has_next() const { return next != 0; }

    // Takes decoded sheets; a sheet of the wrong size is rejected (returns false for it).
    bool adopt_talk(Image sheet, TextureStore& textures);
    bool adopt_next(Image sheet, TextureStore& textures);
};

// Reads "ui/003.png" and "ui/030.png" from the user's files. Missing sheets are fine (fallback);
// unreadable or wrong-size ones are logged and skipped.
UiSkin load_ui_skin(Files& files, TextureStore& textures, Log& log);

}  // namespace vnr::rt
