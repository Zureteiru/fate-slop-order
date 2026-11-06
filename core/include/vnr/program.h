// Compiling a script into a flat instruction list with real control flow.
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "vnr/command.h"
#include "vnr/rich_text.h"

namespace vnr {

// Run a command (stage, audio, wait…).
struct ExecOp {
    Command cmd;
};

// Show one dialogue block and wait for the reader.
struct SayOp {
    std::string speaker_code;  // "A" from "＠A：Mash"; empty if none
    RichText speaker;          // display name; empty for narration
    RichText text;
    std::string tvoice_folder, tvoice_file;  // from [tVoice folder file], if any
    int line = 0;                            // line of the ＠ (or first text) line
    int end_line = 0;                        // line of the closing [k]
};

struct ChoiceOption {
    int id = 0;
    RichText text;
    std::size_t target = 0;  // first instruction of this option's body
    int line = 0;
};

// Present options; each option body ends with a jump to `join`.
struct ChoiceOp {
    std::vector<ChoiceOption> options;
    std::size_t join = 0;
    int line = 0;
};

struct JumpOp {
    std::size_t target = 0;
    std::string label;  // for diagnostics and dumps
    int line = 0;
};

enum class Condition { QuestCleared, QuestNotCleared, HasMaterial, Revival, MasterFemale };

// Jump to target when the condition holds; otherwise to `else_target` (masterBranch) or fall through.
struct CondJumpOp {
    Condition cond = Condition::QuestCleared;
    std::string arg;  // quest id / material id when the script gives one
    std::size_t target = 0;
    std::string label;
    bool has_else = false;
    std::size_t else_target = 0;
    std::string else_label;
    int line = 0;
};

struct EndOp {
    int line = 0;
};

using Instr = std::variant<ExecOp, SayOp, ChoiceOp, JumpOp, CondJumpOp, EndOp>;

int instr_line(const Instr& in);

struct Diagnostic {
    int line = 0;
    std::string message;
};

struct Diagnostics {
    std::vector<Diagnostic> items;
    std::map<std::string, int> unknown_commands;  // name -> occurrences
    void add(int line, std::string msg) { items.push_back({line, std::move(msg)}); }
};

struct Program {
    std::vector<Instr> code;  // always ends with an EndOp
    std::map<std::string, std::size_t> labels;
    std::size_t choice_count = 0;
};

// True if the player implements `name` (stage, audio, wait or no-op-by-design commands).
bool command_supported(std::string_view name);

Program compile(std::string_view script, const TextContext& ctx, Diagnostics& diag);

}  // namespace vnr
