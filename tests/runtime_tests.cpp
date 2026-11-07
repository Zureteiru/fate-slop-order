// Runtime: JSON, images, asset cache, text layout, and golden draw lists from the App.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "check.h"
#include "drive.h"
#include "fakes/fake_platform.h"
#include "png_writer.h"
#include "vnr/rt/app.h"
#include "vnr/rt/json.h"
#include "vnr/rt/layer_mask.h"
#include "vnr/rt/ui_skin.h"
#include "vnr/rt/effects.h"
#include "vnr/rt/special_mask.h"
#include "vnr/rt/title_card.h"
#include "vnr/rt/wipe_mask.h"
#include "vnr/script_lines.h"

#ifndef VNR_FIXTURE_DIR
#error "VNR_FIXTURE_DIR must point at .cache/scripts"
#endif

using namespace vnr;
using namespace vnr::rt;

namespace {
const std::string kDir = VNR_FIXTURE_DIR;

struct Env {
    fakes::FakeHttp http;
    fakes::MemFiles files;
    fakes::MemLog log;
    Platform platform{http, files, log, fakes::bytes_of(kDir + "/MPLUS1p-Regular.ttf"), 0};
    Env() {
        http.handler = [](const HttpRequest& r) { return fakes::serve_fixture(kDir, r); };
    }
};

bool near(Vec2 a, Vec2 b, float eps = 0.01f) { return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps; }

// Runs the App until it is playing, then taps through until `done` (choosing option 0).
bool play_until(App& app, const std::function<bool()>& done) {
    for (int i = 0; i < 200 && app.phase() != App::Phase::Playing; ++i) app.update(1.0 / 60, {});
    if (app.phase() != App::Phase::Playing) return false;
    for (int guard = 0; guard < 2000000; ++guard) {
        if (done()) {
            app.update(0.5, {});  // let fades started at this line finish, refresh the draw list
            return true;
        }
        const Player* p = app.player();
        if (!p) return false;
        std::vector<InputEvent> in;
        if (p->pending_choice()) in.push_back({InputEvent::Kind::Choose, {}, 0});
        else if (p->status() == Status::Typing || p->status() == Status::AwaitAdvance) in.push_back({InputEvent::Kind::Advance, {}, 0});
        app.update(in.empty() ? drive::kDt : 0, in);
        if (app.phase() == App::Phase::Finished) return done();
    }
    return false;
}
}  // namespace

TEST(json_parses_atlas_shapes) {
    const auto j = Json::parse(R"([{"id":98001000,"faceX":384,"extendData":{"faceSizeRect":[300,350]},"name":"Mash マシュ","ok":true,"n":null}])");
    REQUIRE(j.has_value());
    CHECK_EQ(j->size(), static_cast<std::size_t>(1));
    const Json& row = (*j)[0];
    CHECK_EQ(row["id"].integer(), 98001000);
    CHECK_EQ(row["extendData"]["faceSizeRect"][1].integer(), 350);
    CHECK_EQ(row["name"].string(), std::string("Mash マシュ"));
    CHECK(row["ok"].boolean());
    CHECK(row["missing"]["deeper"].type() == Json::Type::Null);
    CHECK(!Json::parse("{\"a\":}").has_value());
    CHECK(!Json::parse("[1,2").has_value());
}

TEST(images_decode_and_png_header_size) {
    int w = 0, h = 0;
    CHECK(png_size(fakes::bytes_of(kDir + "/body_98001000.bin"), w, h));
    CHECK_EQ(w, 1024);
    CHECK_EQ(h, 1024);
    Image img;
    REQUIRE(decode_image(fakes::bytes_of(kDir + "/98001000_merged.png"), img));
    CHECK_EQ(img.width, 1024);
    CHECK_EQ(img.height, 2560);
    CHECK_EQ(img.rgba.size(), static_cast<std::size_t>(1024) * 2560 * 4);
    CHECK(!decode_image({1, 2, 3}, img));
}

TEST(asset_cache_downloads_then_reuses_disk) {
    Env env;
    TextureStore tex;
    const std::string url = background_url("10310", Region::NA);
    {
        AssetCache cache(env.platform, tex);
        CHECK(cache.request_texture(url) == AssetState::Loading);
        cache.poll();
        REQUIRE(cache.texture(url));
        CHECK_EQ(cache.texture(url)->width, 1024);
        CHECK(cache.request_bytes("https://static.atlasacademy.io/NA/nothing.txt") == AssetState::Loading);
        cache.poll();
        CHECK(cache.state("https://static.atlasacademy.io/NA/nothing.txt") == AssetState::Failed);
    }
    CHECK(env.files.data.count("static.atlasacademy.io/NA/Back/back10310.png"));
    const std::size_t requests = env.http.requests.size();
    AssetCache again(env.platform, tex);
    CHECK(again.request_texture(url) == AssetState::Ready);  // straight from disk
    CHECK_EQ(env.http.requests.size(), requests);
    CHECK_EQ(AssetCache::cache_key("https://api.atlasacademy.io/raw/NA/svtScript?charaId=1&charaId=2", ""),
             std::string("api.atlasacademy.io/raw/NA/svtScript_charaId_1_charaId_2"));
    CHECK_EQ(AssetCache::cache_key("https://x.io/../a.png", "0-31"), std::string("x.io/_/a.png.range_0-31"));
}

TEST(text_wraps_and_respects_typewriter) {
    Env env;
    TextureStore tex;
    GlyphAtlas atlas(tex, env.platform.font, 0);
    REQUIRE(atlas.ok());
    const RichText t = parse_rich_text("Welcome to Special Organization, Chaldea. I'm the director, Olga Marie Animusphere.", {});
    TextStyleBox box;
    box.box = {395, 513, 554, 88};
    DrawList all, part;
    const TextLayoutResult r = draw_text(atlas, t, t.glyph_count, box, all);
    CHECK(r.total_lines >= 2);
    float max_x = 0;
    for (const Quad& q : all.quads) max_x = std::max(max_x, q.pos[1].x);
    CHECK(max_x <= 395 + 554 + 1);
    draw_text(atlas, t, 7, box, part);  // "Welcome" = 7 visible glyphs
    CHECK_EQ(part.quads.size(), static_cast<std::size_t>(7));
    const RichText ruby = parse_rich_text("[#教科書:テキスト]に", {});
    DrawList rq;
    draw_text(atlas, ruby, ruby.glyph_count, box, rq);
    CHECK_EQ(rq.quads.size(), static_cast<std::size_t>(4 + 4));  // 4 base glyphs + 4 ruby glyphs
}

TEST(app_golden_fuyuki_line56_mash_face) {
    Env env;
    AppConfig cfg;
    cfg.script_ids = {"0100000010", "0100000011"};
    App app(env.platform, cfg);
    REQUIRE(play_until(app, [&] { return app.script_index() == 1 && drive::at_say(*app.player(), 56); }));
    // face 12 of a 256px grid, 4 per row: col 3, row 2 -> y = 768 + 512 (Atlas DB maths)
    const Quad* face = nullptr;
    const Quad* body = nullptr;
    for (const Quad& q : app.draw_list().quads) {
        if (q.src.x == 768 && q.src.y == 1280 && q.src.w == 256 && q.src.h == 256) face = &q;
        if (q.src.x == 0 && q.src.y == 0 && q.src.w == 1024 && q.src.h == 1024 && q.texture != 0) body = &q;
    }
    REQUIRE(face);
    REQUIRE(body);
    // body origin = ((1344-1024)/2 + offsetX 0, -offsetY 125); slot 1 = centre; face at (faceX 384, faceY 149)
    CHECK(near(body->pos[0], {160, -125}));
    CHECK(near(face->pos[0], {160 + 384, -125 + 149}));
    CHECK_NEAR(face->color.r, 1.0, 1e-6);
    // the text box is drawn with glyphs
    std::size_t glyphs = 0;
    for (const Quad& q : app.draw_list().quads)
        if (q.texture == 1) ++glyphs;  // the glyph atlas is the first texture the App creates
    CHECK(glyphs > 10);
    CHECK(env.log.lines.size() < 200);  // missing fixture images are logged, not fatal
}

TEST(app_golden_fuyuki_line522_slots_and_dimming) {
    Env env;
    AppConfig cfg;
    cfg.script_ids = {"0100000010", "0100000011"};
    App app(env.platform, cfg);
    REQUIRE(play_until(app, [&] { return app.script_index() == 1 && drive::at_say(*app.player(), 522); }));
    const Quad* mash = nullptr;
    const Quad* lev = nullptr;
    for (const Quad& q : app.draw_list().quads) {
        if (q.texture == 0 || q.src.y != 0 || q.src.w != 1024) continue;
        if (near(q.pos[0], {160 + 256, -125})) mash = &q;          // slot 2, offsetY 125
        if (near(q.pos[0], {160 - 2 - 256, -138})) lev = &q;        // slot 0, offsetX -2, offsetY 138
    }
    REQUIRE(mash);
    REQUIRE(lev);
    CHECK_NEAR(mash->color.r, 0.55, 1e-6);  // listening: dimmed
    CHECK_NEAR(lev->color.r, 1.0, 1e-6);    // speaking
}

TEST(app_pointer_picks_choice) {
    Env env;
    AppConfig cfg;
    cfg.script_ids = {"0100000011"};
    App app(env.platform, cfg);
    REQUIRE(play_until(app, [&] { return app.player()->pending_choice() != nullptr; }));
    const auto rects = choice_rects(*app.player()->pending_choice(), app.ui(), app.player()->text().visible);
    REQUIRE(rects.size() == 2);
    app.update(0, {{InputEvent::Kind::Pointer, {rects[1].x + 10, rects[1].y + 10}, 0}});
    CHECK(app.player()->pending_choice() == nullptr);
    CHECK_EQ(app.player()->backlog().size(), static_cast<std::size_t>(2));
}

namespace {
bool same(const Rect& a, const Rect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }
const Quad* find_src(const DrawList& d, const Rect& src) {
    for (const Quad& q : d.quads)
        if (q.texture != 0 && same(q.src, src)) return &q;
    return nullptr;
}
// Stand-ins for the user's UI sheets: right sizes, flat colour.
void add_sheets(fakes::MemFiles& user) {
    user.data["ui/003.png"] = testpng::solid(2048, 2048, 0x203060ffu);
    user.data["ui/030.png"] = testpng::solid(256, 256, 0xffffffffu);
}
}  // namespace

TEST(ui_skin_missing_or_wrong_size_falls_back) {
    TextureStore textures;
    fakes::MemLog log;
    fakes::MemFiles none;
    CHECK(!load_ui_skin(none, textures, log).has_talk());
    CHECK(log.lines.empty());  // missing sheets are normal, not an error

    fakes::MemFiles wrong;
    wrong.data["ui/003.png"] = testpng::solid(64, 64, 0xffffffffu);
    wrong.data["ui/030.png"] = {1, 2, 3};
    const UiSkin s = load_ui_skin(wrong, textures, log);
    CHECK(!s.has_talk());
    CHECK(!s.has_next());
    CHECK_EQ(log.lines.size(), static_cast<std::size_t>(2));

    fakes::MemFiles good;
    add_sheets(good);
    const UiSkin ok = load_ui_skin(good, textures, log);
    CHECK(ok.has_talk());
    CHECK(ok.has_next());
}

TEST(app_skin_draws_text_box_and_name_tab_without_next_marker) {
    Env env;
    fakes::MemFiles user;
    add_sheets(user);
    env.platform.user = &user;
    AppConfig cfg;
    cfg.script_ids = {"0100000010", "0100000011"};
    App app(env.platform, cfg);
    REQUIRE(play_until(app, [&] { return app.script_index() == 1 && drive::at_say(*app.player(), 56); }));
    const UiLayout& ui = app.ui();
    // text box: the sheet piece at native width, 12 above the bottom of the game's view (video)
    const Quad* box = find_src(app.draw_list(), skin::kTextBox);
    REQUIRE(box);
    CHECK(near(box->pos[0], {179.5f, 425}));
    CHECK(near(box->pos[2], {179.5f + 984, kViewTop + kViewHeight - 12}));
    // name tab ("???"): pointed end kept, stretched to the minimum width
    const Quad* tip = find_src(app.draw_list(), {skin::kNameTab.x, skin::kNameTab.y, skin::kNameTabLeft, skin::kNameTab.h});
    REQUIRE(tip);
    CHECK(near(tip->pos[0], {ui.name_plate.x, ui.name_plate.y}));
    CHECK(tip > box);                        // the tab is drawn over the box's top-left corner
    CHECK(tip->pos[0].x < box->pos[0].x);    // its point sticks out left of the box
    const Quad* fade = find_src(app.draw_list(), {skin::kNameTab.x + skin::kNameTab.w - skin::kNameTabRight, skin::kNameTab.y,
                                                  skin::kNameTabRight, skin::kNameTab.h});
    REQUIRE(fade);
    CHECK(fade->pos[1].x >= ui.name_plate.x + ui.name_plate.w - 0.01f);
    // waiting for a tap: no spinning "next" marker (left out on request)
    for (int i = 0; i < 100 && app.player()->status() != Status::AwaitAdvance; ++i) app.update(0.1, {});
    REQUIRE(app.player()->status() == Status::AwaitAdvance);
    bool marker = false;
    for (int f = 0; f < skin::kNextFrames; ++f) marker = marker || find_src(app.draw_list(), skin::next_frame(f)) != nullptr;
    CHECK(!marker);
}

TEST(app_skin_draws_choice_frames) {
    Env env;
    fakes::MemFiles user;
    add_sheets(user);
    env.platform.user = &user;
    AppConfig cfg;
    cfg.script_ids = {"0100000011"};
    App app(env.platform, cfg);
    REQUIRE(play_until(app, [&] { return app.player()->pending_choice() != nullptr; }));
    const auto rects = choice_rects(*app.player()->pending_choice(), app.ui(), app.player()->text().visible);
    REQUIRE(rects.size() == 2);
    // each choice: three slices of the frame, corners unscaled, covering the hit rect exactly
    const Rect left{skin::kChoice.x, skin::kChoice.y, skin::kChoiceEdge, skin::kChoice.h};
    std::size_t lefts = 0;
    for (const Quad& q : app.draw_list().quads)
        if (q.texture != 0 && same(q.src, left)) {
            ++lefts;
            const Rect& r = rects[lefts - 1];
            CHECK(near(q.pos[0], {r.x, r.y}));
            CHECK(near(q.pos[3], {r.x, r.y + r.h}));
        }
    CHECK_EQ(lefts, static_cast<std::size_t>(2));
    // no "next" marker while a choice is pending
    for (int f = 0; f < skin::kNextFrames; ++f) CHECK(find_src(app.draw_list(), skin::next_frame(f)) == nullptr);
}

TEST(app_without_sheets_uses_plain_boxes) {
    Env env;
    AppConfig cfg;
    cfg.script_ids = {"0100000010", "0100000011"};
    App app(env.platform, cfg);
    REQUIRE(play_until(app, [&] { return app.script_index() == 1 && drive::at_say(*app.player(), 56); }));
    CHECK(find_src(app.draw_list(), skin::kTextBox) == nullptr);
    bool panel = false;
    for (const Quad& q : app.draw_list().quads)
        if (q.texture == 0 && near(q.pos[0], {app.ui().panel.x, app.ui().panel.y})) panel = true;
    CHECK(panel);
}

TEST(app_prefers_user_game_font_and_falls_back) {
    {
        Env env;
        fakes::MemFiles user;
        user.data[kGameFontKey] = fakes::bytes_of(kDir + "/MPLUS1p-Regular.ttf");  // stand-in for the game font
        env.platform.user = &user;
        env.platform.font.clear();  // no OS font: text must come from the user font
        AppConfig cfg;
        cfg.script_ids = {"0100000010", "0100000011"};
        App app(env.platform, cfg);
        CHECK(app.game_font());
        REQUIRE(play_until(app, [&] { return app.script_index() == 1 && drive::at_say(*app.player(), 56); }));
        std::size_t glyphs = 0;
        for (const Quad& q : app.draw_list().quads)
            if (q.texture == 1) ++glyphs;
        CHECK(glyphs > 10);
    }
    {
        Env env;
        fakes::MemFiles user;
        auto truncated = fakes::bytes_of(kDir + "/MPLUS1p-Regular.ttf");
        truncated.resize(2000);  // tables point past the end: must be rejected without reading out of bounds
        user.data[kGameFontKey] = truncated;
        CHECK(!GlyphAtlas::usable({'O', 'T', 'T', 'O', 0, 0}, 0));
        CHECK(!GlyphAtlas::usable(truncated, 0));
        env.platform.user = &user;
        AppConfig cfg;
        cfg.script_ids = {"0100000011"};
        App app(env.platform, cfg);
        CHECK(!app.game_font());
        bool logged = false;
        for (const auto& l : env.log.lines) logged = logged || l.find("could not be opened") != std::string::npos;
        CHECK(logged);
    }
}

TEST(text_wraps_at_the_game_width) {
    // From the NA video: the script's second line fits on one line in the game's box (it wrapped
    // early when the text area was narrower than the game's).
    const std::string game = drive::read_file(kDir + "/../user/FOT-SkipStd-B.otf");  // local copy, never committed
    const std::vector<std::uint8_t> font = game.empty() ? fakes::bytes_of(kDir + "/MPLUS1p-Regular.ttf")
                                                        : std::vector<std::uint8_t>(game.begin(), game.end());
    TextureStore tex;
    GlyphAtlas atlas(tex, font, 0);
    REQUIRE(atlas.ok());
    const UiLayout ui;
    TextStyleBox b;
    b.box = ui.text;
    b.px = ui.text_px;
    b.line_height = ui.line_height;
    b.max_lines = 99;
    const RichText t = parse_rich_text(
        "You were asleep, but I don't see why you'd sleep in the[sr]hall. Can you not sleep unless you're on a hard bed?", {});
    DrawList out;
    if (!game.empty()) CHECK_EQ(draw_text(atlas, t, t.glyph_count, b, out).total_lines, 2);
    // and a long unbroken line still wraps rather than running past the box
    const RichText long_line = parse_rich_text(
        "But please don't be discouraged because you're a public applicant. We need all of you for this mission.", {});
    CHECK_EQ(draw_text(atlas, long_line, long_line.glyph_count, b, out).total_lines, 2);
}

// ---- story buttons (LOG / AUTO) and the backlog ----

namespace {
Vec2 centre(const Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }
bool contains(const Rect& outer, const Rect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}
InputEvent ev(InputEvent::Kind k, Vec2 p = {}) { return {k, p, 0}; }
InputEvent scroll_ev(float dy) { return {InputEvent::Kind::Scroll, {0, dy}, 0}; }

// Plays the Fuyuki part to the Mash line at script line 56 and waits until it is fully shown.
bool to_line56(App& app) {
    if (!play_until(app, [&] { return app.script_index() == 1 && drive::at_say(*app.player(), 56); })) return false;
    for (int i = 0; i < 100 && app.player()->status() != Status::AwaitAdvance; ++i) app.update(0.1, {});
    return app.player()->status() == Status::AwaitAdvance;
}
}  // namespace

TEST(story_buttons_hit_testing) {
    const UiLayout ui;
    CHECK(story_button_at(ui, centre(ui.log_button), true) == StoryButton::Log);
    CHECK(story_button_at(ui, centre(ui.auto_on), true) == StoryButton::Auto);
    CHECK(story_button_at(ui, centre(ui.auto_off), true) == StoryButton::Auto);
    CHECK(story_button_at(ui, centre(ui.text), true) == StoryButton::None);
    CHECK(story_button_at(ui, {600, 200}, true) == StoryButton::None);
    CHECK(story_button_at(ui, centre(ui.log_button), false) == StoryButton::None);  // no text box, no buttons
    // every drawn button lies inside its tap area, and the areas stay clear of the text column
    CHECK(contains(ui.log_hit, ui.log_button));
    CHECK(contains(ui.auto_hit, ui.auto_on));
    CHECK(contains(ui.auto_hit, ui.auto_off));
    CHECK(ui.log_hit.x >= ui.text.x + ui.text.w);
    CHECK(ui.log_hit.y + ui.log_hit.h <= ui.auto_hit.y);
}

TEST(app_auto_button_toggles_without_advancing) {
    Env env;
    fakes::MemFiles user;
    add_sheets(user);
    env.platform.user = &user;
    AppConfig cfg;
    cfg.script_ids = {"0100000010", "0100000011"};
    App app(env.platform, cfg);
    REQUIRE(to_line56(app));
    const UiLayout& ui = app.ui();
    const Quad* log = find_src(app.draw_list(), skin::kLogButton);
    REQUIRE(log);
    CHECK(near(log->pos[0], {ui.log_button.x, ui.log_button.y}));
    const Quad* off = find_src(app.draw_list(), skin::kAutoOff);
    REQUIRE(off);
    CHECK(near(off->pos[2], {ui.auto_off.x + ui.auto_off.w, ui.auto_off.y + ui.auto_off.h}));
    CHECK(find_src(app.draw_list(), skin::kAutoOn) == nullptr);

    const std::size_t lines = app.player()->backlog().size();
    app.update(0, {ev(InputEvent::Kind::Pointer, centre(ui.auto_hit)), ev(InputEvent::Kind::PointerUp, centre(ui.auto_hit))});
    CHECK(app.auto_mode());
    CHECK_EQ(app.player()->backlog().size(), lines);  // the tap did not advance the text
    CHECK(app.player()->status() == Status::AwaitAdvance);
    const Quad* on = find_src(app.draw_list(), skin::kAutoOn);
    REQUIRE(on);
    CHECK(near(on->pos[0], {ui.auto_on.x, ui.auto_on.y}));
    CHECK(find_src(app.draw_list(), skin::kAutoOff) == nullptr);

    app.update(0, {ev(InputEvent::Kind::ToggleAuto)});  // the A key / Y button
    CHECK(!app.auto_mode());
    CHECK(find_src(app.draw_list(), skin::kAutoOff) != nullptr);

    // a tap on the text still advances
    app.update(0, {ev(InputEvent::Kind::Pointer, centre(ui.text))});
    CHECK(app.player()->status() != Status::AwaitAdvance || app.player()->backlog().size() > lines);
}

TEST(app_log_button_opens_backlog_and_pauses_the_story) {
    Env env;
    AppConfig cfg;
    cfg.script_ids = {"0100000010", "0100000011"};
    App app(env.platform, cfg);
    REQUIRE(to_line56(app));
    const UiLayout& ui = app.ui();
    app.update(0, {ev(InputEvent::Kind::ToggleAuto)});
    const std::size_t lines = app.player()->backlog().size();
    const std::size_t pc = app.player()->pc();

    app.update(0, {ev(InputEvent::Kind::Pointer, centre(ui.log_hit)), ev(InputEvent::Kind::PointerUp, centre(ui.log_hit))});
    REQUIRE(app.backlog().is_open());  // the release of the opening tap does not close it again
    CHECK_EQ(app.player()->backlog().size(), lines);
    for (int i = 0; i < 300; ++i) app.update(0.1, {});  // 30 s with AUTO on: nothing moves under the log
    CHECK_EQ(app.player()->pc(), pc);
    CHECK(app.backlog().is_open());

    app.update(0, {ev(InputEvent::Kind::Back)});  // B / right click / Backspace
    CHECK(!app.backlog().is_open());
    app.update(0, {ev(InputEvent::Kind::OpenLog)});  // L / X
    CHECK(app.backlog().is_open());
    app.update(0, {ev(InputEvent::Kind::Cancel)});  // Esc closes the log ...
    CHECK(!app.backlog().is_open());
    CHECK(!app.quit_requested());
    app.update(0, {ev(InputEvent::Kind::OpenLog)});
    app.update(0, {ev(InputEvent::Kind::Pointer, {600, 300}), ev(InputEvent::Kind::PointerUp, {602, 301})});  // a tap
    CHECK(!app.backlog().is_open());
    app.update(0, {ev(InputEvent::Kind::Cancel)});  // ... and quits when it is closed
    CHECK(app.quit_requested());
}

TEST(app_backlog_scrolls_with_wheel_and_drag) {
    Env env;
    AppConfig cfg;
    cfg.script_ids = {"0100000011"};
    App app(env.platform, cfg);
    REQUIRE(play_until(app, [&] { return app.player()->backlog().size() >= 40; }));
    app.update(0, {ev(InputEvent::Kind::OpenLog)});
    const Backlog& log = app.backlog();
    REQUIRE(log.is_open());
    REQUIRE(log.max_scroll() > 500);                   // 40 entries are much taller than the screen
    CHECK_NEAR(log.scroll(), log.max_scroll(), 1e-3);  // opens on the newest line

    const float bottom = log.scroll();
    app.update(0, {scroll_ev(-60)});  // one wheel notch up (or the stick / d-pad, same event)
    CHECK_NEAR(log.scroll(), bottom - 60, 1e-3);
    app.update(0, {scroll_ev(1e6f)});
    CHECK_NEAR(log.scroll(), log.max_scroll(), 1e-3);  // clamped at the newest
    app.update(0, {scroll_ev(-1e6f)});
    CHECK_NEAR(log.scroll(), 0, 1e-3);                 // and at the oldest

    // drag: the list follows the pointer; releasing after a drag keeps the log open
    app.update(0, {ev(InputEvent::Kind::Pointer, {600, 400})});
    app.update(0, {ev(InputEvent::Kind::PointerMove, {600, 300})});
    CHECK(log.dragging());
    CHECK_NEAR(log.scroll(), 100, 1e-3);
    app.update(0, {ev(InputEvent::Kind::PointerMove, {600, 150})});
    CHECK_NEAR(log.scroll(), 250, 1e-3);
    app.update(0, {ev(InputEvent::Kind::PointerUp, {600, 150})});
    CHECK(log.is_open());
    CHECK_NEAR(log.scroll(), 250, 1e-3);

    // scrolling changes which lines are drawn
    app.update(0, {scroll_ev(-1e6f)});
    std::vector<float> top_ys;
    for (const Quad& q : app.draw_list().quads) top_ys.push_back(q.pos[0].y);
    app.update(0, {scroll_ev(1e6f)});
    std::vector<float> bottom_ys;
    for (const Quad& q : app.draw_list().quads) bottom_ys.push_back(q.pos[0].y);
    CHECK(top_ys.size() > 20);
    CHECK(bottom_ys.size() > 20);
    CHECK(top_ys != bottom_ys);
}

TEST(backlog_draws_names_and_ruby) {
    TextureStore tex;
    GlyphAtlas atlas(tex, fakes::bytes_of(kDir + "/MPLUS1p-Regular.ttf"), 0);
    REQUIRE(atlas.ok());
    std::vector<BacklogEntry> entries(2);
    entries[0].speaker = parse_rich_text("\xE3\x83\x9E\xE3\x82\xB7\xE3\x83\xA5", {});  // Mash
    entries[0].text = parse_rich_text("[#\xE5\x85\x88\xE8\xBC\xA9:\xE3\x81\x9B\xE3\x82\x93\xE3\x81\xB1\xE3\x81\x84]\xE3\x80\x81ok.", {});  // [#senpai:reading], ok.
    entries[1].text = parse_rich_text("Narration with no speaker.", {});
    Backlog log;
    log.open(entries, atlas);
    REQUIRE(log.is_open());
    const BacklogStyle& st = log.style();
    CHECK_NEAR(log.content_height(), st.name_height + 2 * st.line_height + 2 * st.entry_gap, 1e-3);
    CHECK_NEAR(log.max_scroll(), 0, 1e-3);  // fits: nothing to scroll
    DrawList out;
    log.draw(entries, atlas, out);
    // glyph quads: the name above the first entry, the ruby above its base text
    float name_y = 1e9f, ruby_y = 1e9f, text_y = 1e9f;
    std::size_t glyphs = 0;
    for (const Quad& q : out.quads) {
        if (q.texture != atlas.texture()) continue;
        ++glyphs;
        if (q.color.r == 0) continue;  // drop shadows
        if (q.color.r < 0.9f) name_y = std::min(name_y, q.pos[0].y);  // name ink (light blue)
        else if (std::fabs(q.color.r - 0.95f) < 1e-4f) ruby_y = std::min(ruby_y, q.pos[0].y);  // ruby ink
        else text_y = std::min(text_y, q.pos[0].y);
    }
    CHECK(glyphs > 30);
    CHECK(name_y < ruby_y);
    CHECK(ruby_y < text_y);
    log.close();
    DrawList none;
    log.draw(entries, atlas, none);
    CHECK(none.quads.empty());
}

TEST(asset_cache_keys_stay_short_for_long_urls) {
    const std::string shortk = AssetCache::cache_key("https://static.atlasacademy.io/NA/Back/back10310.png", {});
    CHECK_EQ(shortk, "static.atlasacademy.io/NA/Back/back10310.png");  // short keys unchanged
    std::vector<int> ids = {98115000, 1098122900, 1098123000, 1098124500, 1098257300, 1098269330,
                            1098330800, 1098347310, 1098348500, 1098348510, 1098348700};
    const std::string a = AssetCache::cache_key(svt_script_url(ids, Region::JP), {});
    CHECK(a.size() <= AssetCache::kMaxKeyLength);
    CHECK(a.rfind("api.atlasacademy.io/raw/JP/svtScript", 0) == 0);
    ids.back() = 1098348701;  // a different list keeps a different key
    CHECK(AssetCache::cache_key(svt_script_url(ids, Region::JP), {}) != a);
    CHECK_EQ(AssetCache::cache_key(svt_script_url(ids, Region::JP), {}).size(), a.size());
}

namespace {
// A w x h mask: pixel (x, y) is white when inside(x, y), else opaque black.
Image mask_image(int w, int h, const std::function<bool(int, int)>& inside) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &img.rgba[(static_cast<std::size_t>(y) * w + x) * 4];
            p[0] = p[1] = p[2] = inside(x, y) ? 255 : 0;
            p[3] = 255;
        }
    return img;
}
float polygon_area(const std::vector<Vec2>& poly) {
    float a = 0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const Vec2 p = poly[i], q = poly[(i + 1) % poly.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return std::fabs(a) / 2;
}
}  // namespace

TEST(layer_mask_reads_the_window_shape) {
    // a band over the top half, full width: 64x64 mask -> stage x 0-1344, y 0-512
    LayerMask band = build_layer_mask(mask_image(64, 64, [](int, int y) { return y < 32; }));
    REQUIRE(band.ok);
    CHECK(band.full_width);
    CHECK(!band.narrow);
    CHECK_NEAR(polygon_area(band.polygon), 1344.0 * 512, 1.0);
    CHECK_NEAR(band.band_center, 15.5 * 16, 0.01);  // mean bright row 15.5, 16 stage units per mask row
    // the left 3/4 of the top half: neither full width nor narrow
    LayerMask left = build_layer_mask(mask_image(64, 64, [](int x, int y) { return y < 32 && x < 48; }));
    REQUIRE(left.ok);
    CHECK(!left.full_width);
    CHECK(!left.narrow);
    CHECK_NEAR(polygon_area(left.polygon), 1008.0 * 512, 1.0);
    CHECK_NEAR(left.band_center_x, 23.5 * 21, 0.01);
    // a narrow column
    CHECK(build_layer_mask(mask_image(64, 64, [](int x, int) { return x >= 10 && x < 20; })).narrow);
    // nothing bright: no window
    CHECK(!build_layer_mask(mask_image(8, 8, [](int, int) { return false; })).ok);
}

TEST(clip_quad_cuts_sprites_along_the_mask_edge) {
    Quad q;
    q.texture = 7;
    q.src = {0, 0, 100, 100};
    q.pos[0] = {0, 0};
    q.pos[1] = {100, 0};
    q.pos[2] = {100, 100};
    q.pos[3] = {0, 100};
    // a triangle covering the half of the square below its diagonal (and more, outside it)
    const std::vector<Vec2> tri = {{-50, -50}, {150, 150}, {-50, 150}};
    const std::vector<Quad> parts = clip_quad(q, tri);
    REQUIRE(!parts.empty());
    float area = 0;
    for (const Quad& p : parts) {
        CHECK(p.has_uv);
        CHECK_EQ(p.texture, static_cast<TextureId>(7));
        CHECK(p.pos[3] == p.pos[2]);  // triangles
        area += polygon_area({p.pos[0], p.pos[1], p.pos[2]});
        for (int k = 0; k < 3; ++k) {  // this square maps 1:1 to its texture
            CHECK_NEAR(p.uv[k].x, p.pos[k].x, 1e-3);
            CHECK_NEAR(p.uv[k].y, p.pos[k].y, 1e-3);
            CHECK(p.pos[k].y >= p.pos[k].x - 1e-3);  // below the diagonal
        }
    }
    CHECK_NEAR(area, 5000, 0.5);
    // fully inside: kept whole (as two triangles); fully outside: dropped
    CHECK_EQ(clip_quad(q, {{-10, -10}, {200, -10}, {200, 200}, {-10, 200}}).size(), static_cast<std::size_t>(2));
    CHECK(clip_quad(q, {{200, 200}, {300, 200}, {300, 300}}).empty());
}

TEST(quad_rotates_about_an_origin) {
    Quad q;
    q.pos[0] = {10, 0};
    q.pos[1] = {20, 0};
    q.pos[2] = {20, 10};
    q.pos[3] = {10, 10};
    rotate(q, {0, 0}, 90);  // clockwise on a y-down screen: +x goes to +y
    CHECK_NEAR(q.pos[0].x, 0, 1e-4);
    CHECK_NEAR(q.pos[0].y, 10, 1e-4);
    CHECK_NEAR(q.pos[2].x, -10, 1e-4);
    CHECK_NEAR(q.pos[2].y, 20, 1e-4);
}

TEST(clip_quad_interpolates_corner_colours) {
    Quad q;
    q.pos[0] = {0, 0};
    q.pos[1] = {100, 0};
    q.pos[2] = {100, 100};
    q.pos[3] = {0, 100};
    q.has_colors = true;
    q.blend = Blend::Add;
    q.colors[0] = q.colors[1] = {1, 1, 1, 0};  // transparent at the top, opaque at the bottom
    q.colors[2] = q.colors[3] = {1, 1, 1, 1};
    // keep the top half only
    const std::vector<Quad> parts = clip_quad(q, {{-10, -10}, {110, -10}, {110, 50}, {-10, 50}});
    REQUIRE(!parts.empty());
    for (const Quad& p : parts) {
        CHECK(p.has_colors);
        CHECK(p.blend == Blend::Add);
        for (int k = 0; k < 3; ++k) CHECK_NEAR(p.colors[k].a, p.pos[k].y / 100, 1e-3);
    }
}

TEST(wipe_threshold_matches_fate_cafe) {
    CHECK_NEAR(wipe_alpha(0.5f, 0, 1), 0, 1e-6);   // nothing covered at the start
    CHECK_NEAR(wipe_alpha(1.0f, 1, 1), 1, 1e-6);   // everything at the end
    CHECK_NEAR(wipe_alpha(0.5f, 0.5f, 1), 0.5f, 1e-6);  // r = p(1+g) = 1, (r - v) / g
    CHECK(wipe_alpha(0.2f, 0.3f, 0) == 1);          // hard edge without gradation
    CHECK(wipe_alpha(0.4f, 0.3f, 0) == 0);
    CHECK(wipe_key(" RectangleLeftToRight") == "rectanglestriplefttoright");
    CHECK(wipe_texture_file("OPENEYE") == "wipes/openEye.png");
    CHECK(wipe_texture_file("cutVer").empty());
}

TEST(wipe_builtin_fields_follow_the_texture_shapes) {
    auto at = [](const WipeField& f, float u, float y) {
        return f.v[static_cast<std::size_t>(y * (kWipeFieldSize - 1)) * kWipeFieldSize + static_cast<std::size_t>(u * (kWipeFieldSize - 1))];
    };
    const WipeField l2r = procedural_wipe_field("leftToRight");
    REQUIRE(l2r.ok());
    CHECK(at(l2r, 0.05f, 0.5f) < at(l2r, 0.95f, 0.5f));
    const WipeField in = procedural_wipe_field("circleIn"), out = procedural_wipe_field("circleOut");
    CHECK(at(in, 0.5f, 0.5f) > at(in, 0.05f, 0.05f));    // closes from the corners
    CHECK(at(out, 0.5f, 0.5f) < at(out, 0.05f, 0.05f));  // opens from the middle
    const WipeField eye = procedural_wipe_field("openEye");
    CHECK(at(eye, 0.5f, 0.05f) < 0.01f);  // lids first
    CHECK(at(eye, 0.5f, 0.5f) > 0.9f);
    const WipeField roll = procedural_wipe_field("rollLeft");  // counter-clockwise from the top
    CHECK(at(roll, 0.05f, 0.5f) < at(roll, 0.5f, 0.95f));
    CHECK(at(roll, 0.5f, 0.95f) < at(roll, 0.95f, 0.5f));
    CHECK(procedural_wipe_field("noise").ok());
    CHECK(!procedural_wipe_field("uzumaki").ok());  // no built-in shape: even fade
}

TEST(wipe_masks_draw_a_texture_mask_or_an_even_fade) {
    TextureStore tex;
    WipeMasks wm;
    Stage st;
    st.wipe.pattern = "leftToRight";
    st.wipe.softness = 0;
    st.wipe.cover.snap(0.5f);
    wm.update(st, tex);
    REQUIRE(wm.wipe.texture != 0);
    const Image* img = tex.pixels(wm.wipe.texture);
    REQUIRE(img && img->width == kWipeFieldSize);
    auto alpha = [&](int x) { return img->rgba[(static_cast<std::size_t>(128) * kWipeFieldSize + x) * 4 + 3]; };
    CHECK(alpha(10) == 255);  // left half covered
    CHECK(alpha(245) == 0);
    DrawList dl;
    draw_wipe_layer(wm.wipe, dl);
    REQUIRE(dl.quads.size() == 4);  // the view, plus edge pixels repeated out to the stage edges
    CHECK(near(dl.quads[0].pos[0], {kViewSafeLeft, kViewTop}));
    // an installed texture replaces the built-in field: a flipped gradient covers the right side
    Image flip;
    flip.width = flip.height = 4;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            for (int c = 0; c < 4; ++c) flip.rgba.push_back(c == 3 ? 255 : static_cast<std::uint8_t>(255 - x * 85));
    wm.install("LEFTTORIGHT", wipe_field_from_image(flip));
    wm.update(st, tex);
    img = tex.pixels(wm.wipe.texture);
    CHECK(alpha(10) == 0);
    CHECK(alpha(245) == 255);
    // unknown shape: even cover; fully covered: even cover too; cinema filter without texture: bars
    st.wipe.pattern = "uzumaki";
    wm.update(st, tex);
    CHECK(wm.wipe.texture == 0);
    CHECK_NEAR(wm.wipe.even, 0.5f, 1e-6);
    st.wipe_filter = {true, "cinema", 0.3f, 1};
    wm.update(st, tex);
    CHECK(wm.filter.bars);
    dl.clear_all();
    draw_wipe_layer(wm.filter, dl);
    CHECK(dl.quads.size() == 2);
}

TEST(picture_frame_covers_the_stage_and_fades_in) {
    Env env;
    const std::string url = image_url("cut063_cinema", Region::NA);
    env.http.handler = [&](const HttpRequest& r) {
        HttpResult res;
        if (r.url == url) {
            res.status = 200;
            res.body = testpng::solid(1024, 1024, 0x000000ffu);
        }
        return res;
    };
    TextureStore tex;
    AssetCache cache(env.platform, tex);
    cache.request_texture(url);
    for (int i = 0; i < 100 && !cache.texture(url); ++i) cache.poll();
    REQUIRE(cache.texture(url));
    Stage st;
    st.picture_frame = "cut063_cinema";
    SceneAssets a;
    a.cache = &cache;
    DrawList dl;
    draw_picture_frame(st, a, dl);
    REQUIRE(dl.quads.size() == 3);  // the 1024-wide view plus edge columns out to the stage edges
    const Quad& q = dl.quads[0];
    CHECK(near(q.pos[0], {kViewSafeLeft, 0}));
    CHECK(near(q.pos[2], {kViewSafeLeft + kViewSafeWidth, static_cast<float>(kStageHeight)}));  // top 626 rows = stage
    CHECK_NEAR(q.src.h, 626, 0.01f);
    a.picture_frame_opacity = 0.5f;
    dl.clear_all();
    draw_picture_frame(st, a, dl);
    CHECK_NEAR(dl.quads[0].color.a, 0.5f, 1e-6);
    // no image: Fate Café's fallback shades the top and bottom of the view
    st.picture_frame = "missing_frame";
    dl.clear_all();
    draw_picture_frame(st, a, dl);
    REQUIRE(dl.quads.size() == 2);
    CHECK(dl.quads[0].has_colors);
    CHECK_NEAR(dl.quads[0].colors[0].a, 0.31f, 1e-4);  // 0.62 at half opacity
    st.picture_frame.clear();
    dl.clear_all();
    draw_picture_frame(st, a, dl);
    CHECK(dl.quads.empty());
}

TEST(special_mask_erases_top_down_and_reveals_bottom_up) {
    Quad q;
    q.texture = 7;
    q.blend = Blend::Add;
    q.pos[0] = {0, 0};
    q.pos[1] = {100, 0};
    q.pos[2] = {100, 100};
    q.pos[3] = {0, 100};
    q.src = {0, 0, 100, 100};
    const Rect box{0, 0, 100, 100};
    SpecialErase erase;
    CHECK(apply_special_mask({q}, box, erase, 0).size() == 1);  // untouched at the start
    CHECK(apply_special_mask({q}, box, erase, 1).empty());       // gone at the end
    // mid-way: clear above the band, a gradient through it, opaque below
    const float t = 0.5f, start = 1 - 1.2039215f * (1 - t);
    float lowest_clear = 1e9f;
    for (const Quad& p : apply_special_mask({q}, box, erase, t)) {
        CHECK(p.texture == 7 && p.blend == Blend::Add && p.has_colors);
        for (int k = 0; k < 3; ++k) {
            CHECK(p.pos[k].y >= start * 100 - 0.01f);
            CHECK_NEAR(p.colors[k].a, std::clamp((p.pos[k].y / 100 - start) / 0.2f, 0.f, 1.f), 1e-3);
            lowest_clear = std::min(lowest_clear, p.pos[k].y);
        }
    }
    CHECK_NEAR(lowest_clear, start * 100, 0.01f);
    // reversed: the clear part is at the bottom instead
    SpecialErase rev;
    rev.reverse = true;
    for (const Quad& p : apply_special_mask({q}, box, rev, t))
        for (int k = 0; k < 3; ++k) CHECK(p.pos[k].y <= (1 - start) * 100 + 0.01f);
    // appearance at the start shows nothing, at the end everything
    SpecialErase in;
    in.incoming = true;
    for (const Quad& p : apply_special_mask({q}, box, in, 0))  // only the band's start at the feet (Fate Café)
        for (int k = 0; k < 3; ++k) CHECK(p.pos[k].y >= 80 - 0.01f);
    CHECK(apply_special_mask({q}, box, in, 1).size() == 1);
}

TEST(special_mask_radial_keeps_the_centre_and_clears_the_corners) {
    Quad q;
    q.pos[0] = {0, 0};
    q.pos[1] = {100, 0};
    q.pos[2] = {100, 100};
    q.pos[3] = {0, 100};
    SpecialErase wipe;
    wipe.radial = true;
    const std::vector<Quad> parts = apply_special_mask({q}, {0, 0, 100, 100}, wipe, 0.6f);
    REQUIRE(!parts.empty());
    bool opaque_disc = false;  // the inner disc keeps full alpha
    for (const Quad& p : parts) {
        bool solid = true;
        for (int k = 0; k < 3; ++k) {
            CHECK(std::hypot(p.pos[k].x - 50, p.pos[k].y - 50) < 60);  // n = 0.48: the corners (70.7 away) are gone
            solid = solid && p.colors[k].a > 0.99f;
        }
        opaque_disc = opaque_disc || solid;
    }
    CHECK(opaque_disc);
    CHECK(apply_special_mask({q}, {0, 0, 100, 100}, wipe, 0).size() == 1);
}

TEST(scanlines_split_a_sprite_into_lit_and_dim_rows) {
    Quad q;
    q.pos[0] = {0, 0};
    q.pos[1] = {10, 0};
    q.pos[2] = {10, 8};
    q.pos[3] = {0, 8};
    DrawList dl;
    add_scanlines({q}, dl, 4, 0.4f);
    REQUIRE(!dl.quads.empty());
    for (const Quad& p : dl.quads) {
        const Rect b = quad_bounds(p);
        const float row = std::fmod(b.y, 4.f);
        CHECK_NEAR(p.colors[0].a, row < 2.9f ? 1.f : 0.4f, 1e-4);  // rows 0-3 lit, 3-4 dim
    }
}

TEST(camera_blur_and_gray_filter_the_stage_quads) {
    DrawList dl;
    dl.rect({0, 0, 10, 10}, {1, 0, 0, 1});
    Camera cam;
    cam.filter = "gray";
    SceneAssets a;
    Env env;
    TextureStore tex;
    AssetCache cache(env.platform, tex);
    a.cache = &cache;
    apply_camera_filter(cam, a, 0, dl);
    REQUIRE(dl.quads.size() == 1);
    CHECK_NEAR(dl.quads[0].color.g, 0.2126f, 1e-4);  // red's luma
    cam.filter.clear();
    cam.blur.snap(4);
    apply_camera_filter(cam, a, 0, dl);
    REQUIRE(dl.quads.size() == 9);  // the quad plus 8 offset copies
    CHECK_NEAR(dl.quads[8].color.a, 1.f / 9, 1e-4);  // the last copy weighs 1/9: an even average
    CHECK_NEAR(std::hypot(dl.quads[1].pos[0].x, dl.quads[1].pos[0].y), 4, 1e-3);
}

TEST(title_card_follows_fate_cafe_keyframes) {
    CHECK_NEAR(title_card_opacity(0), 0, 1e-6);
    CHECK_NEAR(title_card_opacity(0.1f), 0.5f, 0.01f);  // ease-in-out halfway up
    CHECK_NEAR(title_card_opacity(0.5f), 1, 1e-6);
    CHECK_NEAR(title_card_opacity(1), 0, 1e-6);
    CHECK_NEAR(title_card_open(0), 0, 1e-6);
    CHECK(title_card_open(0.05f) > 0.4f);  // cubic-bezier(.2,.7,.2,1): opens fast
    CHECK_NEAR(title_card_open(0.3f), 1, 1e-6);
    Env env;
    TextureStore tex;
    GlyphAtlas atlas(tex, env.platform.font, 0);
    REQUIRE(atlas.ok());
    TitleCard card{false, "bit_queststart", 1.3, 2.6};
    DrawList dl;
    draw_title_card(card, {}, atlas, dl);
    CHECK(dl.quads.empty());  // nothing to say: no card
    draw_title_card(card, {"PROLOGUE", "Long", "Section 3", "Opening"}, atlas, dl);
    REQUIRE(dl.quads.size() > 9);
    // the band: three gradient strips centred on the view, .62 black in the middle
    const Rect band{dl.quads[0].pos[0].x, dl.quads[0].pos[0].y, dl.quads[2].pos[1].x - dl.quads[0].pos[0].x,
                    dl.quads[0].pos[3].y - dl.quads[0].pos[0].y};
    CHECK_NEAR(band.x, kViewSafeLeft, 0.01f);
    CHECK_NEAR(band.w, kViewSafeWidth, 0.01f);
    CHECK_NEAR(band.y + band.h / 2, kViewTop + kViewHeight / 2, 0.01f);
    CHECK_NEAR(band.h, 26 + 26 * 1.2f + 6 + 46 * 1.25f + 30, 0.01f);  // section padding, label, title
    CHECK_NEAR(dl.quads[1].colors[0].a, 0.62f, 1e-4);
}

TEST(title_card_wraps_long_titles) {
    Env env;
    TextureStore tex;
    GlyphAtlas atlas(tex, env.platform.font, 0);
    REQUIRE(atlas.ok());
    TitleCard card{true, "bit_chapterstart", 1.3, 2.6};
    DrawList dl;
    draw_title_card(card, {"Lostbelt No.3", "Synthesis Empire SIN: Land of Unfulfilled Ambition", "", ""}, atlas, dl);
    const float h = dl.quads[0].pos[3].y - dl.quads[0].pos[0].y;
    CHECK_NEAR(h, 40 + 26 * 1.2f + 6 + 2 * 54 * 1.25f + 44, 0.01f);  // two title lines
    for (const Quad& q : dl.quads)
        for (const Vec2& p : q.pos) CHECK(p.x >= kViewSafeLeft - 0.01f && p.x <= kViewSafeLeft + kViewSafeWidth + 0.01f);
}

TEST(effect_names_map_to_programs) {
    const EffectDef* flare = find_effect("bit_talk_10_LowLevel");
    REQUIRE(flare);
    CHECK(flare->program == FxProgram::Flare);
    CHECK(!flare->loop);
    CHECK_NEAR(flare->seconds, 0.18f, 1e-6);
    REQUIRE(find_effect("BIT_SEPIA01_DEPTH_FS"));  // any case
    CHECK(find_effect("bit_sepia01_depth_fs")->program == FxProgram::Backdrop);
    REQUIRE(find_effect("bit_talk_explosion_big_new"));  // keyword fallback
    CHECK(find_effect("bit_talk_explosion_big_new")->program == FxProgram::Fireball);
    CHECK(find_effect("bit_talk_qwerty") == nullptr);  // unknown: nothing
    const Image atlas = make_effect_atlas();
    CHECK(atlas.width == kFxSpriteSize * static_cast<int>(FxSprite::kCount));
    CHECK(atlas.height == kFxSpriteSize);
    // the glow sprite is bright in the middle and clear at the corner
    auto alpha = [&](FxSprite s, int x, int y) {
        return atlas.rgba[(static_cast<std::size_t>(y) * atlas.width + static_cast<int>(s) * kFxSpriteSize + x) * 4 + 3];
    };
    CHECK(alpha(FxSprite::Glow, 64, 64) > 240);
    CHECK(alpha(FxSprite::Glow, 1, 1) == 0);
}

TEST(effects_run_once_or_loop_and_follow_their_anchor) {
    const EffectDef* flare = find_effect("bit_talk_10_lowlevel");
    const EffectDef* charge = find_effect("bit_talk_06");
    REQUIRE(flare && charge);
    DrawList dl;
    draw_effect(*flare, 0.05, {600, 300}, false, false, 9, 1, dl);
    REQUIRE(!dl.quads.empty());
    for (const Quad& q : dl.quads) CHECK(q.texture == 9 && q.blend == Blend::Add);
    const Rect b = quad_bounds(dl.quads[0]);
    CHECK_NEAR(b.x + b.w / 2, 600, 0.5f);  // centred on the anchor
    CHECK_NEAR(b.y + b.h / 2, 300, 0.5f);
    dl.clear_all();
    draw_effect(*flare, 0.5, {600, 300}, false, false, 9, 1, dl);
    CHECK(dl.quads.empty());  // a one-shot is over after its run
    draw_effect(*charge, 10.2, {600, 300}, false, false, 9, 1, dl);
    CHECK(!dl.quads.empty());  // loops keep going
    // character effects sit on their character; hidden characters show none
    Stage st;
    Element& e = st.elements["A"];
    e.code = "A";
    e.shown = true;
    e.pos.snap({100, 0});
    Effect fx;
    fx.name = "bit_talk_10_LowLevel";
    fx.layer = EffectLayer::Chara;
    fx.target = "A";
    fx.elapsed = 0.05;
    st.effects.push_back(fx);
    SceneAssets a;
    a.fx_atlas = 9;
    dl.clear_all();
    draw_effects(st, a, false, dl);
    REQUIRE(!dl.quads.empty());
    const Rect c = quad_bounds(dl.quads[0]);
    CHECK_NEAR(c.x + c.w / 2, kStageWidth / 2.f + 100, 0.5f);
    dl.clear_all();
    draw_effects(st, a, true, dl);
    CHECK(dl.quads.empty());  // not a back effect
    e.shown = false;
    draw_effects(st, a, false, dl);
    CHECK(dl.quads.empty());
}

// ---- dictionary lookups (the vnpop fixture dictionary, built by the test_dicts target)

namespace {
std::vector<char32_t> u32(const std::string& s) {
    std::vector<char32_t> out;
    for (std::size_t i = 0; i < s.size();) out.push_back(utf8::next(s, i));
    return out;
}
std::vector<std::uint8_t> test_dict() { return fakes::bytes_of(VNR_TEST_DICT); }
// A row of 30-unit character boxes at y 470 (where the dialogue sits), for popups without an App.
LookupTarget row_target(const std::string& text) {
    LookupTarget t;
    float x = 300;
    for (char32_t c : u32(text)) {
        t.boxes.push_back({{x, 470, 30, 40}, c});
        x += 30;
    }
    return t;
}
// Plays the JP Fuyuki fixture until the line containing `text` waits for a tap.
bool play_jp_until(App& app, const std::string& text) {
    return play_until(app, [&] {
        const Player* p = app.player();
        const TextView tv = p->text();
        return p->status() == Status::AwaitAdvance && tv.say && tv.say->text.plain().find(text) != std::string::npos;
    });
}
Vec2 centre(const GlyphBox& b) { return {b.rect.x + b.rect.w / 2, b.rect.y + b.rect.h / 2}; }
}  // namespace

TEST(dictionary_finds_inflected_words_in_yomitan_order) {
    Dictionary dict;
    CHECK(!dict.load({1, 2, 3}));
    REQUIRE(dict.load(test_dict()));
    const auto text = u32("星を見て食べなかった");
    const auto at_tabe = dict.lookup(text, 4);
    REQUIRE(!at_tabe.empty());
    CHECK_EQ(at_tabe[0].headword, std::string("食べる"));
    CHECK_EQ(at_tabe[0].reading, std::string("たべる"));
    CHECK_EQ(at_tabe[0].matched, static_cast<std::size_t>(6));  // 食べなかった
    CHECK(!at_tabe[0].rule.empty());
    CHECK(at_tabe[0].glossary.find("to eat") != std::string::npos);
    CHECK(at_tabe[0].freq_rank > 0);
    const auto at_hoshi = dict.lookup(text, 0);
    REQUIRE(!at_hoshi.empty());
    CHECK_EQ(at_hoshi[0].headword, std::string("星"));
    CHECK(at_hoshi[0].rule.empty());
    CHECK(dict.lookup(text, 1).empty());  // を: no entry
    const auto words = dict.word_starts(text);
    CHECK(std::find(words.begin(), words.end(), std::size_t{0}) != words.end());
    CHECK(std::find(words.begin(), words.end(), std::size_t{4}) != words.end());
    // text longer than vnpop's 128-character line is looked up through a window
    std::string long_text;
    for (int i = 0; i < 200; ++i) long_text += "を";
    long_text += "読んだ";
    const auto far = dict.lookup(u32(long_text), 200);
    REQUIRE(!far.empty());
    CHECK_EQ(far[0].headword, std::string("読む"));
    const auto far_words = dict.word_starts(u32(long_text));
    CHECK(std::find(far_words.begin(), far_words.end(), std::size_t{200}) != far_words.end());
}

TEST(lookup_popup_hops_pages_and_sits_above_the_word) {
    Env env;
    TextureStore tex;
    GlyphAtlas atlas(tex, env.platform.font, 0);
    REQUIRE(atlas.ok());
    Dictionary dict;
    REQUIRE(dict.load(test_dict()));
    const LookupTarget t = row_target("夜に星を見て食べなかった");
    LookupPopup pop;
    CHECK(!pop.open(dict, t, 1));  // に
    CHECK(!pop.is_open());
    REQUIRE(pop.open_first(dict, t));
    CHECK_EQ(pop.start(), static_cast<std::size_t>(0));
    CHECK(pop.hop(dict, +1));
    CHECK_EQ(pop.start(), static_cast<std::size_t>(2));  // 星
    CHECK(pop.hop(dict, +1));
    CHECK_EQ(pop.start(), static_cast<std::size_t>(6));  // 食べなかった
    CHECK(!pop.hop(dict, +1));                           // the last word: stays
    CHECK_EQ(pop.start(), static_cast<std::size_t>(6));
    CHECK(pop.hop(dict, -1));
    CHECK_EQ(pop.start(), static_cast<std::size_t>(2));
    pop.select(+5);  // clamps to the entries there are
    CHECK(pop.selected() < pop.entries().size());
    DrawList dl;
    pop.draw(atlas, dl);
    CHECK(dl.quads.size() > 20);
    CHECK(pop.contains({300 + 2 * 30 + 15, 470 - 30}));  // just above 星
    CHECK(!pop.contains({300 + 2 * 30 + 15, 490}));      // not over the word itself
    pop.close();
    CHECK(!pop.contains({300 + 2 * 30 + 15, 470 - 30}));
}

TEST(app_shift_hover_and_y_look_up_the_dialogue) {
    Env env;
    fakes::MemFiles user;
    user.data[kDictKey] = test_dict();
    env.platform.user = &user;
    AppConfig cfg;
    cfg.region = Region::JP;
    cfg.script_ids = {"0100000011"};
    App app(env.platform, cfg);
    REQUIRE(play_jp_until(app, "行ってしまいました"));
    const std::vector<GlyphBox> boxes = app.story_boxes();
    const char32_t kIku = 0x884C;  // 行
    std::size_t iku = boxes.size();
    for (std::size_t i = 0; i < boxes.size(); ++i)
        if (boxes[i].cp == kIku) iku = i;
    REQUIRE(iku < boxes.size());
    const std::size_t lines = app.player()->backlog().size();

    // Shift over 行: 行く, with the inflection undone; the story waits under the popup
    app.update(0, {{InputEvent::Kind::LookupAt, centre(boxes[iku]), 0}});
    REQUIRE(app.lookup().is_open());
    CHECK_EQ(app.lookup().start(), iku);
    CHECK_EQ(app.lookup().entries()[0].headword, std::string("行く"));
    CHECK(!app.lookup().entries()[0].rule.empty());
    app.update(30, {});
    CHECK_EQ(app.player()->backlog().size(), lines);
    // a tap outside closes it and does nothing else (its release is not a tap either)
    app.update(0, {{InputEvent::Kind::Pointer, {700, 200}, 0}, {InputEvent::Kind::PointerUp, {700, 200}, 0}});
    CHECK(!app.lookup().is_open());
    CHECK_EQ(app.player()->backlog().size(), lines);
    CHECK(app.player()->status() == Status::AwaitAdvance);
    // Shift over no text closes it
    app.update(0, {{InputEvent::Kind::LookupAt, centre(boxes[iku]), 0}});
    REQUIRE(app.lookup().is_open());
    app.update(0, {{InputEvent::Kind::LookupAt, {700, 100}, 0}});
    CHECK(!app.lookup().is_open());

    // Y: the first dictionary word of the line; right hops on; A closes without advancing
    app.update(0, {{InputEvent::Kind::Lookup, {}, 0}});
    REQUIRE(app.lookup().is_open());
    const std::size_t first = app.lookup().start();
    CHECK(first <= iku);
    app.update(0, {{InputEvent::Kind::Nav, {}, static_cast<std::size_t>(NavDir::Right)}});
    CHECK(app.lookup().start() >= first);
    app.update(0, {{InputEvent::Kind::Advance, {}, 0}});
    CHECK(!app.lookup().is_open());
    CHECK_EQ(app.player()->backlog().size(), lines);
    // the log: Y looks up its newest entry on screen; LOG again closes the popup (and the log)
    app.update(0, {{InputEvent::Kind::OpenLog, {}, 0}});
    app.update(0, {{InputEvent::Kind::Lookup, {}, 0}});
    CHECK(app.lookup().is_open());
    CHECK(app.lookup().target_id() > 0);
    app.update(0, {{InputEvent::Kind::OpenLog, {}, 0}});
    CHECK(!app.lookup().is_open());
    // the smoke helper finds text in the line
    CHECK(app.lookup_text("行って"));
    CHECK_EQ(app.lookup().start(), iku);
}

TEST(app_without_a_dictionary_says_so) {
    Env env;
    fakes::MemFiles user;
    env.platform.user = &user;
    AppConfig cfg;
    cfg.region = Region::JP;
    cfg.script_ids = {"0100000011"};
    App app(env.platform, cfg);
    REQUIRE(play_jp_until(app, "行ってしまいました"));
    app.update(0, {{InputEvent::Kind::Lookup, {}, 0}});
    CHECK(!app.lookup().is_open());
    bool logged = false;
    for (const std::string& l : env.log.lines) logged = logged || l.find("dict.bin") != std::string::npos;
    CHECK(logged);
}
