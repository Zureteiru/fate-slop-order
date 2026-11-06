// Parsing one bracket command ("[charaFadein A 0.1 1]") and its typed arguments.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vnr {

struct Command {
    std::string name;               // first token, e.g. "charaFadein"
    std::vector<std::string> args;  // remaining tokens; quoted tokens keep their spaces
    int line = 0;                   // script line it came from

    const std::string& arg(std::size_t i) const;  // empty string when missing
    bool has(std::size_t i) const { return i < args.size(); }
};

// Splits the inside of a bracket into tokens: runs of non-space, or "quoted text".
std::vector<std::string> tokenize(std::string_view inner);

// Parses "[name args...]" (brackets optional). Returns nullopt for "[]" or a bracket with no name.
std::optional<Command> parse_command(std::string_view bracket, int line = 0);

// A plain number. Accepts a decimal comma ("0,5") when the whole token is digits,digits.
std::optional<double> parse_number(std::string_view token);

struct Vec2 {
    float x = 0, y = 0;
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
};

// A character position argument: a slot index ("1") or explicit stage offsets ("x,y[,scale]").
struct Position {
    bool is_slot = false;
    int slot = 0;
    Vec2 offset;                  // explicit offset, y positive upwards
    std::optional<float> scale;   // third component of "x,y,scale"
};
std::optional<Position> parse_position(std::string_view token);

struct Rgba {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const Rgba& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};
// "RRGGBB", "RRGGBBAA" (optional leading '#'), or the names black / white / red.
std::optional<Rgba> parse_color(std::string_view token);

}  // namespace vnr
