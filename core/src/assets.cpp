#include "vnr/assets.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace vnr {

std::string_view region_name(Region r) { return r == Region::JP ? "JP" : "NA"; }

namespace {
std::string base(Region r) { return std::string(kAtlasStatic) + "/" + std::string(region_name(r)) + "/"; }
}  // namespace

std::string script_folder(std::string_view id) {
    if (id == "WarEpilogue108") return "01";
    if (!id.empty() && (id[0] == '0' || id[0] == '9')) {
        if (id.substr(0, 2) == "94") return "94/" + std::string(id.substr(0, 4));
        return std::string(id.substr(0, 2));
    }
    return "Common";
}

std::string script_url(std::string_view id, Region r) {
    return base(r) + "Script/" + script_folder(id) + "/" + std::string(id) + ".txt";
}

std::string background_url(std::string_view scene_id, Region r) {
    const long n = std::strtol(std::string(scene_id).c_str(), nullptr, 10);
    const std::string suffix = n >= 101900 ? "_1344_626" : "";
    return base(r) + "Back/back" + std::string(scene_id) + suffix + ".png";
}

std::string figure_url(int id, Region r) {
    const std::string s = std::to_string(id);
    return base(r) + "CharaFigure/" + s + "/" + s + "_merged.png";
}

std::string image_url(std::string_view name, Region r) {
    const std::string n(name);
    return base(r) + "Image/" + n + "/" + n + ".png";
}

AssetUrl bgm_url(std::string_view name, Region r) {
    const std::string n(name);
    return {base(r) + "Audio/Bgm/" + n + "/" + n + ".mp3", base(r) + "Audio/" + n + "/" + n + ".mp3"};
}

std::string se_folder(std::string_view name) {
    if (name.substr(0, 2) == "ba") return "Battle";
    if (name.substr(0, 2) == "ar") return "ResidentSE";
    std::size_t i = 0;
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
    if (i > 0 && i < name.size() && name[i] == '_') return "SE_" + std::string(name.substr(0, i));
    return "SE";
}

std::string se_url(std::string_view name, Region r, std::string_view folder) {
    const std::string f = folder.empty() ? se_folder(name) : std::string(folder);
    return base(r) + "Audio/" + f + "/" + std::string(name) + ".mp3";
}

std::optional<AssetUrl> voice_url(std::string_view voice_id, Region r) {
    const std::size_t us = voice_id.find('_');
    if (us == std::string_view::npos || us == 0 || us + 1 >= voice_id.size()) return std::nullopt;
    const std::string a(voice_id.substr(0, us)), b(voice_id.substr(us + 1));
    if (a == "NP") {  // Noble Phantasm shout: NP_<svt>_<n> -> NoblePhantasm_<svt>/NP_<svt>_<n>
        const std::size_t us2 = b.find('_');
        if (us2 == std::string::npos || us2 == 0) return std::nullopt;
        return AssetUrl{base(r) + "Audio/NoblePhantasm_" + b.substr(0, us2) + "/" + std::string(voice_id) + ".mp3", {}};
    }
    // Atlas DB's rule: battle lines (last part "B...") live in Servants_, the rest in ChrVoice_.
    const std::string last = b.substr(b.rfind('_') == std::string::npos ? 0 : b.rfind('_') + 1);
    const std::string servants = base(r) + "Audio/Servants_" + a + "/" + b + ".mp3";
    const std::string chr = base(r) + "Audio/ChrVoice_" + a + "/" + b + ".mp3";
    if (!last.empty() && last[0] == 'B') return AssetUrl{servants, chr};
    return AssetUrl{chr, servants};
}

std::string tvoice_url(std::string_view folder, std::string_view file, Region r) {
    return base(r) + "Audio/" + std::string(folder) + "/" + std::string(file) + ".mp3";
}

AssetUrl movie_url(std::string_view movie_id, Region r) {
    std::string id(movie_id);
    const std::size_t slash = id.rfind('/');
    if (slash != std::string::npos) id = id.substr(slash + 1);
    if (id.size() < 4 || id.compare(id.size() - 4, 4, ".mp4") != 0) id += ".mp4";
    const Region other = r == Region::JP ? Region::NA : Region::JP;
    return {base(r) + "Movie/" + id, base(other) + "Movie/" + id};
}

std::string svt_script_url(const std::vector<int>& ids, Region r) {
    std::string url = std::string(kAtlasApi) + "/raw/" + std::string(region_name(r)) + "/svtScript";
    char sep = '?';
    for (int id : ids) {
        url += sep;
        url += "charaId=" + std::to_string(id);
        sep = '&';
    }
    return url;
}

Vec2 body_origin(const FigureMeta& meta, int image_width) {
    return {static_cast<float>((kStageWidth - image_width) / 2.0 + meta.offset_x), static_cast<float>(-meta.offset_y)};
}

std::optional<FaceCrop> face_crop(const FigureMeta& meta, int face, int image_height) {
    if (face <= 0 || meta.face_w <= 0 || meta.face_h <= 0) return std::nullopt;
    const int w = meta.face_w, h = meta.face_h;
    const int per_row = std::max(1, kFacePage / w);
    const int k = face - 1;
    const int col = k % per_row;
    const int row = k / per_row;
    const int page = row / per_row;
    const int row_in_page = row % per_row;
    float sy;
    if (h == kDefaultFaceSize && image_height == kFacePage) {
        sy = static_cast<float>(kFacePage - kDefaultFaceSize + kDefaultFaceSize * row);
    } else {
        sy = static_cast<float>(image_height + kFacePage * page + row_in_page * h);
    }
    FaceCrop c;
    c.src = {static_cast<float>(col * w), sy, static_cast<float>(w), static_cast<float>(h)};
    c.dst = {static_cast<float>(meta.face_x), static_cast<float>(meta.face_y)};
    return c;
}

Vec2 slot_offset(int slot) {
    static const float kSlots[] = {-256, 0, 256, -438, -512, 438, 512};
    if (slot < 0 || slot >= static_cast<int>(sizeof(kSlots) / sizeof(kSlots[0]))) return {0, 0};
    return {kSlots[slot], 0};
}

Vec2 position_offset(const Position& p) { return p.is_slot ? slot_offset(p.slot) : p.offset; }

}  // namespace vnr
