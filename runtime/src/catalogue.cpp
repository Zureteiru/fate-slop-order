#include "vnr/rt/catalogue.h"

#include <algorithm>
#include <cmath>

#include "vnr/rt/json.h"

namespace vnr::rt {

namespace {

const std::string& pick(const Json& obj, const char* english, const char* fallback) {
    const std::string& e = obj[english].string();
    return e.empty() ? obj[fallback].string() : e;
}

Region region_of(const std::string& s) { return s == "JP" ? Region::JP : Region::NA; }

std::string upper_ascii(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

void escape_into(std::string& out, const std::string& s) {
    out += '"';
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    out += '"';
}

}  // namespace

std::string CatalogueSection::label() const {
    if (number <= 0) return name;
    return "Section " + std::to_string(number) + " \xC2\xB7 " + name;
}

const CatalogueChapter* Catalogue::chapter(std::string_view id) const {
    const auto i = chapter_index(id);
    return i ? &chapters[*i] : nullptr;
}

std::optional<std::size_t> Catalogue::chapter_index(std::string_view id) const {
    for (std::size_t i = 0; i < chapters.size(); ++i)
        if (chapters[i].id == id) return i;
    return std::nullopt;
}

std::optional<Catalogue> parse_catalogue(std::string_view text) {
    const auto json = Json::parse(text);
    if (!json || !json->is_object()) return std::nullopt;
    Catalogue cat;
    cat.data_version = (*json)["dataVersion"].integer();

    struct Ordered {
        int order;
        std::size_t file_index;
        CatalogueChapter ch;
    };
    std::vector<Ordered> list;
    const auto& rows = (*json)["chapters"].items();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Json& c = rows[i];
        CatalogueChapter ch;
        ch.id = c["id"].string();
        if (ch.id.empty()) continue;
        ch.arc_id = c["arcId"].string();
        ch.label = upper_ascii(pick(c, "sourceChapterLabel", "chapterLabel"));
        ch.name = pick(c, "sourceName", "name");
        ch.long_name = pick(c, "sourceLongName", "longName");
        ch.banner_url = c["bannerUrl"].string();
        ch.duration = c["durationSeconds"].integer();
        for (const Json& s : c["sections"].items()) {
            CatalogueSection sec;
            sec.quest_id = s["questId"].integer();
            sec.name = pick(s, "sourceName", "name");
            sec.number = s["sectionNumber"].integer();
            sec.type = s["type"].string();
            sec.spot = s["spotName"].string();
            sec.duration = s["durationSeconds"].integer();
            sec.locked = s["locked"].boolean();
            sec.video = s["video"].is_object();
            for (const Json& sc : s["scripts"].items()) {
                CatalogueScript script;
                script.id = sc["scriptId"].string();
                if (script.id.empty()) continue;
                script.region = region_of(sc["scriptRegion"].string());
                script.english = script.region == Region::NA || sc["englishScriptRegion"].string() == "NA";
                for (const Json& m : sc["skipMovieIds"].items())
                    if (!m.string().empty()) script.skip_movies.push_back(m.string());
                sec.scripts.push_back(std::move(script));
            }
            ch.sections.push_back(std::move(sec));
        }
        ch.script_count = c["scriptCount"].integer(-1);
        if (ch.script_count < 0) {
            ch.script_count = 0;
            for (const auto& s : ch.sections) ch.script_count += static_cast<int>(s.scripts.size());
        }
        list.push_back({c["order"].integer(static_cast<int>(i)), i, std::move(ch)});
    }
    if (list.empty()) return std::nullopt;
    std::stable_sort(list.begin(), list.end(), [](const Ordered& a, const Ordered& b) { return a.order < b.order; });
    for (auto& o : list) cat.chapters.push_back(std::move(o.ch));

    for (const Json& a : (*json)["arcs"].items()) {
        CatalogueArc arc;
        arc.id = a["id"].string();
        arc.name = pick(a, "sourceName", "name");
        if (!arc.id.empty()) cat.arcs.push_back(std::move(arc));
    }
    for (std::size_t i = 0; i < cat.chapters.size(); ++i) {
        const std::string& want = cat.chapters[i].arc_id;
        auto it = std::find_if(cat.arcs.begin(), cat.arcs.end(), [&](const CatalogueArc& a) { return a.id == want; });
        if (it == cat.arcs.end()) {
            CatalogueArc extra;
            extra.id = want;
            extra.name = want.empty() ? "Other" : want;
            cat.arcs.push_back(std::move(extra));
            it = cat.arcs.end() - 1;
        }
        it->chapters.push_back(i);
    }
    cat.arcs.erase(std::remove_if(cat.arcs.begin(), cat.arcs.end(), [](const CatalogueArc& a) { return a.chapters.empty(); }),
                   cat.arcs.end());
    return cat;
}

std::string format_duration(int seconds) {
    const int minutes = static_cast<int>(std::lround(std::max(0, seconds) / 60.0));
    const int h = minutes / 60, m = minutes % 60;
    if (h == 0) return std::to_string(m) + "min";
    if (m == 0) return std::to_string(h) + "h";
    return std::to_string(h) + "h " + std::to_string(m) + "min";
}

std::string chapter_meta(const CatalogueChapter& c) {
    std::string s = plural(static_cast<int>(c.sections.size()), "part", "parts");
    if (c.duration > 0) s += " \xC2\xB7 " + format_duration(c.duration);
    return s;
}

std::string section_meta(const CatalogueSection& s) {
    std::vector<std::string> parts;
    if (!s.spot.empty()) parts.push_back(s.spot);
    if (s.locked) parts.push_back("locked");
    else if (s.video) parts.push_back("video (not supported)");
    else if (!s.scripts.empty()) parts.push_back(plural(static_cast<int>(s.scripts.size()), "scene", "scenes"));
    if (s.duration > 0) parts.push_back(format_duration(s.duration));
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : " \xC2\xB7 ") + p;
    return out;
}

std::optional<PartChoice> part_for(const Catalogue& cat, std::string_view chapter_id, std::size_t section, Region wanted) {
    const CatalogueChapter* ch = cat.chapter(chapter_id);
    if (!ch || section >= ch->sections.size() || !ch->sections[section].playable()) return std::nullopt;
    const CatalogueSection& s = ch->sections[section];
    PartChoice p;
    p.chapter_id = ch->id;
    p.section = section;
    p.label = s.label();
    p.region = wanted;
    if (wanted == Region::NA)
        for (const auto& sc : s.scripts)
            if (!sc.english) p.region = Region::JP;
    for (const auto& sc : s.scripts) p.script_ids.push_back(sc.id);
    return p;
}

bool Progress::is_read(const std::string& chapter, std::size_t section) const {
    const auto it = read.find(chapter);
    return it != read.end() && it->second.count(section) > 0;
}

void Progress::mark_read(const std::string& chapter, std::size_t section) { read[chapter].insert(section); }

void Progress::opened(const std::string& chapter, std::size_t section) {
    last_chapter = chapter;
    last_section = section;
}

Progress parse_progress(std::string_view text) {
    Progress p;
    const auto json = Json::parse(text);
    if (!json || !json->is_object()) return p;
    p.region = region_of((*json)["region"].string());
    const Json& last = (*json)["last"];
    if (!last["chapter"].string().empty() && last["section"].number(-1) >= 0) {
        p.last_chapter = last["chapter"].string();
        p.last_section = static_cast<std::size_t>(last["section"].integer());
    }
    for (const auto& [chapter, list] : (*json)["read"].members())
        for (const Json& n : list.items())
            if (n.number(-1) >= 0) p.read[chapter].insert(static_cast<std::size_t>(n.integer()));
    return p;
}

std::string serialize_progress(const Progress& p) {
    std::string out = "{\"region\": ";
    escape_into(out, p.region == Region::JP ? "JP" : "NA");
    if (!p.last_chapter.empty()) {
        out += ", \"last\": {\"chapter\": ";
        escape_into(out, p.last_chapter);
        out += ", \"section\": " + std::to_string(p.last_section) + "}";
    }
    out += ", \"read\": {";
    bool first = true;
    for (const auto& [chapter, set] : p.read) {
        if (set.empty()) continue;
        out += first ? "" : ", ";
        first = false;
        escape_into(out, chapter);
        out += ": [";
        bool f2 = true;
        for (const std::size_t n : set) {
            out += (f2 ? "" : ", ") + std::to_string(n);
            f2 = false;
        }
        out += "]";
    }
    out += "}}\n";
    return out;
}

std::optional<std::size_t> next_unread(const CatalogueChapter& c, const Progress& p) {
    std::optional<std::size_t> first;
    for (std::size_t i = 0; i < c.sections.size(); ++i) {
        if (!c.sections[i].playable()) continue;
        if (!first) first = i;
        if (!p.is_read(c.id, i)) return i;
    }
    return first;
}

}  // namespace vnr::rt
