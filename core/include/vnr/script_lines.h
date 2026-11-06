// UTF-8 helpers and splitting a raw Atlas script into classified lines.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace vnr {

namespace utf8 {
// Decodes the code point at s[i] and advances i. Invalid bytes decode as U+FFFD (one byte).
char32_t next(std::string_view s, std::size_t& i);
void append(std::string& out, char32_t cp);
std::size_t count(std::string_view s);
// True if s starts with the UTF-8 encoding of a full-width or ASCII character `cp`.
bool starts_with(std::string_view s, char32_t cp);
}  // namespace utf8

// ASCII whitespace plus U+3000 (ideographic space).
std::string_view trim(std::string_view s);

enum class LineKind {
    Blank,         // empty after trimming
    Comment,       // ＄… header or //…
    Speaker,       // ＠code：name  /  ＠name  /  ＠ (narration)
    ChoiceOption,  // ？n：text
    ChoiceEnd,     // ？！
    Bracket,       // starts with '[' (a command, or bracketed text inside dialogue)
    Text,          // anything else
};

struct ScriptLine {
    int number = 0;  // 1-based line number in the .txt
    LineKind kind = LineKind::Blank;
    std::string text;  // trimmed line
};

// Strips a UTF-8 BOM, splits on LF/CRLF, trims each line and classifies it.
std::vector<ScriptLine> split_lines(std::string_view script);

}  // namespace vnr
