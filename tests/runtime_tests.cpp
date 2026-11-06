// Runtime: JSON, images, asset cache, text layout, and golden draw lists from the App.
#include "check.h"
#include "drive.h"
#include "fakes/fake_platform.h"
#include "png_writer.h"
#include "vnr/rt/app.h"
#include "vnr/rt/json.h"
#include "vnr/rt/ui_skin.h"

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
