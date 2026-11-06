#include "vnr/program.h"

#include <cctype>
#include <cstdlib>
#include <type_traits>

#include "vnr/script_lines.h"

namespace vnr {

int instr_line(const Instr& in) {
    if (const auto* e = std::get_if<ExecOp>(&in)) return e->cmd.line;
    return std::visit(
        [](const auto& op) -> int {
            if constexpr (std::is_same_v<std::decay_t<decltype(op)>, ExecOp>) return op.cmd.line;
            else return op.line;
        },
        in);
}

namespace {

bool ends_with_wait_tag(std::string_view s) {
    return (s.size() >= 3 && (s.substr(s.size() - 3) == "[k]" || s.substr(s.size() - 3) == "[q]"));
}

struct SpeakerInfo {
    std::string code;
    std::string name;
};

SpeakerInfo parse_speaker(std::string_view line) {
    // line starts with "＠" (3 bytes)
    std::string_view s = trim(line.substr(3));
    const std::size_t spot = s.find("=spot");
    if (spot != std::string_view::npos) s = trim(s.substr(0, spot));
    std::size_t sep = s.find(':');
    std::size_t sep_len = 1;
    const std::size_t fw = s.find("：");
    if (fw != std::string_view::npos && (sep == std::string_view::npos || fw < sep)) { sep = fw; sep_len = 3; }
    if (sep != std::string_view::npos && sep >= 1 && sep <= 4) {
        const std::string_view code = s.substr(0, sep);
        bool alnum = true;
        for (char c : code)
            if (!std::isalnum(static_cast<unsigned char>(c))) alnum = false;
        if (alnum) return {std::string(code), std::string(trim(s.substr(sep + sep_len)))};
    }
    return {{}, std::string(s)};
}

// "？12：text" / "？1,xyz：text" -> id and text
bool parse_choice(std::string_view line, int& id, std::string& text) {
    std::string_view s = line.substr(3);  // after "？"
    std::size_t i = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    if (i == 0) return false;
    id = std::atoi(std::string(s.substr(0, i)).c_str());
    std::size_t sep = s.find("：", i);
    std::size_t sep_len = 3;
    const std::size_t ascii = s.find(':', i);
    if (ascii != std::string_view::npos && (sep == std::string_view::npos || ascii < sep)) { sep = ascii; sep_len = 1; }
    if (sep == std::string_view::npos) return false;
    // anything between the id and the separator must be a ",extra" suffix
    if (sep != i && s[i] != ',') return false;
    text = std::string(trim(s.substr(sep + sep_len)));
    return true;
}

struct PendingJump {
    std::size_t pc;
    std::string label;
    bool is_else_target;  // CondJump else branch
};

struct OpenChoice {
    std::size_t choice_pc;
    int last_id;
    std::vector<std::size_t> exit_jumps;  // JumpOp indexes to patch with the join
};

class Compiler {
public:
    Compiler(const TextContext& ctx, Diagnostics& diag) : ctx_(ctx), diag_(diag) {}

    Program run(std::string_view script) {
        for (const ScriptLine& ln : split_lines(script)) line(ln);
        flush(last_line_);
        while (!choices_.empty()) {
            diag_.add(std::get<ChoiceOp>(prog_.code[choices_.back().choice_pc]).line,
                      "choice block not closed with ？！ before end of script");
            close_choice(last_line_);
        }
        prog_.code.push_back(EndOp{last_line_});
        resolve();
        return std::move(prog_);
    }

private:
    const TextContext& ctx_;
    Diagnostics& diag_;
    Program prog_;
    int last_line_ = 0;

    // dialogue block state
    bool in_block_ = false;
    bool have_speaker_ = false;
    SpeakerInfo speaker_;
    std::string raw_;
    int block_line_ = 0;
    bool block_has_lines_ = false;
    std::string voice_folder_, voice_file_;

    std::vector<OpenChoice> choices_;
    std::vector<PendingJump> pending_;
    std::map<std::string, std::vector<std::size_t>> label_positions_;
    std::vector<std::size_t> endif_positions_;
    std::vector<std::size_t> else_jumps_;

    std::size_t pc() const { return prog_.code.size(); }

    void line(const ScriptLine& ln) {
        last_line_ = ln.number;
        switch (ln.kind) {
            case LineKind::Blank:
            case LineKind::Comment:
                return;
            case LineKind::Speaker:
                if (block_has_lines_) flush(ln.number);
                speaker_ = parse_speaker(ln.text);
                have_speaker_ = true;
                open_block(ln.number);
                return;
            case LineKind::ChoiceOption:
            case LineKind::ChoiceEnd:
                if (block_has_lines_) flush(ln.number);
                in_block_ = false;
                if (ln.kind == LineKind::ChoiceEnd) end_choice(ln.number);
                else choice_option(ln);
                return;
            case LineKind::Bracket:
                bracket(ln);
                return;
            case LineKind::Text:
                if (!in_block_) {
                    if (!have_speaker_) speaker_ = {};
                    open_block(ln.number);
                }
                append(ln);
                return;
        }
    }

    void open_block(int number) {
        in_block_ = true;
        raw_.clear();
        block_has_lines_ = false;
        block_line_ = number;
    }

    void append(const ScriptLine& ln) {
        raw_ += ln.text;
        block_has_lines_ = true;
        if (ends_with_wait_tag(ln.text)) flush(ln.number);
    }

    void flush(int end_line) {
        if (!in_block_ || !block_has_lines_) {
            in_block_ = false;
            return;
        }
        SayOp say;
        say.speaker_code = speaker_.code;
        say.speaker = parse_rich_text(speaker_.name, ctx_, block_line_);
        say.text = parse_rich_text(raw_, ctx_, block_line_);
        say.tvoice_folder = std::move(voice_folder_);
        say.tvoice_file = std::move(voice_file_);
        voice_folder_.clear();
        voice_file_.clear();
        say.line = block_line_;
        say.end_line = end_line;
        for (const auto& ic : say.text.commands) note_command(ic.cmd);
        prog_.code.push_back(std::move(say));
        in_block_ = false;
        block_has_lines_ = false;
        raw_.clear();
    }

    void note_command(const Command& c) {
        if (!command_supported(c.name)) {
            ++diag_.unknown_commands[c.name];
        }
    }

    void bracket(const ScriptLine& ln) {
        const auto cmd = parse_command(ln.text, ln.number);
        if (!cmd) return;
        const std::string& n = cmd->name;
        if (n == "k" || n == "q" || n == "page") {
            flush(ln.number);
            return;
        }
        if (n == "tVoice") {
            voice_folder_ = cmd->arg(0);
            voice_file_ = cmd->arg(1);
            return;
        }
        if (in_block_) {  // commands and bracketed text inside a dialogue block run inline
            append(ln);
            return;
        }
        // A line that starts with a text tag ("[51d4ff]Announcement…") outside a block is text.
        if (ln.text.find(']') + 1 < ln.text.size() && !ends_with_wait_tag(ln.text) && looks_like_text(*cmd)) {
            if (!have_speaker_) speaker_ = {};
            open_block(ln.number);
            append(ln);
            return;
        }
        structural_or_exec(*cmd);
    }

    static bool looks_like_text(const Command& c) {
        const std::string& n = c.name;
        if (n == "#" || n.rfind("#", 0) == 0 || n.rfind("&", 0) == 0 || n == "line" || n == "%1" || n == "-") return true;
        if (n.size() == 6 && c.args.empty()) {
            for (char ch : n)
                if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
            return true;
        }
        return false;
    }

    void structural_or_exec(const Command& c) {
        const std::string& n = c.name;
        if (n == "label") {
            if (c.has(0)) label_positions_[c.arg(0)].push_back(pc());
            else diag_.add(c.line, "[label] without a name");
            return;
        }
        if (n == "branch") {
            if (!c.has(0)) { diag_.add(c.line, "[branch] without a label"); return; }
            if (c.args.size() > 1) diag_.add(c.line, "[branch] with several labels; jumping to the first");
            pending_.push_back({pc(), c.arg(0), false});
            prog_.code.push_back(JumpOp{0, c.arg(0), c.line});
            return;
        }
        if (n == "branchQuestClear" || n == "branchQuestNotClear" || n == "branchMaterial" || n == "revivalBranch") {
            if (!c.has(0)) { diag_.add(c.line, "[" + n + "] without a label"); return; }
            CondJumpOp op;
            op.cond = n == "branchQuestClear"      ? Condition::QuestCleared
                      : n == "branchQuestNotClear" ? Condition::QuestNotCleared
                      : n == "branchMaterial"      ? Condition::HasMaterial
                                                   : Condition::Revival;
            op.arg = c.arg(1);
            op.label = c.arg(0);
            op.line = c.line;
            pending_.push_back({pc(), c.arg(0), false});
            prog_.code.push_back(std::move(op));
            return;
        }
        if (n == "masterBranch") {  // [masterBranch maleLabel femaleLabel]
            if (c.args.size() < 2) { diag_.add(c.line, "[masterBranch] needs two labels"); return; }
            CondJumpOp op;
            op.cond = Condition::MasterFemale;
            op.label = c.arg(1);
            op.has_else = true;
            op.else_label = c.arg(0);
            op.line = c.line;
            pending_.push_back({pc(), c.arg(1), false});
            pending_.push_back({pc(), c.arg(0), true});
            prog_.code.push_back(std::move(op));
            return;
        }
        if (n == "else") {
            else_jumps_.push_back(pc());
            prog_.code.push_back(JumpOp{0, "endIf", c.line});
            return;
        }
        if (n == "endIf") {
            endif_positions_.push_back(pc());
            return;
        }
        if (n == "end") {
            prog_.code.push_back(EndOp{c.line});
            return;
        }
        note_command(c);
        prog_.code.push_back(ExecOp{c});
    }

    void choice_option(const ScriptLine& ln) {
        int id = 0;
        std::string text;
        if (!parse_choice(ln.text, id, text)) {
            diag_.add(ln.number, "unreadable choice line");
            return;
        }
        if (!choices_.empty() && id > choices_.back().last_id) {
            close_option(ln.number);
        } else {
            ChoiceOp op;
            op.line = ln.number;
            choices_.push_back({pc(), id, {}});
            prog_.code.push_back(std::move(op));
            ++prog_.choice_count;
        }
        OpenChoice& open = choices_.back();
        open.last_id = id;
        auto& choice = std::get<ChoiceOp>(prog_.code[open.choice_pc]);
        choice.options.push_back({id, parse_rich_text(text, ctx_, ln.number), pc(), ln.number});
    }

    void close_option(int number) {
        choices_.back().exit_jumps.push_back(pc());
        prog_.code.push_back(JumpOp{0, "？！", number});
    }

    void close_choice(int number) {
        close_option(number);
        OpenChoice open = choices_.back();
        choices_.pop_back();
        const std::size_t join = pc();
        for (std::size_t j : open.exit_jumps) std::get<JumpOp>(prog_.code[j]).target = join;
        std::get<ChoiceOp>(prog_.code[open.choice_pc]).join = join;
    }

    void end_choice(int number) {
        if (choices_.empty()) {
            diag_.add(number, "？！ without an open choice");
            return;
        }
        close_choice(number);
    }

    std::size_t resolve_label(const std::string& label, std::size_t from, int line, bool& ok) {
        const auto it = label_positions_.find(label);
        if (it == label_positions_.end() || it->second.empty()) {
            diag_.add(line, "missing label '" + label + "'");
            ok = false;
            return from + 1;
        }
        for (std::size_t p : it->second)
            if (p > from) return p;
        return it->second.front();
    }

    void resolve() {
        for (const auto& [name, positions] : label_positions_) prog_.labels[name] = positions.front();
        for (const PendingJump& pj : pending_) {
            bool ok = true;
            Instr& in = prog_.code[pj.pc];
            if (auto* j = std::get_if<JumpOp>(&in)) {
                j->target = resolve_label(pj.label, pj.pc, j->line, ok);
            } else if (auto* cj = std::get_if<CondJumpOp>(&in)) {
                const std::size_t t = resolve_label(pj.label, pj.pc, cj->line, ok);
                if (pj.is_else_target) cj->else_target = t;
                else cj->target = t;
            }
        }
        for (std::size_t e : else_jumps_) {
            auto& j = std::get<JumpOp>(prog_.code[e]);
            std::size_t target = prog_.code.size() - 1;  // End if no [endIf] follows
            bool found = false;
            for (std::size_t p : endif_positions_)
                if (p > e) { target = p; found = true; break; }
            if (!found) diag_.add(j.line, "[else] without a following [endIf]");
            j.target = target;
        }
    }
};

}  // namespace

Program compile(std::string_view script, const TextContext& ctx, Diagnostics& diag) {
    return Compiler(ctx, diag).run(script);
}

}  // namespace vnr
