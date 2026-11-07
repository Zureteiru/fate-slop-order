// Runtime: JSON, images, asset cache, text layout, and golden draw lists from the App.
#include <cstdio>

#include "check.h"
#include "drive.h"
#include "fakes/fake_platform.h"
#include "png_writer.h"
#include "vnr/rt/app.h"
#include "vnr/rt/json.h"
#include "vnr/rt/layer_mask.h"
#include "vnr/rt/ui_skin.h"
#include "vnr/rt/wipe_mask.h"

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

TEST(app_skin_draws_text_box_name_tab_and_next_marker) {
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
    // waiting for a tap: one frame of the spinning "next" marker
    for (int i = 0; i < 100 && app.player()->status() != Status::AwaitAdvance; ++i) app.update(0.1, {});
    REQUIRE(app.player()->status() == Status::AwaitAdvance);
    bool marker = false;
    for (int f = 0; f < skin::kNextFrames; ++f) marker = marker || find_src(app.draw_list(), skin::next_frame(f)) != nullptr;
    CHECK(marker);
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
