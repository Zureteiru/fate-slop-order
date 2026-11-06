// Dialogue text with inline tags (ruby, colour, size, speed…) resolved into styled spans.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vnr/command.h"

namespace vnr {

enum class Gender { Male, Female };

struct TextContext {
    std::string master_name = "Fujimaru";  // replaces [%1]
    Gender gender = Gender::Male;          // picks [&male:female]
};

enum class Align { Left, Center, Right };

struct TextStyle {
    std::optional<Rgba> color;  // nullopt = default ink (white)
    float scale = 1.f;          // font size relative to the normal 29 px
    Align align = Align::Left;
    float speed = 0.f;          // [speed n]; 0 = setting speed. Rate = cps * speed / 20.
    bool operator==(const TextStyle& o) const {
        return color == o.color && scale == o.scale && align == o.align && speed == o.speed;
    }
};

struct Span {
    enum class Kind { Text, Ruby, Image, Break };
    Kind kind = Kind::Text;
    std::string text;  // Text: the text; Ruby: the base; Image: image name; Break: empty
    std::string ruby;  // Ruby only: reading shown above the base
    TextStyle style;
    std::size_t glyphs = 0;  // glyphs this span adds to the typewriter count
};

// A command written inside dialogue text; it runs when typing reaches `glyph`.
struct InlineCommand {
    std::size_t glyph = 0;
    Command cmd;
};

struct RichText {
    std::vector<Span> spans;
    std::vector<InlineCommand> commands;
    std::size_t glyph_count = 0;

    std::string plain() const;  // text with ruby bases, breaks as '\n', no tags
    bool empty() const { return glyph_count == 0; }
};

// Parses dialogue text. `line` is recorded on inline commands.
RichText parse_rich_text(std::string_view src, const TextContext& ctx, int line = 0);

// Seconds after the line starts at which each glyph has been revealed (size = glyph_count).
// cps <= 0 means instant (all zeros).
std::vector<double> reveal_times(const RichText& text, double cps);

}  // namespace vnr
