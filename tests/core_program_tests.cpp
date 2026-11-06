// compile(): dialogue blocks, choices, labels and branches.
#include "check.h"
#include "vnr/program.h"

using namespace vnr;

namespace {
Program build(const char* src, Diagnostics& d) { return compile(src, TextContext{}, d); }

template <class T>
std::vector<const T*> all(const Program& p) {
    std::vector<const T*> out;
    for (const auto& in : p.code)
        if (const auto* x = std::get_if<T>(&in)) out.push_back(x);
    return out;
}
}  // namespace

TEST(program_dialogue_blocks) {
    Diagnostics d;
    const Program p = build(
        "[scene 10000]\n"
        "＠A：???\n"
        "Fou?[sr]Kyuu...\n"
        "[k]\n"
        "\n"
        "＠\n"
        "Narration line one\n"
        "and two.\n"
        "[k]\n"
        "＠[51d4ff]Announcement[-]\n"
        "Base sequence[r]confirmed\n"
        "[k]\n",
        d);
    const auto says = all<SayOp>(p);
    REQUIRE(says.size() == 3);
    CHECK_EQ(says[0]->speaker_code, std::string("A"));
    CHECK_EQ(says[0]->speaker.plain(), std::string("???"));
    CHECK_EQ(says[0]->text.plain(), std::string("Fou?\nKyuu..."));
    CHECK_EQ(says[0]->line, 2);
    CHECK_EQ(says[0]->end_line, 4);
    CHECK(says[1]->speaker.empty());
    CHECK_EQ(says[1]->text.plain(), std::string("Narration line oneand two."));
    CHECK_EQ(says[2]->speaker.plain(), std::string("Announcement"));
    CHECK(says[2]->speaker.spans[0].style.color.has_value());
    CHECK(d.items.empty());
}

TEST(program_inline_command_lines_inside_block) {
    Diagnostics d;
    const Program p = build("＠Mash\nHello\n[charaFace A 4]\nthere.\n[k]\n", d);
    const auto says = all<SayOp>(p);
    REQUIRE(says.size() == 1);
    REQUIRE(says[0]->text.commands.size() == 1);
    CHECK_EQ(says[0]->text.commands[0].cmd.name, std::string("charaFace"));
    CHECK_EQ(says[0]->text.commands[0].glyph, static_cast<std::size_t>(5));
}

TEST(program_choice_bodies_and_join) {
    Diagnostics d;
    const Program p = build(
        "？1：Left\n"
        "[scene 1]\n"
        "？2：Right\n"
        "[scene 2]\n"
        "＠Mash\nRight it is.\n[k]\n"
        "？3：Nothing\n"
        "？！\n"
        "[scene 9]\n",
        d);
    CHECK_EQ(p.choice_count, static_cast<std::size_t>(1));
    REQUIRE(std::holds_alternative<ChoiceOp>(p.code[0]));
    const auto& c = std::get<ChoiceOp>(p.code[0]);
    REQUIRE(c.options.size() == 3);
    CHECK_EQ(c.options[0].text.plain(), std::string("Left"));
    // option 1 body: [scene 1] then jump to join
    CHECK(std::holds_alternative<ExecOp>(p.code[c.options[0].target]));
    const auto& j1 = std::get<JumpOp>(p.code[c.options[0].target + 1]);
    CHECK_EQ(j1.target, c.join);
    // option 3 is empty: its first instruction is the jump to the join
    CHECK_EQ(std::get<JumpOp>(p.code[c.options[2].target]).target, c.join);
    CHECK_EQ(std::get<ExecOp>(p.code[c.join]).cmd.arg(0), std::string("9"));
    CHECK(d.items.empty());
}

TEST(program_labels_branches_and_missing_label) {
    Diagnostics d;
    const Program p = build(
        "[branch later]\n"
        "[scene 1]\n"
        "[label later]\n"
        "[scene 2]\n"
        "[branch nowhere]\n"
        "[masterBranch male female]\n"
        "[label male]\n"
        "[label female]\n",
        d);
    const auto& j = std::get<JumpOp>(p.code[0]);
    CHECK_EQ(std::get<ExecOp>(p.code[j.target]).cmd.arg(0), std::string("2"));
    REQUIRE(d.items.size() == 1);
    CHECK_EQ(d.items[0].line, 5);
    const auto& bad = std::get<JumpOp>(p.code[3]);
    CHECK_EQ(bad.target, static_cast<std::size_t>(4));  // falls through
    const auto& mb = std::get<CondJumpOp>(p.code[4]);
    CHECK(mb.cond == Condition::MasterFemale);
    CHECK(mb.has_else);
}

TEST(program_else_endif_and_end) {
    Diagnostics d;
    const Program p = build("[scene 1]\n[else]\n[scene 2]\n[endIf]\n[scene 3]\n[end]\n[scene 4]\n", d);
    const auto& j = std::get<JumpOp>(p.code[1]);
    CHECK_EQ(std::get<ExecOp>(p.code[j.target]).cmd.arg(0), std::string("3"));
    CHECK(std::holds_alternative<EndOp>(p.code[4]));
    CHECK(std::holds_alternative<EndOp>(p.code.back()));
}

TEST(program_unknown_commands_counted) {
    Diagnostics d;
    build("[fancyNewCommand 1]\n[fancyNewCommand 2]\n[input name]\n[charaFadeinFSL A 0.1 1]\n", d);
    CHECK_EQ(d.unknown_commands.size(), static_cast<std::size_t>(1));
    CHECK_EQ(d.unknown_commands["fancyNewCommand"], 2);
}

TEST(program_tvoice_attaches_to_next_say) {
    Diagnostics d;
    const Program p = build("[tVoice Servants_100100 0_A010]\n＠Artoria\nHello.\n[k]\n", d);
    const auto says = all<SayOp>(p);
    REQUIRE(says.size() == 1);
    CHECK_EQ(says[0]->tvoice_folder, std::string("Servants_100100"));
    CHECK_EQ(says[0]->tvoice_file, std::string("0_A010"));
}
