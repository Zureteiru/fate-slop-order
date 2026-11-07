// Atlas Academy asset locations and figure geometry (faces, slots). Pure functions.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vnr/command.h"

namespace vnr {

constexpr int kStageWidth = 1344;   // logical stage (game widescreen)
constexpr int kStageHeight = 626;
constexpr int kSafeWidth = 1024;    // classic 4:3-ish area centred in the stage
constexpr int kFacePage = 1024;     // face sheet page size
constexpr int kDefaultFaceSize = 256;

enum class Region { NA, JP };
std::string_view region_name(Region r);

constexpr const char* kAtlasStatic = "https://static.atlasacademy.io";
constexpr const char* kAtlasApi = "https://api.atlasacademy.io";

// A URL to try first, and one to try if it fails (empty when there is none).
struct AssetUrl {
    std::string primary;
    std::string fallback;
};

std::string script_folder(std::string_view script_id);  // "01", "94/9400", "Common"
std::string script_url(std::string_view script_id, Region r);
std::string background_url(std::string_view scene_id, Region r);  // _1344_626 variant for id >= 101900
std::string figure_url(int figure_id, Region r);                   // CharaFigure/<id>/<id>_merged.png
std::string image_url(std::string_view name, Region r);            // Image/<name>/<name>.png
AssetUrl bgm_url(std::string_view name, Region r);
std::string se_folder(std::string_view name);                      // Battle / ResidentSE / SE_<n> / SE
std::string se_url(std::string_view name, Region r, std::string_view folder = {});
// "A_B" -> Servants_A/B and ChrVoice_A/B (Servants first when the last part starts with B);
// "NP_<svt>_<n>" -> NoblePhantasm_<svt>/NP_<svt>_<n>.
std::optional<AssetUrl> voice_url(std::string_view voice_id, Region r);
std::string tvoice_url(std::string_view folder, std::string_view file, Region r);
AssetUrl movie_url(std::string_view movie_id, Region r);  // fallback = the other region
std::string svt_script_url(const std::vector<int>& figure_ids, Region r);

// svtScript extendData fields that matter for drawing a figure.
struct FigureMeta {
    int face_x = 0, face_y = 0;
    int offset_x = 0, offset_y = 0;
    int face_w = kDefaultFaceSize, face_h = kDefaultFaceSize;  // faceSizeRect, or faceSize x faceSize
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
};

// Where a figure's body sits on the stage, before slot/position offsets (top-left, y down).
Vec2 body_origin(const FigureMeta& meta, int image_width);

// Face overlay: the source rectangle on the sheet, and where to draw it relative to body_origin.
struct FaceCrop {
    Rect src;
    Vec2 dst;
};
// face 0 = no overlay. Same maths as Atlas DB Scene.tsx.
std::optional<FaceCrop> face_crop(const FigureMeta& meta, int face, int image_height);

// x offsets of the numbered slots used by charaFadein/charaPut (y is 0). Unknown slots give (0,0).
Vec2 slot_offset(int slot);
// A Position as a stage offset from the centred figure (y positive upwards).
Vec2 position_offset(const Position& p);

}  // namespace vnr
