#include "vnr/script_lines.h"

namespace vnr {

namespace utf8 {

char32_t next(std::string_view s, std::size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    auto cont = [&](std::size_t k) -> int {
        if (i + k >= s.size()) return -1;
        const auto b = static_cast<unsigned char>(s[i + k]);
        return (b & 0xC0) == 0x80 ? (b & 0x3F) : -1;
    };
    if (b0 < 0x80) { i += 1; return b0; }
    if ((b0 & 0xE0) == 0xC0) {
        const int c1 = cont(1);
        if (c1 >= 0) { i += 2; return static_cast<char32_t>(((b0 & 0x1F) << 6) | c1); }
    } else if ((b0 & 0xF0) == 0xE0) {
        const int c1 = cont(1), c2 = cont(2);
        if (c1 >= 0 && c2 >= 0) { i += 3; return static_cast<char32_t>(((b0 & 0x0F) << 12) | (c1 << 6) | c2); }
    } else if ((b0 & 0xF8) == 0xF0) {
        const int c1 = cont(1), c2 = cont(2), c3 = cont(3);
        if (c1 >= 0 && c2 >= 0 && c3 >= 0) {
            i += 4;
            return static_cast<char32_t>(((b0 & 0x07) << 18) | (c1 << 12) | (c2 << 6) | c3);
        }
    }
    i += 1;
    return 0xFFFD;
}

void append(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::size_t count(std::string_view s) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size();) { next(s, i); ++n; }
    return n;
}

bool starts_with(std::string_view s, char32_t cp) {
    if (s.empty()) return false;
    std::size_t i = 0;
    return next(s, i) == cp;
}

}  // namespace utf8

namespace {
bool is_space_at(std::string_view s, std::size_t i, std::size_t& len) {
    const char c = s[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') { len = 1; return true; }
    // U+3000 = E3 80 80
    if (i + 2 < s.size() + 0 && static_cast<unsigned char>(c) == 0xE3 &&
        static_cast<unsigned char>(s[i + 1]) == 0x80 && static_cast<unsigned char>(s[i + 2]) == 0x80) {
        len = 3;
        return true;
    }
    return false;
}
}  // namespace

std::string_view trim(std::string_view s) {
    std::size_t len = 0;
    while (!s.empty() && is_space_at(s, 0, len)) s.remove_prefix(len);
    while (!s.empty()) {
        if (is_space_at(s, s.size() - 1, len)) { s.remove_suffix(1); continue; }
        if (s.size() >= 3 && is_space_at(s, s.size() - 3, len) && len == 3) { s.remove_suffix(3); continue; }
        break;
    }
    return s;
}

std::vector<ScriptLine> split_lines(std::string_view script) {
    if (script.size() >= 3 && script.substr(0, 3) == "\xEF\xBB\xBF") script.remove_prefix(3);
    std::vector<ScriptLine> out;
    int number = 0;
    std::size_t start = 0;
    while (start <= script.size()) {
        std::size_t end = script.find('\n', start);
        const bool last = end == std::string_view::npos;
        if (last) end = script.size();
        ++number;
        const std::string_view raw = trim(script.substr(start, end - start));
        ScriptLine line;
        line.number = number;
        line.text = std::string(raw);
        if (raw.empty()) {
            line.kind = LineKind::Blank;
        } else if (utf8::starts_with(raw, U'＄') || raw.substr(0, 2) == "//") {
            line.kind = LineKind::Comment;
        } else if (utf8::starts_with(raw, U'＠')) {
            line.kind = LineKind::Speaker;
        } else if (raw == "？！") {
            line.kind = LineKind::ChoiceEnd;
        } else if (utf8::starts_with(raw, U'？')) {
            line.kind = LineKind::ChoiceOption;
        } else if (raw[0] == '[') {
            line.kind = LineKind::Bracket;
        } else {
            line.kind = LineKind::Text;
        }
        out.push_back(std::move(line));
        if (last) break;
        start = end + 1;
    }
    // A trailing newline produces one empty final line; drop it so counts match editors.
    if (!out.empty() && out.back().kind == LineKind::Blank && !script.empty() && script.back() == '\n') out.pop_back();
    return out;
}

}  // namespace vnr
