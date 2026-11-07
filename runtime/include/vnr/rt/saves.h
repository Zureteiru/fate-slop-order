// Save slots and the autosave: where the reader is (part, script, stop) and the choices taken on the way.
// A save is restored by replaying its script up to the stop (vnr::Player::resume), not by storing the stage.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vnr/assets.h"
#include "vnr/program.h"

namespace vnr::rt {

inline constexpr std::size_t kSaveSlots = 30;  // 3 pages of 10
inline constexpr std::size_t kSlotsPerPage = 10;
inline constexpr const char* kSavesKey = "saves/fgo.json";  // one file per project (Arknights gets its own)
// Runs without the picker (--scripts, story smoke runs) keep their own saves, so they never replace the
// reader's autosave.
inline constexpr const char* kScriptSavesKey = "saves/scripts.json";

struct SaveEntry {
    std::string chapter_id;
    std::size_t section = 0;
    Region region = Region::NA;
    std::string chapter_label, part_label;  // shown on the slot card
    std::size_t script_index = 0;
    std::string script_id;
    std::uint32_t script_hash = 0;  // FNV-1a of the script text: detects an edited script
    std::size_t stop_pc = 0;        // a SayOp or ChoiceOp (see ResumePoint)
    std::vector<int> choices;       // options taken in that script before the stop
    int line = 0;                   // script line of the stop (fallback when the hash differs)
    std::string speaker, text;      // preview (plain text, cut to ~60 characters)
    std::string background;         // scene id for the thumbnail
    bool auto_mode = false;
    std::int64_t saved_at = 0;  // unix seconds
};

struct SaveFile {
    std::optional<SaveEntry> autosave;
    std::optional<SaveEntry> quick;
    std::array<std::optional<SaveEntry>, kSaveSlots> slots;
};

SaveFile parse_saves(std::string_view json);  // bad entries are dropped; a bad file gives an empty SaveFile
std::string serialize_saves(const SaveFile& f);

std::uint32_t fnv1a(std::string_view text);
std::string preview_text(std::string_view plain, std::size_t max_chars = 60);  // UTF-8 aware, adds "…"
std::string format_save_time(std::int64_t unix_seconds);                      // "2026-11-07 14:05", local time

// Where to resume in `prog` (the script as downloaded now). With a matching hash the saved stop is used as is;
// otherwise the first dialogue at the saved line. nullopt = play the script from its top.
struct ResumePoint {
    std::size_t stop_pc = 0;
    std::vector<int> choices;
};
std::optional<ResumePoint> resume_point(const SaveEntry& e, const Program& prog, std::uint32_t script_hash);

}  // namespace vnr::rt
