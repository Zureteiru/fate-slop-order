// script_lines, command and rich_text.
#include "check.h"
#include "vnr/command.h"
#include "vnr/rich_text.h"
#include "vnr/script_lines.h"

using namespace vnr;

TEST(lines_bom_crlf_and_kinds) {
    const auto lines = split_lines("\xEF\xBB\xBF＄01-00\r\n[scene 10]\r\n＠A：???\r\nhello[k]\r\n？1：yes\r\n？！\r\n\r\n// note\r\ntext");
    REQUIRE(lines.size() == 9);
    CHECK(lines[0].kind == LineKind::Comment);
    CHECK(lines[1].kind == LineKind::Bracket);
    CHECK_EQ(lines[1].text, std::string("[scene 10]"));
    CHECK(lines[2].kind == LineKind::Speaker);
    CHECK(lines[3].kind == LineKind::Text);
    CHECK(lines[4].kind == LineKind::ChoiceOption);
    CHECK(lines[5].kind == LineKind::ChoiceEnd);
    CHECK(lines[6].kind == LineKind::Blank);
    CHECK(lines[7].kind == LineKind::Comment);
    CHECK_EQ(lines[8].number, 9);
}

TEST(lines_trim_fullwidth_space) {
    const auto lines = split_lines("\xE3\x80\x80 hello \xE3\x80\x80\n");
    REQUIRE(lines.size() == 1);
    CHECK_EQ(lines[0].text, std::string("hello"));
}

TEST(command_tokens_and_quotes) {
    const auto c = parse_command("[charaSet A 98001000 0 \"Mash Kyrielight\"]", 7);
    REQUIRE(c.has_value());
    CHECK_EQ(c->name, std::string("charaSet"));
    REQUIRE(c->args.size() == 4);
    CHECK_EQ(c->args[3], std::string("Mash Kyrielight"));
    CHECK_EQ(c->line, 7);
    CHECK_EQ(c->arg(9), std::string());
    CHECK(!parse_command("[]").has_value());
}

TEST(numbers_positions_colors) {
    CHECK_NEAR(*parse_number("0.4"), 0.4, 1e-9);
    CHECK_NEAR(*parse_number("0,5"), 0.5, 1e-9);
    CHECK_NEAR(*parse_number("-2"), -2.0, 1e-9);
    CHECK(!parse_number("fade").has_value());
    CHECK(!parse_number("1,2,3").has_value());

    const auto slot = parse_position("2");
    REQUIRE(slot.has_value());
    CHECK(slot->is_slot);
    CHECK_EQ(slot->slot, 2);
    const auto xy = parse_position("-15,0");
    REQUIRE(xy.has_value());
    CHECK(!xy->is_slot);
    CHECK_NEAR(xy->offset.x, -15, 1e-6);
    const auto xys = parse_position("0,-50,1.1");
    REQUIRE(xys.has_value());
    CHECK_NEAR(*xys->scale, 1.1, 1e-6);

    CHECK(parse_color("FFFFFF80") == (Rgba{255, 255, 255, 128}));
    CHECK(parse_color("51d4ff") == (Rgba{0x51, 0xd4, 0xff, 255}));
    CHECK(parse_color("black") == (Rgba{0, 0, 0, 255}));
    CHECK(!parse_color("nope").has_value());
}

TEST(rich_breaks_and_glyphs) {
    const RichText t = parse_rich_text("Fou?[sr]Kyuu... Kyuu?[k]", {});
    CHECK_EQ(t.plain(), std::string("Fou?\nKyuu... Kyuu?"));
    CHECK_EQ(t.glyph_count, static_cast<std::size_t>(4 + 13));
}

TEST(rich_ruby_counts_base_only) {
    const RichText t = parse_rich_text("はい。[r][#教科書:テキスト]に載せたい", {});
    std::size_t rubies = 0;
    for (const auto& s : t.spans)
        if (s.kind == Span::Kind::Ruby) {
            ++rubies;
            CHECK_EQ(s.text, std::string("教科書"));
            CHECK_EQ(s.ruby, std::string("テキスト"));
            CHECK_EQ(s.glyphs, static_cast<std::size_t>(3));
        }
    CHECK_EQ(rubies, static_cast<std::size_t>(1));
    CHECK_EQ(t.glyph_count, static_cast<std::size_t>(3 + 3 + 5));
}

TEST(rich_color_stack_and_reset) {
    const RichText t = parse_rich_text("a[51d4ff]b[FF0000]c[-]d[-]e", {});
    REQUIRE(t.spans.size() == 5);
    CHECK(!t.spans[0].style.color.has_value());
    CHECK(t.spans[1].style.color == (Rgba{0x51, 0xd4, 0xff, 255}));
    CHECK(t.spans[2].style.color == (Rgba{255, 0, 0, 255}));
    CHECK(t.spans[3].style.color == (Rgba{0x51, 0xd4, 0xff, 255}));
    CHECK(!t.spans[4].style.color.has_value());
}

TEST(rich_line_name_gender_size) {
    TextContext ctx;
    ctx.master_name = "Ritsuka";
    ctx.gender = Gender::Female;
    const RichText t = parse_rich_text("[line 3]Hmm, [%1]. [&he:she] [s 58]BIG[-]", ctx);
    CHECK_EQ(t.plain(), std::string("━━━Hmm, Ritsuka. she BIG"));
    bool big = false;
    for (const auto& s : t.spans)
        if (s.text == "BIG") big = std::abs(s.style.scale - 2.0f) < 1e-4f;
    CHECK(big);
}

TEST(rich_inline_commands_at_glyph) {
    const RichText t = parse_rich_text("abc[wt 0.5]de[charaFace A 3]f", {});
    REQUIRE(t.commands.size() == 2);
    CHECK_EQ(t.commands[0].cmd.name, std::string("wt"));
    CHECK_EQ(t.commands[0].glyph, static_cast<std::size_t>(3));
    CHECK_EQ(t.commands[1].glyph, static_cast<std::size_t>(5));
    CHECK_EQ(t.plain(), std::string("abcdef"));
}

TEST(rich_unicode_escape_runs) {
    const RichText t = parse_rich_text("x u2605u2606 y user", {});
    CHECK_EQ(t.plain(), std::string("x ★☆ y user"));
}

TEST(reveal_times_with_speed) {
    const RichText t = parse_rich_text("ab[speed 40]cd", {});
    const auto times = reveal_times(t, 45);
    REQUIRE(times.size() == 4);
    CHECK_NEAR(times[1], 2.0 / 45, 1e-9);
    CHECK_NEAR(times[3], 2.0 / 45 + 2.0 / 90, 1e-9);  // speed 40 = double rate
    const auto instant = reveal_times(t, 0);
    CHECK_NEAR(instant[3], 0, 1e-12);
}
