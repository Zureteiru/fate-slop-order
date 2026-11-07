// Player: script-driven durations, waits, typewriter, choices, branches, Back, focus, audio.
#include "check.h"
#include "drive.h"
#include "vnr/player.h"

using namespace vnr;

namespace {
struct Run {
    Diagnostics diag;
    Program prog;
    Player player;
    Run(const char* src, Settings s = {}) : prog(compile(src, s.text, diag)), player(prog, s) {}
};
bool has_kind(const std::vector<AudioEvent>& ev, AudioEvent::Kind k) {
    for (const auto& e : ev)
        if (e.kind == k) return true;
    return false;
}
}  // namespace

TEST(player_chara_fade_uses_script_duration) {
    Run r("[charaSet A 98001000 1 Mash]\n[charaFadein A 0.1 1]\n[wait chara]\n＠Mash\nHi.\n[k]\n");
    const Element* a = r.player.stage().elements.count("A") ? &r.player.stage().elements.at("A") : nullptr;
    REQUIRE(a);
    CHECK(a->shown);
    CHECK_NEAR(a->opacity.duration, 0.1, 1e-9);
    CHECK(a->pos.value() == (Vec2{0, 0}));  // slot 1
    const double t = drive::time_until(r.player, [&] { return r.player.status() != Status::Waiting; });
    CHECK_NEAR(t, 0.1, 2 * drive::kDt);
}

TEST(player_wt_and_wait_fade) {
    Run r("[fadeout black 0.4]\n[wait fade]\n[wt 1.0]\n＠\nText.\n[k]\n");
    const double t = drive::time_until(r.player, [&] { return drive::at_say(r.player, 4); });
    CHECK_NEAR(t, 1.4, 3 * drive::kDt);
    CHECK_NEAR(r.player.stage().fade.opacity.value(), 1.0, 1e-6);
}

TEST(player_fadein_reveals_from_cover) {
    Run r("[fadein black 1]\n[wt 0.5]\n＠\nx\n[k]\n");
    CHECK_NEAR(r.player.stage().fade.opacity.value(), 1.0, 1e-6);  // starts covered
    drive::time_until(r.player, [&] { return drive::at_say(r.player, 3); });
    CHECK(r.player.stage().fade.opacity.value() > 0.4f);  // linear: half way after 0.5 s
    CHECK(r.player.stage().fade.opacity.value() < 0.6f);
}

TEST(player_typewriter_45cps_click_completes) {
    Run r("＠Mash\nabcdefghi\n[k]\n");  // 9 glyphs at 45 cps = 0.2 s
    CHECK(r.player.status() == Status::Typing);
    const double t = drive::time_until(r.player, [&] { return r.player.status() == Status::AwaitAdvance; });
    CHECK_NEAR(t, 0.2, 2 * drive::kDt);

    Run r2("＠Mash\nabcdefghi\n[k]\n＠Mash\nnext\n[k]\n");
    r2.player.update(0.05);
    CHECK(r2.player.text().shown < 9);
    r2.player.advance();  // first tap completes the line
    CHECK(r2.player.status() == Status::AwaitAdvance);
    CHECK_EQ(r2.player.text().shown, static_cast<std::size_t>(9));
    r2.player.advance();  // second tap moves on
    CHECK(drive::at_say(r2.player, 4));
}

TEST(player_inline_wt_pauses_typing) {
    Run r("＠Mash\nab[wt 1.0]cd\n[k]\n");
    const double t = drive::time_until(r.player, [&] { return r.player.status() == Status::AwaitAdvance; });
    CHECK_NEAR(t, 4.0 / 45 + 1.0, 3 * drive::kDt);
}

TEST(player_choice_keeps_picked_option_state) {
    const char* src =
        "[scene 100]\n"
        "？1：Left\n[scene 1]\n？2：Right\n[scene 2]\n？！\n"
        "＠\nafter\n[k]\n";
    for (std::size_t pick : {0u, 1u}) {
        Run r(src);
        REQUIRE(r.player.status() == Status::AwaitChoice);
        REQUIRE(r.player.pending_choice());
        CHECK_EQ(r.player.pending_choice()->options.size(), static_cast<std::size_t>(2));
        r.player.choose(pick);
        CHECK(drive::at_say(r.player, 7));
        CHECK_EQ(r.player.stage().background, std::string(pick == 0 ? "1" : "2"));
    }
}

TEST(player_branch_jumps_at_runtime) {
    Run r("[branch skipIt]\n＠\nskipped\n[k]\n[label skipIt]\n＠\nreached\n[k]\n");
    REQUIRE(r.player.text().say);
    CHECK_EQ(r.player.text().say->text.plain(), std::string("reached"));
}

TEST(player_master_branch_by_gender) {
    const char* src = "[masterBranch m f]\n[label m]\n＠\nhe\n[k]\n[branch end]\n[label f]\n＠\nshe\n[k]\n[label end]\n";
    Settings fem;
    fem.text.gender = Gender::Female;
    Run male(src), female(src, fem);
    CHECK_EQ(male.player.text().say->text.plain(), std::string("he"));
    CHECK_EQ(female.player.text().say->text.plain(), std::string("she"));
}

TEST(player_back_restores_stage_and_backlog) {
    Run r("[scene 1]\n＠\none\n[k]\n[scene 2]\n＠\ntwo\n[k]\n");
    r.player.advance();
    r.player.advance();
    REQUIRE(drive::at_say(r.player, 6));
    CHECK_EQ(r.player.stage().background, std::string("2"));
    CHECK_EQ(r.player.backlog().size(), static_cast<std::size_t>(2));
    CHECK(r.player.back());
    CHECK(drive::at_say(r.player, 2));
    CHECK_EQ(r.player.stage().background, std::string("1"));
    CHECK_EQ(r.player.backlog().size(), static_cast<std::size_t>(1));
    CHECK(r.player.status() == Status::AwaitAdvance);  // shown in full after Back
    CHECK(!r.player.back());  // nothing before the first line
}

TEST(player_talk_focus_dims_others) {
    Run r(
        "[charaSet A 1 0 Mash]\n[charaSet D 2 0 Lev]\n"
        "[charaFadein A 0 2]\n[charaFadein D 0 0]\n"
        "＠Lev\nNo more small talk.\n[k]\n"
        "＠\nNarration.\n[k]\n");
    const auto& els = r.player.stage().elements;
    CHECK(els.at("A").dimmed);
    CHECK(!els.at("D").dimmed);
    r.player.advance();
    r.player.advance();
    CHECK(!r.player.stage().elements.at("A").dimmed);  // narration: nobody in focus
}

TEST(player_message_off_until_next_line) {
    Run r("＠\none\n[k]\n[messageOff]\n[wt 0.5]\n＠\ntwo\n[k]\n");
    r.player.advance();
    r.player.advance();
    CHECK(r.player.status() == Status::Waiting);
    CHECK(!r.player.stage().message_visible);
    drive::time_until(r.player, [&] { return drive::at_say(r.player, 6); });
    CHECK(r.player.stage().message_visible);
    CHECK(r.player.text().visible);
}

TEST(player_audio_events) {
    Run r("[bgm BGM_EVENT_2 0.1]\n[se ad13]\n[seLoop ad271 0.5 0.8]\n[voice 100100_0_A010]\n＠Mash\nHi\n[k]\n[bgmStop BGM_EVENT_2 2.0]\n＠\nx\n[k]\n");
    auto ev = r.player.drain_audio();
    REQUIRE(ev.size() == 4);
    CHECK(ev[0].kind == AudioEvent::Kind::BgmPlay);
    CHECK_NEAR(ev[0].fade, 0.1, 1e-9);
    CHECK(ev[1].kind == AudioEvent::Kind::SePlay);
    CHECK(ev[2].loop);
    CHECK_NEAR(ev[2].fade, 0.5, 1e-9);
    CHECK_NEAR(ev[2].volume, 0.8, 1e-6);
    CHECK(ev[3].kind == AudioEvent::Kind::VoicePlay);
    CHECK_EQ(r.player.backlog().back().voice_name, std::string("100100_0_A010"));
    r.player.advance();
    r.player.advance();
    ev = r.player.drain_audio();
    CHECK(!has_kind(ev, AudioEvent::Kind::VoiceStop));  // the voice plays on past its line
    CHECK(has_kind(ev, AudioEvent::Kind::BgmStop));
    CHECK(r.player.bgm().empty());
    r.player.advance();
    r.player.back();
    CHECK(has_kind(r.player.drain_audio(), AudioEvent::Kind::VoiceStop));  // Back stops it
}

TEST(player_voice_plays_where_it_appears) {
    // An NP shout followed by a wait plays at once and is not the next line's voice.
    Run r("[voice NP_700900_1]\n[wt 1.5]\n＠A\nx\n[k]\n[voice 1_a]\n＠B\ny\n[k]\n");
    auto ev = r.player.drain_audio();
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == AudioEvent::Kind::VoicePlay);
    CHECK_EQ(ev[0].name, std::string("NP_700900_1"));
    CHECK(r.player.status() == Status::Waiting);
    drive::time_until(r.player, [&] { return drive::at_say(r.player, 3); });
    CHECK(r.player.backlog().back().voice_name.empty());
    CHECK(r.player.drain_audio().empty());  // nothing replayed at the line
    r.player.advance();
    r.player.advance();
    // A [voice] right before a line is that line's voice (for the backlog).
    CHECK(drive::at_say(r.player, 7));
    CHECK_EQ(r.player.backlog().back().voice_name, std::string("1_a"));
    ev = r.player.drain_audio();
    REQUIRE(ev.size() == 1);
    CHECK_EQ(ev[0].name, std::string("1_a"));
}

TEST(player_wait_voice_after_k_waits) {
    Run r("[voice 1_a]\n＠\nv\n[k]\n[wait voice]\n＠\nnext\n[k]\n");
    r.player.advance();
    r.player.advance();
    CHECK(r.player.status() == Status::Waiting);  // the voice outlives its line
    r.player.update(1.0);
    CHECK(r.player.status() == Status::Waiting);
    r.player.voice_finished();
    r.player.update(drive::kDt);
    CHECK(drive::at_say(r.player, 6));

    Run capped("[voice 1_a]\n＠\nv\n[k]\n[wait voice]\n＠\nnext\n[k]\n");
    capped.player.advance();
    capped.player.advance();
    const double t = drive::time_until(capped.player, [&] { return drive::at_say(capped.player, 6); });
    CHECK_NEAR(t, 12.0, 3 * drive::kDt);  // never hangs: the cap releases it
}

TEST(player_voice_stop_and_inline_voice) {
    Run r("[voice 1_a]\n＠\nv[voice 1_b]w\n[k]\n[voiceStop]\n＠\nnext\n[k]\n");
    r.player.advance();  // finish typing: runs the inline [voice]
    auto ev = r.player.drain_audio();
    REQUIRE(ev.size() == 2);
    CHECK_EQ(ev[1].name, std::string("1_b"));
    CHECK_EQ(r.player.backlog().back().voice_name, std::string("1_b"));
    r.player.advance();
    CHECK(has_kind(r.player.drain_audio(), AudioEvent::Kind::VoiceStop));
}

TEST(player_wait_voice_and_auto) {
    Settings s;
    s.auto_mode = true;
    s.auto_seconds = 1.0;
    Run r("[voice 1_a]\n＠\nv\n[k]\n＠\nnext\n[k]\n", s);
    drive::time_until(r.player, [&] { return r.player.status() == Status::AwaitAdvance; });
    r.player.update(2.0);  // voice still playing: auto waits
    CHECK(drive::at_say(r.player, 2));
    r.player.voice_finished();
    r.player.update(drive::kDt);
    CHECK(drive::at_say(r.player, 5));
}

TEST(player_flash_rise_and_flashout) {
    Run r("[flashin once 1 0.4 FFFFFFFF 00000000]\n[wt 0.5]\n[flashout 0.7]\n[wait flash]\n[wt 0.5]\n＠\nx\n[k]\n");
    CHECK_NEAR(r.player.stage().flash.rise, 1.0, 1e-9);
    CHECK_NEAR(r.player.stage().flash.duration, 0.4, 1e-9);
    const double t = drive::time_until(r.player, [&] { return drive::at_say(r.player, 6); });
    CHECK_NEAR(t, 1.7, 3 * drive::kDt);
}

TEST(player_camera_and_effects) {
    Run r("[charaSet A 1 0 X]\n[cameraMove 5.0 0,0 1.5]\n[charaEffect A bit_talk_41]\n[effect bit_chapterstart00]\n[wait effect]\n＠\nx\n[k]\n[charaEffectStop A bit_talk_41]\n＠\ny\n[k]\n");
    const auto& st = r.player.stage();
    CHECK_NEAR(st.camera.scale.to, 1.5, 1e-6);
    CHECK_NEAR(st.camera.scale.duration, 5.0, 1e-9);
    REQUIRE(st.effects.size() == 1);
    CHECK(st.effects[0].layer == EffectLayer::Chara);
    CHECK(st.title_card.has_value());
    const double t = drive::time_until(r.player, [&] { return drive::at_say(r.player, 6); });
    CHECK_NEAR(t, 2.6, 3 * drive::kDt);
    r.player.advance();
    r.player.advance();
    CHECK(r.player.stage().effects.empty());
}

namespace {
const Element& el(const Player& p, const char* code) { return p.stage().elements.at(code); }
bool waiting_on(const Player& p, const char* text) {
    const auto v = p.text();
    return p.status() == Status::AwaitAdvance && v.say && v.say->text.plain() == text;
}
}  // namespace

TEST(player_sub_layers_show_only_after_subRenderFadein) {
    Run r("[charaSet A 98001000 1 Mash]\n[charaSet S 98004000 1 Sub]\n[charaFadein A 0 0,0]\n[subCameraOn 4]\n"
          "[charaLayer S sub #A]\n[charaFadein S 0 50,100]\n＠\none\n[k]\n"
          "[subRenderFadein #A 0.4 400,-150]\n＠\ntwo\n[k]\n"
          "[subRenderFadeout #A 0.5]\n＠\nthree\n[k]\n");
    CHECK(r.diag.unknown_commands.empty());
    REQUIRE(drive::time_until(r.player, [&] { return waiting_on(r.player, "one"); }) >= 0);
    const Stage& st = r.player.stage();
    CHECK(!st.placement(el(r.player, "S")).visible);  // on a layer nobody showed yet
    CHECK(st.placement(el(r.player, "A")).visible);
    CHECK_NEAR(st.placement(el(r.player, "A")).order, kMainLayerOrder, 1e-6);
    r.player.advance();
    REQUIRE(drive::time_until(r.player, [&] { return waiting_on(r.player, "two"); }) >= 0);
    drive::time_until(r.player, [] { return false; }, 0.5);
    LayerPlacement p = st.placement(el(r.player, "S"));
    CHECK(p.visible);
    CHECK_NEAR(p.opacity, 1, 1e-4);
    CHECK_NEAR(p.offset.x, 400, 1e-4);
    CHECK_NEAR(p.offset.y, 0, 1e-4);   // no mask: Fate Café moves plain layers along x only
    CHECK_NEAR(p.order, 1, 1e-6);      // #A, placed: under the main characters
    CHECK(!p.scale_position);
    r.player.advance();
    REQUIRE(drive::time_until(r.player, [&] { return waiting_on(r.player, "three"); }) >= 0);
    CHECK(st.placement(el(r.player, "S")).visible);  // fading out
    drive::time_until(r.player, [] { return false; }, 0.6);
    CHECK(!st.placement(el(r.player, "S")).visible);
}

TEST(player_mask_edge_layers_move_as_a_group_and_subCameraOff_hides_them) {
    Run r("[charaSet S 98004000 1 Sub]\n[charaLayer S sub #B]\n[charaFadein S 0 100,50]\n"
          "[subCameraFilter #B maskEdge cut359_mask16 5 255,255,255,255 0]\n[subRenderScale #B 2]\n"
          "[subRenderFadeinFSSideR #B 0 -400,-150]\n＠\na\n[k]\n[subCameraOff]\n＠\nb\n[k]\n"
          "[charaLayer S normal]\n＠\nc\n[k]\n");
    CHECK(r.diag.unknown_commands.empty());
    REQUIRE(drive::time_until(r.player, [&] { return waiting_on(r.player, "a"); }) >= 0);
    const Stage& st = r.player.stage();
    LayerPlacement p = st.placement(el(r.player, "S"));
    CHECK(p.visible);
    CHECK(p.scale_position);
    CHECK_NEAR(p.scale, 2, 1e-6);
    CHECK_NEAR(p.offset.x, -400, 1e-4);
    CHECK_NEAR(p.offset.y, -150, 1e-4);
    CHECK_NEAR(p.order, kSurfaceLayerOrder + 2, 1e-6);  // #B, a maskEdge surface: above the main stage
    CHECK_EQ(st.sub_layers.at("#B").mask, "cut359_mask16");
    r.player.advance();
    REQUIRE(drive::time_until(r.player, [&] { return waiting_on(r.player, "b"); }) >= 0);
    CHECK(!st.placement(el(r.player, "S")).visible);  // subCameraOff resets every layer
    r.player.back();  // Back restores the layer with the rest of the stage
    REQUIRE(drive::time_until(r.player, [&] { return waiting_on(r.player, "a"); }) >= 0);
    CHECK(r.player.stage().placement(el(r.player, "S")).visible);
    r.player.advance();
    REQUIRE(drive::time_until(r.player, [&] { return waiting_on(r.player, "b"); }) >= 0);
    r.player.advance();
    REQUIRE(drive::time_until(r.player, [&] { return waiting_on(r.player, "c"); }) >= 0);
    CHECK(r.player.stage().placement(el(r.player, "S")).visible);  // back on the main stage
}

TEST(player_chara_special_effect_keeps_mode_and_duration) {
    Run r("[charaSet A 98001000 1 Mash]\n[charaFadein A 0 1]\n[charaSpecialEffect A flashErasure 0 0.5]\n[wait chara]\n"
          "＠Mash\nGone.\n[k]\n");
    const Element& a = r.player.stage().elements.at("A");
    REQUIRE(a.special);
    CHECK(!a.special->incoming);
    CHECK(!a.special->reverse);
    CHECK(!a.special->radial);
    CHECK_NEAR(a.special->progress.duration, 0.5, 1e-9);
    CHECK_NEAR(a.opacity.value(), 1, 1e-6);  // the mask erases, not the opacity
    const double t = drive::time_until(r.player, [&] { return r.player.status() != Status::Waiting; });
    CHECK_NEAR(t, 0.5, 2 * drive::kDt);  // [wait chara] waits for it
    drive::time_until(r.player, [&] { return !r.player.stage().elements.at("A").special; }, 1);
    const Element& done = r.player.stage().elements.at("A");
    CHECK(!done.shown);
    CHECK_NEAR(done.opacity.value(), 0, 1e-6);
}

TEST(player_chara_special_effect_modes) {
    Run r("[charaSet A 98001000 1 Mash]\n[charaSet B 98001000 1 Mash]\n[charaSet C 98001000 1 Mash]\n"
          "[charaSpecialEffect A appearance 0,-10]\n[charaSpecialEffect B erasureReverse]\n[charaSpecialEffect C wipeTime]\n"
          "＠Mash\nHi.\n[k]\n");
    const auto& els = r.player.stage().elements;
    const Element& a = els.at("A");
    REQUIRE(a.special && els.at("B").special && els.at("C").special);
    CHECK(a.special->incoming);
    CHECK(a.shown);
    CHECK_NEAR(a.opacity.value(), 1, 1e-6);
    CHECK_NEAR(a.special->progress.duration, 2, 1e-9);  // Fate Café's default
    CHECK(els.at("B").special->reverse);
    CHECK(!els.at("B").special->incoming);
    CHECK(els.at("C").special->radial);
}

TEST(player_chara_roll_axis_y_is_a_flip_not_a_spin) {
    Run r("[charaSet A 98001000 1 Mash]\n[charaFadein A 0 1]\n[charaRollAxis A y 180 0.4 0]\n[charaRollAxis A z 30 0.2 0]\n"
          "＠Mash\nHi.\n[k]\n");
    const Element& a = r.player.stage().elements.at("A");
    CHECK_NEAR(a.roll_y.to, 180, 1e-6);
    CHECK_NEAR(a.roll_y.duration, 0.4, 1e-9);
    CHECK_NEAR(a.roll.to, 30, 1e-6);  // other axes keep the ordinary roll
}
