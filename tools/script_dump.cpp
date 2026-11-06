// Host tool: compile an Atlas script and print its program and a playback timeline.
//   script_dump <script.txt> [--program] [--pick 1,0,2] [--female] [--name NAME]
// Timeline: one row per stop (dialogue / choice) with time, line, stage summary and text.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "vnr/player.h"

using namespace vnr;

namespace {

std::string read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string clip(std::string s, std::size_t n) {
    for (auto& c : s)
        if (c == '\n') c = '/';
    if (s.size() > n) {
        s.resize(n);
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();  // keep UTF-8 whole
        if (!s.empty() && static_cast<unsigned char>(s.back()) >= 0xC0) s.pop_back();
        s += "…";
    }
    return s;
}

void print_program(const Program& p) {
    for (std::size_t pc = 0; pc < p.code.size(); ++pc) {
        const Instr& in = p.code[pc];
        std::printf("%5zu  L%-5d ", pc, instr_line(in));
        if (const auto* e = std::get_if<ExecOp>(&in)) {
            std::printf("%s", e->cmd.name.c_str());
            for (const auto& a : e->cmd.args) std::printf(" %s", a.c_str());
        } else if (const auto* s = std::get_if<SayOp>(&in)) {
            std::printf("SAY [%s] %s", s->speaker.plain().c_str(), clip(s->text.plain(), 60).c_str());
        } else if (const auto* c = std::get_if<ChoiceOp>(&in)) {
            std::printf("CHOICE join=%zu", c->join);
            for (const auto& o : c->options) std::printf(" | %d->%zu %s", o.id, o.target, clip(o.text.plain(), 30).c_str());
        } else if (const auto* j = std::get_if<JumpOp>(&in)) {
            std::printf("JUMP %zu (%s)", j->target, j->label.c_str());
        } else if (const auto* cj = std::get_if<CondJumpOp>(&in)) {
            std::printf("CONDJUMP %zu (%s)", cj->target, cj->label.c_str());
        } else {
            std::printf("END");
        }
        std::printf("\n");
    }
}

std::string stage_summary(const Player& p) {
    const Stage& s = p.stage();
    std::string out = "bg=" + s.background;
    if (!p.bgm().empty()) out += " bgm=" + p.bgm();
    for (const auto& [code, e] : s.elements) {
        if (!e.shown) continue;
        char buf[96];
        const Vec2 pos{e.pos.x.to, e.pos.y.to};
        std::snprintf(buf, sizeof buf, " %s:%d/f%d@%g%s", code.c_str(), e.figure_id, e.face, pos.x, e.dimmed ? "(dim)" : "");
        out += buf;
    }
    for (const auto& fx : s.effects) out += " fx:" + fx.name + (fx.target.empty() ? "" : "@" + fx.target);
    if (s.communication) out += " comm:" + std::to_string(s.communication->figure_id);
    if (s.fade.opacity.to > 0) out += " fade";
    if (s.wipe.cover.to > 0) out += " wiped";
    if (s.shake.active) out += " shake";
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <script.txt> [--program] [--pick a,b,c] [--female] [--name NAME]\n", argv[0]);
        return 2;
    }
    bool show_program = false;
    std::vector<std::size_t> picks;
    Settings settings;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--program")) show_program = true;
        else if (!std::strcmp(argv[i], "--female")) settings.text.gender = Gender::Female;
        else if (!std::strcmp(argv[i], "--name") && i + 1 < argc) settings.text.master_name = argv[++i];
        else if (!std::strcmp(argv[i], "--pick") && i + 1 < argc) {
            std::stringstream ss(argv[++i]);
            std::string item;
            while (std::getline(ss, item, ',')) picks.push_back(static_cast<std::size_t>(std::atoi(item.c_str())));
        }
    }
    const std::string text = read_file(argv[1]);
    Diagnostics diag;
    const Program prog = compile(text, settings.text, diag);
    std::printf("# %zu instructions, %zu choices, %zu labels\n", prog.code.size(), prog.choice_count, prog.labels.size());
    for (const auto& d : diag.items) std::printf("# warning L%d: %s\n", d.line, d.message.c_str());
    for (const auto& [name, n] : diag.unknown_commands) std::printf("# unsupported: %s x%d\n", name.c_str(), n);
    if (show_program) print_program(prog);

    Player p(prog, settings);
    double t = 0;
    std::size_t pick = 0;
    const double dt = 1.0 / 120.0;
    for (int guard = 0; guard < 50000000 && p.status() != Status::Ended; ++guard) {
        switch (p.status()) {
            case Status::AwaitChoice: {
                const ChoiceOp* c = p.pending_choice();
                const std::size_t k = pick < picks.size() ? picks[pick] : 0;
                ++pick;
                std::printf("%8.2fs L%-5d CHOICE", t, p.line());
                for (std::size_t i = 0; i < c->options.size(); ++i)
                    std::printf(" %s[%zu] %s", i == k ? "*" : "", i, clip(c->options[i].text.plain(), 30).c_str());
                std::printf("\n");
                p.choose(k);
                break;
            }
            case Status::Typing:
            case Status::AwaitAdvance: {
                const TextView v = p.text();
                std::printf("%8.2fs L%-5d %-60s | %s: %s\n", t, v.say->line, stage_summary(p).c_str(),
                            v.say->speaker.plain().c_str(), clip(v.say->text.plain(), 70).c_str());
                p.advance();
                if (p.status() == Status::AwaitAdvance) p.advance();
                break;
            }
            default:
                p.update(dt);
                t += dt;
                break;
        }
    }
    std::printf("%8.2fs end\n", t);
    return 0;
}
