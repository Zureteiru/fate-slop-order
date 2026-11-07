#include "vnr/rt/saves.h"

#include <ctime>
#include <variant>

#include "vnr/rt/json.h"

namespace vnr::rt {
namespace {

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

void field(std::string& out, const char* key, const std::string& value) {
    out += ", \"";
    out += key;
    out += "\": ";
    escape_into(out, value);
}

std::optional<SaveEntry> parse_entry(const Json& j) {
    if (!j.is_object() || j["chapter"].string().empty() || j["section"].number(-1) < 0 ||
        j["script_index"].number(-1) < 0 || j["stop_pc"].number(-1) < 0)
        return std::nullopt;
    SaveEntry e;
    e.chapter_id = j["chapter"].string();
    e.section = static_cast<std::size_t>(j["section"].number());
    e.region = j["region"].string() == "JP" ? Region::JP : Region::NA;
    e.chapter_label = j["chapter_label"].string();
    e.part_label = j["part_label"].string();
    e.script_index = static_cast<std::size_t>(j["script_index"].number());
    e.script_id = j["script_id"].string();
    e.script_hash = static_cast<std::uint32_t>(j["script_hash"].number());
    e.stop_pc = static_cast<std::size_t>(j["stop_pc"].number());
    for (const Json& c : j["choices"].items()) {
        if (c.number(-1) < 0) return std::nullopt;
        e.choices.push_back(c.integer());
    }
    e.line = j["line"].integer();
    e.speaker = j["speaker"].string();
    e.text = j["text"].string();
    e.background = j["background"].string();
    e.auto_mode = j["auto"].boolean();
    e.saved_at = static_cast<std::int64_t>(j["saved_at"].number());
    return e;
}

void entry_into(std::string& out, const std::optional<SaveEntry>& opt) {
    if (!opt) {
        out += "null";
        return;
    }
    const SaveEntry& e = *opt;
    out += "{\"chapter\": ";
    escape_into(out, e.chapter_id);
    out += ", \"section\": " + std::to_string(e.section);
    field(out, "region", e.region == Region::JP ? "JP" : "NA");
    field(out, "chapter_label", e.chapter_label);
    field(out, "part_label", e.part_label);
    out += ", \"script_index\": " + std::to_string(e.script_index);
    field(out, "script_id", e.script_id);
    out += ", \"script_hash\": " + std::to_string(e.script_hash);
    out += ", \"stop_pc\": " + std::to_string(e.stop_pc);
    out += ", \"choices\": [";
    for (std::size_t i = 0; i < e.choices.size(); ++i) out += (i ? ", " : "") + std::to_string(e.choices[i]);
    out += "], \"line\": " + std::to_string(e.line);
    field(out, "speaker", e.speaker);
    field(out, "text", e.text);
    field(out, "background", e.background);
    out += std::string(", \"auto\": ") + (e.auto_mode ? "true" : "false");
    out += ", \"saved_at\": " + std::to_string(e.saved_at) + "}";
}

}  // namespace

SaveFile parse_saves(std::string_view text) {
    SaveFile f;
    const auto json = Json::parse(text);
    if (!json || !json->is_object()) return f;
    f.autosave = parse_entry((*json)["autosave"]);
    f.quick = parse_entry((*json)["quick"]);
    const Json& slots = (*json)["slots"];
    for (std::size_t i = 0; i < kSaveSlots && i < slots.size(); ++i) f.slots[i] = parse_entry(slots[i]);
    return f;
}

std::string serialize_saves(const SaveFile& f) {
    std::string out = "{\"version\": 1,\n\"autosave\": ";
    entry_into(out, f.autosave);
    out += ",\n\"quick\": ";
    entry_into(out, f.quick);
    out += ",\n\"slots\": [";
    for (std::size_t i = 0; i < kSaveSlots; ++i) {
        out += i ? ",\n  " : "\n  ";
        entry_into(out, f.slots[i]);
    }
    out += "\n]}\n";
    return out;
}

std::uint32_t fnv1a(std::string_view text) {
    std::uint32_t h = 2166136261u;
    for (const char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    return h;
}

std::string preview_text(std::string_view plain, std::size_t max_chars) {
    std::string out;
    std::size_t chars = 0;
    for (std::size_t i = 0; i < plain.size();) {
        const unsigned char c = static_cast<unsigned char>(plain[i]);
        std::size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (i + len > plain.size()) len = plain.size() - i;
        if (chars == max_chars) return out + "\xE2\x80\xA6";  // …
        if (plain[i] == '\n' || plain[i] == '\r') out += ' ';
        else out.append(plain.substr(i, len));
        i += len;
        ++chars;
    }
    return out;
}

std::string format_save_time(std::int64_t unix_seconds) {
    const std::time_t t = static_cast<std::time_t>(unix_seconds);
    std::tm tm{};
#ifdef _WIN32
    if (localtime_s(&tm, &t) != 0) return {};
#else
    if (!localtime_r(&t, &tm)) return {};
#endif
    char buf[32];
    if (std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm) == 0) return {};
    return buf;
}

std::optional<ResumePoint> resume_point(const SaveEntry& e, const Program& prog, std::uint32_t script_hash) {
    if (e.script_hash == script_hash && e.stop_pc < prog.code.size()) return ResumePoint{e.stop_pc, e.choices};
    for (std::size_t pc = 0; pc < prog.code.size(); ++pc) {
        const auto* say = std::get_if<SayOp>(&prog.code[pc]);
        if (say && say->line == e.line) return ResumePoint{pc, e.choices};  // the choices are likely still right
    }
    return std::nullopt;
}

}  // namespace vnr::rt
