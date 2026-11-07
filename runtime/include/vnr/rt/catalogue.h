// The chapter catalogue (Fate Café's reader.json) and the reader's progress through it.
// Pure data: parsing, labels, and turning a section into the scripts of one reader "part".
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "vnr/assets.h"

namespace vnr::rt {

// Fate Café's catalogue. The app downloads it at run time (never committed); a copy the user puts in
// their folder at kCatalogueUserKey wins and is never refreshed.
inline constexpr const char* kCatalogueUrl = "https://assets.fatecafe.com/data/atlas/reader.json";
inline constexpr const char* kCatalogueUserKey = "catalogue/reader.json";
// Which parts were read, the last one opened, and the NA/JP choice (written by the app, user folder).
inline constexpr const char* kProgressKey = "progress.json";

struct CatalogueScript {
    std::string id;
    Region region = Region::NA;          // where the script lives (`scriptRegion`)
    bool english = false;                // an NA copy exists (NA region, or `englishScriptUrl`)
    std::vector<std::string> skip_movies;
};

struct CatalogueSection {
    int quest_id = 0;
    std::string name;            // English when the catalogue has it (`sourceName`), else `name`
    int number = 0;              // `sectionNumber`; 0 = none (prologues, extras)
    std::string type;            // main / event / free / movie
    std::string spot;
    int duration = 0;            // seconds
    bool locked = false;
    bool video = false;          // a video-only section (not supported)
    std::vector<CatalogueScript> scripts;

    bool playable() const { return !locked && !video && !scripts.empty(); }
    std::string label() const;   // "Section 10 · Facing the Greater Grail", or just the name
};

struct CatalogueChapter {
    std::string id;              // "fuyuki"
    std::string arc_id;
    std::string label;           // "PROLOGUE" source: `sourceChapterLabel`
    std::string label_as_written;  // "Prologue": the same label in its own case (title cards)
    std::string name;            // "Singularity F"
    std::string long_name;       // "Flame Contaminated City: Fuyuki"
    std::string banner_url;
    int script_count = 0;
    int duration = 0;
    std::vector<CatalogueSection> sections;
};

struct CatalogueArc {
    std::string id;
    std::string name;            // "Arc 1"
    std::vector<std::size_t> chapters;  // indices into Catalogue::chapters, in reading order
};

struct Catalogue {
    std::vector<CatalogueArc> arcs;
    std::vector<CatalogueChapter> chapters;
    int data_version = 0;

    const CatalogueChapter* chapter(std::string_view id) const;
    std::optional<std::size_t> chapter_index(std::string_view id) const;
};

// Parses reader.json. Empty optional when it is not JSON or has no chapters. Chapters keep the
// file's `order`; arcs keep the file's order and list their chapters; chapters with an unknown arc
// get an arc of their own at the end.
std::optional<Catalogue> parse_catalogue(std::string_view json);

// "1h 44min", "19min", "11h" (rounded to the minute, as Fate Café shows it).
std::string format_duration(int seconds);
// "11 parts · 1h 44min", "1 part · 17min".
std::string chapter_meta(const CatalogueChapter& c);
// "Unknown Coordinates X-A · 2 scenes · 19min" (empty parts dropped).
std::string section_meta(const CatalogueSection& s);

// One reader "part" ready for the App.
struct PartChoice {
    std::string chapter_id;
    std::size_t section = 0;
    Region region = Region::NA;
    std::vector<std::string> script_ids;
    std::string label;           // for the end card
    // Title cards (Fate Café): chapter = catalogue label + long name; section = "Section N" / 第N節 + name.
    std::string chapter_label, chapter_title, section_label, section_title;
};

// The scripts of a section in the wanted language. NA falls back to JP when any script has no
// English copy (Fate Café disables EN there); JP always works.
std::optional<PartChoice> part_for(const Catalogue& cat, std::string_view chapter_id, std::size_t section, Region wanted);

// ---- progress (progress.json in the user folder)
struct Progress {
    Region region = Region::NA;
    std::string last_chapter;    // empty = nothing opened yet
    std::size_t last_section = 0;
    std::map<std::string, std::set<std::size_t>> read;  // chapter id -> section indices

    bool is_read(const std::string& chapter, std::size_t section) const;
    void mark_read(const std::string& chapter, std::size_t section);
    void opened(const std::string& chapter, std::size_t section);
};

Progress parse_progress(std::string_view json);  // bad or missing values keep the defaults
std::string serialize_progress(const Progress& p);

// The section "Start" opens: the first playable section not read yet, else the first playable one.
std::optional<std::size_t> next_unread(const CatalogueChapter& c, const Progress& p);

}  // namespace vnr::rt
