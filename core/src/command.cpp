#include "vnr/command.h"

#include <cctype>
#include <cstdlib>

#include "vnr/script_lines.h"

namespace vnr {

const std::string& Command::arg(std::size_t i) const {
    static const std::string empty;
    return i < args.size() ? args[i] : empty;
}

namespace {
bool space_at(std::string_view s, std::size_t i, std::size_t& len) {
    const char c = s[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { len = 1; return true; }
    if (i + 2 < s.size() && static_cast<unsigned char>(c) == 0xE3 &&
        static_cast<unsigned char>(s[i + 1]) == 0x80 && static_cast<unsigned char>(s[i + 2]) == 0x80) {
        len = 3;
        return true;
    }
    return false;
}
}  // namespace

std::vector<std::string> tokenize(std::string_view inner) {
    std::vector<std::string> out;
    std::size_t i = 0, len = 0;
    while (i < inner.size()) {
        if (space_at(inner, i, len)) { i += len; continue; }
        if (inner[i] == '"') {
            const std::size_t close = inner.find('"', i + 1);
            if (close != std::string_view::npos) {
                out.emplace_back(inner.substr(i + 1, close - i - 1));
                i = close + 1;
                continue;
            }
        }
        std::size_t j = i;
        while (j < inner.size() && !space_at(inner, j, len) && inner[j] != '"') ++j;
        if (j == i) ++j;  // a lone unmatched quote
        out.emplace_back(inner.substr(i, j - i));
        i = j;
    }
    return out;
}

std::optional<Command> parse_command(std::string_view bracket, int line) {
    bracket = trim(bracket);
    if (!bracket.empty() && bracket.front() == '[') bracket.remove_prefix(1);
    if (!bracket.empty() && bracket.back() == ']') bracket.remove_suffix(1);
    auto tokens = tokenize(bracket);
    if (tokens.empty() || tokens[0].empty()) return std::nullopt;
    Command c;
    c.name = std::move(tokens[0]);
    c.args.assign(std::make_move_iterator(tokens.begin() + 1), std::make_move_iterator(tokens.end()));
    c.line = line;
    return c;
}

namespace {
bool all_digits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}
}  // namespace

std::optional<double> parse_number(std::string_view token) {
    std::string t(trim(token));
    if (t.empty()) return std::nullopt;
    // "0,5" -> "0.5" only when the token is exactly [+-]digits,digits.
    const auto comma = t.find(',');
    if (comma != std::string::npos) {
        std::string_view head(t.data(), comma);
        if (!head.empty() && (head[0] == '+' || head[0] == '-')) head.remove_prefix(1);
        if (all_digits(head) && all_digits(std::string_view(t).substr(comma + 1))) t[comma] = '.';
        else return std::nullopt;
    }
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str()) return std::nullopt;
    // Allow trailing garbage like "1.0f"? No: the scripts never use it, so reject to catch typos.
    if (*end != '\0') return std::nullopt;
    return v;
}

std::optional<Position> parse_position(std::string_view token) {
    token = trim(token);
    if (token.empty()) return std::nullopt;
    Position p;
    if (token.find(',') == std::string_view::npos) {
        std::string_view digits = token;
        bool neg = false;
        if (!digits.empty() && digits[0] == '-') { neg = true; digits.remove_prefix(1); }
        if (!all_digits(digits)) return std::nullopt;
        p.is_slot = true;
        p.slot = std::atoi(std::string(digits).c_str()) * (neg ? -1 : 1);
        return p;
    }
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t c = token.find(',', start);
        parts.push_back(token.substr(start, c == std::string_view::npos ? std::string_view::npos : c - start));
        if (c == std::string_view::npos) break;
        start = c + 1;
    }
    if (parts.size() < 2 || parts.size() > 3) return std::nullopt;
    const auto x = parse_number(parts[0]);
    const auto y = parse_number(parts[1]);
    if (!x || !y) return std::nullopt;
    p.offset = {static_cast<float>(*x), static_cast<float>(*y)};
    if (parts.size() == 3) {
        const auto s = parse_number(parts[2]);
        if (!s) return std::nullopt;
        p.scale = static_cast<float>(*s);
    }
    return p;
}

std::optional<Rgba> parse_color(std::string_view token) {
    token = trim(token);
    if (token == "black") return Rgba{0, 0, 0, 255};
    if (token == "white") return Rgba{255, 255, 255, 255};
    if (token == "red") return Rgba{255, 0, 0, 255};
    if (!token.empty() && token[0] == '#') token.remove_prefix(1);
    if (token.size() != 6 && token.size() != 8) return std::nullopt;
    for (char c : token)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
    auto byte = [&](std::size_t i) {
        return static_cast<std::uint8_t>(std::strtoul(std::string(token.substr(i, 2)).c_str(), nullptr, 16));
    };
    Rgba c{byte(0), byte(2), byte(4), 255};
    if (token.size() == 8) c.a = byte(6);
    return c;
}

}  // namespace vnr
