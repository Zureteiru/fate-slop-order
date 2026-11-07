// Golden tests on real Atlas scripts (fetched by tests/fetch_fixtures.sh, SHA-256 pinned).
#include "check.h"
#include "drive.h"
#include "vnr/player.h"

#ifndef VNR_FIXTURE_DIR
#error "VNR_FIXTURE_DIR must point at .cache/scripts"
#endif

using namespace vnr;

namespace {
std::string fixture(const char* name) { return drive::read_file(std::string(VNR_FIXTURE_DIR) + "/" + name); }

struct Script {
    std::string text;
    Diagnostics diag;
    Program prog;
    explicit Script(const char* name, TextContext ctx = {}) : text(fixture(name)), prog(compile(text, ctx, diag)) {}
};

const Element* el(const Player& p, const char* code) {
    const auto& e = p.stage().elements;
    const auto it = e.find(code);
    return it == e.end() ? nullptr : &it->second;
}

bool waiting_at(const Player& p, int line) { return p.status() == Status::Waiting && p.line() == line; }
}  // namespace

TEST(golden_scripts_compile_cleanly) {
    for (const char* name : {"NA_0100000011.txt", "JP_0100000011.txt", "NA_0100081810.txt"}) {
        Script s(name);
        REQUIRE(!s.text.empty());
        for (const auto& [cmd, count] : s.diag.unknown_commands)
            check::fail(__FILE__, __LINE__, std::string(name) + ": unsupported command " + cmd + " x" + std::to_string(count));
        for (const auto& d : s.diag.items)
            check::fail(__FILE__, __LINE__, std::string(name) + ":" + std::to_string(d.line) + ": " + d.message);
    }
}

TEST(golden_fuyuki_choice_blocks) {
    CHECK_EQ(Script("NA_0100000011.txt").prog.choice_count, static_cast<std::size_t>(22));
    CHECK_EQ(Script("JP_0100000011.txt").prog.choice_count, static_cast<std::size_t>(22));
}

TEST(golden_fuyuki_line46_fade_is_script_time) {
    Script s("NA_0100000011.txt");
    Player p(s.prog);
    REQUIRE(drive::until(p, [&] { return waiting_at(p, 48); }));  // [wait wipe] right after L46-47
    const Element* a = el(p, "A");
    REQUIRE(a);
    CHECK(a->shown);
    CHECK_NEAR(a->opacity.duration, 0.1, 1e-9);  // [charaFadein A 0.1 1], not a fixed 0.3
    CHECK_EQ(a->face, 12);
    CHECK_NEAR(p.stage().wipe.cover.duration, 1.5, 1e-9);  // [wipein circleIn 1.5 2]
    CHECK(p.stage().wipe.reveal);
}

TEST(golden_fuyuki_line57_mash_slot1) {
    Script s("NA_0100000011.txt");
    Player p(s.prog);
    REQUIRE(drive::until(p, [&] { return drive::at_say(p, 56); }));
    const Element* a = el(p, "A");
    REQUIRE(a);
    CHECK(a->shown);
    CHECK_EQ(a->figure_id, 98001000);
    CHECK_EQ(a->face, 12);
    CHECK(a->pos.value() == (Vec2{0, 0}));
    CHECK_EQ(p.stage().background, std::string("10310"));
    CHECK_EQ(p.bgm(), std::string("BGM_EVENT_2"));
    CHECK_EQ(p.text().say->speaker.plain(), std::string("???"));
}

TEST(golden_fuyuki_line523_slots_and_focus) {
    Script s("NA_0100000011.txt");
    Player p(s.prog);
    REQUIRE(drive::until(p, [&] { return drive::at_say(p, 522); }));
    const Element* mash = el(p, "A");
    const Element* lev = el(p, "D");
    REQUIRE(mash && lev);
    CHECK(mash->pos.value() == (Vec2{256, 0}));
    CHECK(lev->pos.value() == (Vec2{-256, 0}));
    CHECK(mash->dimmed);   // the game dims the listener (YouTube 5:00 capture)
    CHECK(!lev->dimmed);
}

TEST(golden_fuyuki_line488_flash_timing) {
    Script s("NA_0100000011.txt");
    Player p(s.prog);
    REQUIRE(drive::until(p, [&] { return drive::at_say(p, 484); }));
    p.advance();
    p.advance();
    CHECK(p.stage().flash.active);
    CHECK_NEAR(p.stage().flash.rise, 1.0, 1e-9);
    CHECK_NEAR(p.stage().flash.duration, 0.4, 1e-9);
    // wt 0.5 + flashout 0.7 (wait flash) + wt 0.5
    const double t = drive::time_until(p, [&] { return drive::at_say(p, 494); });
    CHECK_NEAR(t, 1.7, 3 * drive::kDt);
}

TEST(golden_solomon_comms_and_effects) {
    Script s("NA_0100081810.txt");
    Player p(s.prog);
    // L41 "？2：Doctor, we're going" holds the comms scene; pick option 2.
    REQUIRE(drive::until(p, [&] { return drive::at_say(p, 49); }, {1}));
    REQUIRE(p.stage().communication.has_value());
    CHECK_EQ(p.stage().communication->figure_id, 98003003);
    CHECK_EQ(p.stage().communication->face, 5);

    REQUIRE(drive::until(p, [&] { return waiting_at(p, 70); }));  // [wait scene] after L69
    bool fx = false;
    for (const auto& e : p.stage().effects)
        if (e.name == "bit_talk_41" && e.target == "A" && e.layer == EffectLayer::Chara) fx = true;
    CHECK(fx);
    CHECK_NEAR(p.stage().camera.scale.to, 1.5, 1e-6);
    CHECK_NEAR(p.stage().camera.scale.duration, 5.0, 1e-9);
    CHECK(p.stage().shake.active);

    REQUIRE(drive::until(p, [&] { return p.line() >= 77; }));
    for (const auto& e : p.stage().effects) CHECK(e.name != "bit_talk_41");
}

TEST(golden_jp_ruby_line106) {
    Script s("JP_0100000011.txt");
    bool found = false;
    for (const auto& in : s.prog.code) {
        const auto* say = std::get_if<SayOp>(&in);
        if (!say || say->line > 106 || say->end_line < 106) continue;
        for (const auto& sp : say->text.spans)
            if (sp.kind == Span::Kind::Ruby && sp.text == "教科書" && sp.ruby == "テキスト") found = true;
    }
    CHECK(found);
}

TEST(golden_fuyuki_resume_matches_playing_there) {
    Script s("NA_0100000011.txt");
    Player played(s.prog);
    REQUIRE(drive::until(played, [&] { return drive::at_say(played, 522); }));
    played.advance();  // finish typing, as a save would be taken from a line read in full
    Player loaded(s.prog);
    REQUIRE(loaded.resume(played.pc(), played.choice_path()));
    CHECK(drive::at_say(loaded, 522));
    CHECK_EQ(loaded.stage().background, played.stage().background);
    CHECK_EQ(loaded.bgm(), played.bgm());
    CHECK_EQ(loaded.backlog().size(), played.backlog().size());
    const Element* mash = el(loaded, "A");
    const Element* lev = el(loaded, "D");
    REQUIRE(mash && lev);
    CHECK(mash->pos.value() == (Vec2{256, 0}));
    CHECK(lev->pos.value() == (Vec2{-256, 0}));
    CHECK(mash->dimmed);
}
