// The chapter picker: catalogue parsing, progress, list navigation (keys, controller, pointer),
// section -> part, and the picker -> reader -> end card -> picker flow through the App.
#include <cmath>

#include "check.h"
#include "fakes/fake_platform.h"
#include "vnr/rt/app.h"
#include "vnr/rt/catalogue.h"
#include "vnr/rt/focus_nav.h"
#include "vnr/rt/picker.h"

#ifndef VNR_FIXTURE_DIR
#error "VNR_FIXTURE_DIR must point at .cache/scripts"
#endif

using namespace vnr;
using namespace vnr::rt;

namespace {

// A tiny hand-written catalogue in reader.json's shape (Portuguese names with English `source*` ones).
const char* kCatalogue = R"({
  "generatedAt": "2026-10-18T18:25:41.126Z", "source": "Atlas Academy", "sourceRegion": "NA", "dataVersion": 7,
  "arcs": [
    {"id": "arc1", "name": "Arco 1", "sourceName": "Arc 1"},
    {"id": "arc2", "name": "Arco 2", "sourceName": "Arc 2"}
  ],
  "chapters": [
    {"id": "alpha", "order": 2, "arcId": "arc1", "name": "Alfa", "sourceName": "Alpha",
     "chapterLabel": "Prólogo", "sourceChapterLabel": "Prologue", "longName": "Longo", "sourceLongName": "Long Alpha",
     "bannerUrl": "https://static.atlasacademy.io/NA/Banner/questboard_cap100.png",
     "sectionCount": 4, "scriptCount": 4, "durationSeconds": 6249,
     "sections": [
       {"questId": 1, "name": "Opening", "type": "main", "spotName": "Spot A", "durationSeconds": 125,
        "scripts": [{"scriptId": "9900000010", "scriptRegion": "NA"}, {"scriptId": "9900000011", "scriptRegion": "NA"}]},
       {"questId": 2, "name": "Fechado", "sourceName": "Locked one", "type": "main", "locked": true, "scripts": []},
       {"questId": 3, "name": "Second", "sectionNumber": 2, "type": "main", "spotName": "Spot B", "durationSeconds": 3600,
        "scripts": [{"scriptId": "9900000020", "scriptRegion": "JP", "englishScriptRegion": "NA"}]},
       {"questId": 4, "name": "JP only", "sectionNumber": 3, "type": "main",
        "scripts": [{"scriptId": "9900000030", "scriptRegion": "JP", "skipMovieIds": ["ops00400"]}]}
     ]},
    {"id": "beta", "order": 1, "arcId": "arc1", "name": "Beta", "chapterLabel": "Primeira", "durationSeconds": 60,
     "sections": [{"questId": 9, "name": "Only", "type": "main", "scripts": [{"scriptId": "9900000040", "scriptRegion": "NA"}]}]},
    {"id": "gamma", "order": 3, "arcId": "arcX", "name": "Gamma", "durationSeconds": 1020,
     "sections": [{"questId": 10, "name": "Trailer", "type": "movie", "video": {"id": "t"}, "scripts": []}]}
  ]
})";

const char* kTinyScript = "＠Mash\nHello.\n[k]\n＠Mash\nBye.\n[k]\n";

std::vector<std::uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

bool ends_with(const std::string& s, const std::string& t) {
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}

struct Env {
    fakes::FakeHttp http;
    fakes::MemFiles files, user;
    fakes::MemLog log;
    Platform platform{http, files, log, fakes::bytes_of(std::string(VNR_FIXTURE_DIR) + "/MPLUS1p-Regular.ttf"), 0, &user};
    bool catalogue_ok = true;
    int catalogue_requests = 0;
    Env() {
        http.handler = [this](const HttpRequest& r) -> HttpResult {
            if (r.url == kCatalogueUrl) {
                ++catalogue_requests;
                return catalogue_ok ? HttpResult{0, 200, bytes(kCatalogue)} : HttpResult{0, 0, {}};
            }
            if (ends_with(r.url, ".txt") && r.url.find("/99000000") != std::string::npos) return {0, 200, bytes(kTinyScript)};
            return {0, 404, {}};
        };
    }
    std::size_t script_requests(const char* region) const {
        std::size_t n = 0;
        for (const auto& r : http.requests)
            if (ends_with(r.url, ".txt") && r.url.find(std::string("/") + region + "/Script/") != std::string::npos) ++n;
        return n;
    }
};

InputEvent ev(InputEvent::Kind k) { return {k, {}, 0}; }
InputEvent nav(NavDir d) { return {InputEvent::Kind::Nav, {}, static_cast<std::size_t>(d)}; }

void step(App& app, std::vector<InputEvent> in = {}, double dt = 1.0 / 60) { app.update(dt, in); }

void settle(App& app, int frames = 5) {
    for (int i = 0; i < frames; ++i) step(app);
}

Vec2 centre(const Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }

void tap(App& app, Vec2 p) {
    step(app, {{InputEvent::Kind::Pointer, p, 0}, {InputEvent::Kind::PointerUp, p, 0}});
}

// Taps through the part until the App is back on the picker (or gives up).
bool play_to_end(App& app) {
    for (int i = 0; i < 20000; ++i) {
        if (app.phase() == App::Phase::Picker) return true;
        const Player* p = app.player();
        std::vector<InputEvent> in;
        if (p && (p->status() == Status::Typing || p->status() == Status::AwaitAdvance)) in.push_back(ev(InputEvent::Kind::Advance));
        step(app, in, 1.0 / 240);
    }
    return false;
}

std::string progress_file(Env& env) {
    const auto& d = env.user.data[kProgressKey];
    return {d.begin(), d.end()};
}

}  // namespace

TEST(catalogue_parses_arcs_chapters_and_sections) {
    const auto cat = parse_catalogue(kCatalogue);
    REQUIRE(cat.has_value());
    CHECK_EQ(cat->data_version, 7);
    REQUIRE(cat->chapters.size() == 3);
    CHECK_EQ(cat->chapters[0].id, "beta");  // sorted by `order`
    CHECK_EQ(cat->chapters[1].id, "alpha");
    const CatalogueChapter& a = cat->chapters[1];
    CHECK_EQ(a.name, "Alpha");           // English source names win
    CHECK_EQ(a.label, "PROLOGUE");
    CHECK_EQ(a.long_name, "Long Alpha");
    CHECK_EQ(cat->chapters[0].name, "Beta");  // no source name: the plain one
    CHECK_EQ(cat->chapters[0].label, "PRIMEIRA");
    CHECK_EQ(a.script_count, 4);
    REQUIRE(a.sections.size() == 4);
    CHECK_EQ(a.sections[1].name, "Locked one");
    CHECK(!a.sections[1].playable());
    CHECK(a.sections[0].playable());
    CHECK_EQ(a.sections[0].label(), "Opening");
    CHECK_EQ(a.sections[2].label(), "Section 2 \xC2\xB7 Second");
    CHECK(a.sections[2].scripts[0].region == Region::JP);
    CHECK(a.sections[2].scripts[0].english);
    CHECK(!a.sections[3].scripts[0].english);
    CHECK_EQ(a.sections[3].scripts[0].skip_movies.size(), static_cast<std::size_t>(1));
    CHECK(cat->chapters[2].sections[0].video);
    CHECK(!cat->chapters[2].sections[0].playable());
    // arc2 has no chapters (dropped); gamma's unknown arc gets its own at the end
    REQUIRE(cat->arcs.size() == 2);
    CHECK_EQ(cat->arcs[0].name, "Arc 1");
    CHECK_EQ(cat->arcs[0].chapters.size(), static_cast<std::size_t>(2));
    CHECK_EQ(cat->arcs[1].id, "arcX");
    CHECK(!parse_catalogue("not json").has_value());
    CHECK(!parse_catalogue(R"({"chapters": []})").has_value());
}

TEST(catalogue_labels_and_durations) {
    CHECK_EQ(format_duration(6249), "1h 44min");
    CHECK_EQ(format_duration(1149), "19min");
    CHECK_EQ(format_duration(39600), "11h");
    CHECK_EQ(format_duration(0), "0min");
    const auto cat = parse_catalogue(kCatalogue);
    REQUIRE(cat.has_value());
    CHECK_EQ(chapter_meta(cat->chapters[1]), "4 parts \xC2\xB7 1h 44min");
    CHECK_EQ(chapter_meta(cat->chapters[0]), "1 part \xC2\xB7 1min");
    CHECK_EQ(section_meta(cat->chapters[1].sections[0]), "Spot A \xC2\xB7 2 scenes \xC2\xB7 2min");
    CHECK_EQ(section_meta(cat->chapters[1].sections[1]), "locked");
}

TEST(section_becomes_a_part_in_the_wanted_region) {
    const auto cat = parse_catalogue(kCatalogue);
    REQUIRE(cat.has_value());
    auto p = part_for(*cat, "alpha", 0, Region::NA);
    REQUIRE(p.has_value());
    CHECK(p->region == Region::NA);
    CHECK_EQ(p->script_ids.size(), static_cast<std::size_t>(2));
    CHECK_EQ(p->script_ids[1], "9900000011");
    p = part_for(*cat, "alpha", 2, Region::NA);  // JP script with an English copy
    REQUIRE(p.has_value());
    CHECK(p->region == Region::NA);
    p = part_for(*cat, "alpha", 3, Region::NA);  // no English: JP
    REQUIRE(p.has_value());
    CHECK(p->region == Region::JP);
    p = part_for(*cat, "alpha", 0, Region::JP);
    REQUIRE(p.has_value());
    CHECK(p->region == Region::JP);
    CHECK(!part_for(*cat, "alpha", 1, Region::NA).has_value());  // locked
    CHECK(!part_for(*cat, "alpha", 9, Region::NA).has_value());
    CHECK(!part_for(*cat, "nope", 0, Region::NA).has_value());
}

TEST(progress_round_trips_and_picks_the_next_unread) {
    Progress p;
    p.region = Region::JP;
    p.opened("alpha", 2);
    p.mark_read("alpha", 0);
    p.mark_read("alpha", 2);
    p.mark_read("we\"ird", 1);
    const Progress q = parse_progress(serialize_progress(p));
    CHECK(q.region == Region::JP);
    CHECK_EQ(q.last_chapter, "alpha");
    CHECK_EQ(q.last_section, static_cast<std::size_t>(2));
    CHECK(q.is_read("alpha", 0));
    CHECK(!q.is_read("alpha", 1));
    CHECK(q.is_read("we\"ird", 1));
    const Progress bad = parse_progress("{oops");
    CHECK(bad.region == Region::NA);
    CHECK(bad.last_chapter.empty());

    const auto cat = parse_catalogue(kCatalogue);
    REQUIRE(cat.has_value());
    const CatalogueChapter& a = *cat->chapter("alpha");
    CHECK_EQ(next_unread(a, Progress{}).value_or(99), static_cast<std::size_t>(0));
    CHECK_EQ(next_unread(a, q).value_or(99), static_cast<std::size_t>(3));  // 1 is locked
    Progress all = q;
    all.mark_read("alpha", 3);
    CHECK_EQ(next_unread(a, all).value_or(99), static_cast<std::size_t>(0));  // all read: the first again
    CHECK(!next_unread(*cat->chapter("gamma"), q).has_value());
}

TEST(focus_nav_moves_through_a_grid_by_keys_and_controller) {
    // A button above a 3-column grid of two rows, then a disabled row and a last button.
    FocusNav n;
    n.set_view(0, 300);
    std::vector<Rect> r = {{0, 0, 100, 40}};
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 3; ++col) r.push_back({static_cast<float>(col) * 120, 60 + static_cast<float>(row) * 120, 100, 100});
    r.push_back({0, 300, 340, 50});  // 7: disabled
    r.push_back({0, 400, 340, 50});  // 8
    std::vector<bool> on(r.size(), true);
    on[7] = false;
    n.set_items(r, on, 500);
    CHECK(n.move(NavDir::Down));  // no focus yet: the first visible button
    CHECK_EQ(n.focus().value_or(99), static_cast<std::size_t>(0));
    n.move(NavDir::Down);
    CHECK_EQ(n.focus().value_or(99), static_cast<std::size_t>(1));  // nearest row, closest column
    n.move(NavDir::Right);
    n.move(NavDir::Right);
    CHECK_EQ(n.focus().value_or(99), static_cast<std::size_t>(3));
    CHECK(!n.move(NavDir::Right));  // the end of the row: stays
    n.move(NavDir::Down);
    CHECK_EQ(n.focus().value_or(99), static_cast<std::size_t>(6));  // same column, next row
    n.move(NavDir::Left);
    CHECK_EQ(n.focus().value_or(99), static_cast<std::size_t>(5));
    n.move(NavDir::Down);  // skips the disabled row
    CHECK_EQ(n.focus().value_or(99), static_cast<std::size_t>(8));
    CHECK_NEAR(n.scroll(), 174, 0.01);  // revealed: 400 + 50 + 24 margin - 300 view
    n.move(NavDir::Up);
    CHECK_EQ(n.focus().value_or(99), static_cast<std::size_t>(5));
    n.move(NavDir::Up);
    n.move(NavDir::Up);
    CHECK_EQ(n.focus().value_or(99), static_cast<std::size_t>(0));
    CHECK_NEAR(n.scroll(), 0, 0.01);
}

TEST(focus_nav_scrolls_and_taps_with_the_pointer) {
    FocusNav n;
    n.set_view(0, 200);
    std::vector<Rect> r;
    for (int i = 0; i < 10; ++i) r.push_back({0, static_cast<float>(i) * 60, 300, 50});
    n.set_items(r, std::vector<bool>(r.size(), true), 600);
    CHECK_NEAR(n.max_scroll(), 400, 0.01);
    n.set_focus(0);
    n.scroll_by(130);  // wheel / stick: the focus follows onto the screen
    CHECK_NEAR(n.scroll(), 130, 0.01);
    CHECK_EQ(n.focus().value_or(99), static_cast<std::size_t>(3));
    n.scroll_by(-1000);
    CHECK_NEAR(n.scroll(), 0, 0.01);
    // A tap picks the button under it...
    n.press({10, 70});
    n.drag({12, 73});  // inside the slop: still a tap
    CHECK_EQ(n.release({12, 73}).value_or(99), static_cast<std::size_t>(1));
    CHECK_EQ(n.focus().value_or(99), static_cast<std::size_t>(1));
    // ...a drag scrolls instead and picks nothing.
    n.press({10, 150});
    n.drag({10, 50});
    CHECK(n.dragging());
    CHECK(!n.release({10, 50}).has_value());
    CHECK_NEAR(n.scroll(), 100, 0.01);
    CHECK_EQ(n.hit({10, 10}).value_or(99), static_cast<std::size_t>(1));  // content y 110
    CHECK(!n.hit({10, 205}).has_value());  // below the view
    CHECK(!n.hit({400, 10}).has_value());
}

TEST(picker_flow_home_chapter_part_end_card_and_back) {
    Env env;
    AppConfig cfg;
    cfg.picker = true;
    App app(env.platform, cfg);
    settle(app);
    CHECK(app.phase() == App::Phase::Picker);
    REQUIRE(app.picker() && app.picker()->catalogue());
    const Picker& pk = *app.picker();
    CHECK(pk.screen() == Picker::Screen::Home);
    CHECK_EQ(pk.focused_label(), "Start \xC2\xB7 Beta");
    CHECK_EQ(env.script_requests("NA") + env.script_requests("JP"), static_cast<std::size_t>(0));  // nothing plays yet
    CHECK(!app.draw_list().quads.empty());

    // d-pad / arrows: down to the first card, right to the next, A opens it
    step(app, {nav(NavDir::Down)});
    CHECK_EQ(pk.focused_label(), "Beta");
    step(app, {nav(NavDir::Right)});
    CHECK_EQ(pk.focused_label(), "Alpha");
    step(app, {ev(InputEvent::Kind::Advance)});
    CHECK(pk.screen() == Picker::Screen::Chapter);
    CHECK_EQ(pk.chapter_id(), "alpha");
    CHECK_EQ(pk.focused_label(), "Start: Opening");
    step(app, {nav(NavDir::Down)});
    CHECK_EQ(pk.focused_label(), "Opening");
    step(app, {nav(NavDir::Down)});
    CHECK_EQ(pk.focused_label(), "Second");  // the locked part is skipped
    step(app, {nav(NavDir::Up)});
    step(app, {ev(InputEvent::Kind::Advance)});
    CHECK(app.phase() != App::Phase::Picker);
    CHECK(app.config().region == Region::NA);
    CHECK_EQ(app.config().script_ids.size(), static_cast<std::size_t>(2));
    CHECK(progress_file(env).find("\"last\": {\"chapter\": \"alpha\", \"section\": 0}") != std::string::npos);

    // read both scripts: the end card offers the next playable part
    REQUIRE(play_to_end(app));
    CHECK(pk.screen() == Picker::Screen::End);
    CHECK(pk.progress().is_read("alpha", 0));
    CHECK(progress_file(env).find("\"alpha\": [0]") != std::string::npos);
    CHECK_EQ(pk.focused_label(), "Continue to Second");
    const std::size_t end_quads = app.draw_list().quads.size();
    step(app, {ev(InputEvent::Kind::Advance)});
    CHECK(app.phase() != App::Phase::Picker);
    CHECK_EQ(app.config().script_ids[0], "9900000020");
    CHECK(app.config().region == Region::NA);
    CHECK(end_quads > 0);

    // Esc while reading goes back to the chapter page, on that part
    for (int i = 0; i < 50 && app.phase() != App::Phase::Playing; ++i) step(app);
    CHECK(app.phase() == App::Phase::Playing);
    step(app, {ev(InputEvent::Kind::Cancel)});
    CHECK(app.phase() == App::Phase::Picker);
    CHECK(!app.quit_requested());
    CHECK(pk.screen() == Picker::Screen::Chapter);
    CHECK_EQ(pk.focused_label(), "Second");
    CHECK(!pk.progress().is_read("alpha", 2));

    // B goes up to the home screen, on the chapter that was open; Esc there quits
    step(app, {ev(InputEvent::Kind::Back)});
    CHECK(pk.screen() == Picker::Screen::Home);
    CHECK_EQ(pk.focused_label(), "Alpha");
    step(app, {ev(InputEvent::Kind::Cancel)});
    CHECK(app.quit_requested());
}

TEST(picker_pointer_taps_drags_and_switches_region) {
    Env env;
    AppConfig cfg;
    cfg.picker = true;
    App app(env.platform, cfg);
    settle(app);
    REQUIRE(app.picker() && app.picker()->catalogue());
    const Picker& pk = *app.picker();
    // tap the EN/JP switch (item 0)
    tap(app, centre(pk.item_rect(0)));
    CHECK(pk.region() == Region::JP);
    CHECK(progress_file(env).find("\"region\": \"JP\"") != std::string::npos);
    // a drag across a card scrolls and opens nothing
    const Vec2 card = centre(pk.item_rect(3));  // Alpha
    step(app, {{InputEvent::Kind::Pointer, card, 0}});
    step(app, {{InputEvent::Kind::PointerMove, {card.x, card.y - 120}, 0}});
    step(app, {{InputEvent::Kind::PointerUp, {card.x, card.y - 120}, 0}});
    CHECK(pk.screen() == Picker::Screen::Home);
    CHECK(pk.nav().scroll() > 0);
    step(app, {{InputEvent::Kind::Scroll, {0, -1000}, 0}});  // wheel back to the top
    CHECK_NEAR(pk.nav().scroll(), 0, 0.01);
    // a tap opens the chapter; a tap on a part plays it, in JP now
    tap(app, centre(pk.item_rect(3)));
    CHECK(pk.screen() == Picker::Screen::Chapter);
    std::size_t opening = 99;
    for (std::size_t i = 0; i < pk.item_count(); ++i) {
        tap(app, centre(pk.item_rect(i)));
        if (app.phase() != App::Phase::Picker) {
            opening = i;
            break;
        }
    }
    CHECK(opening != 99);
    CHECK(app.config().region == Region::JP);
    for (int i = 0; i < 10; ++i) step(app);
    CHECK(env.script_requests("JP") > 0);
    CHECK_EQ(env.script_requests("NA"), static_cast<std::size_t>(0));
}

TEST(picker_remembers_progress_across_launches) {
    Env env;
    Progress p;
    p.opened("alpha", 2);
    p.mark_read("alpha", 0);
    env.user.data[kProgressKey] = bytes(serialize_progress(p));
    AppConfig cfg;
    cfg.picker = true;
    App app(env.platform, cfg);
    settle(app);
    const Picker& pk = *app.picker();
    CHECK_EQ(pk.focused_label(), "Continue · Alpha");
    step(app, {ev(InputEvent::Kind::Advance)});  // continues the last part opened
    CHECK_EQ(app.config().script_ids[0], "9900000020");
    step(app, {ev(InputEvent::Kind::Cancel)});   // left early: back on that part, not marked read
    CHECK_EQ(pk.chapter_id(), "alpha");
    CHECK_EQ(pk.focused_label(), "Second");
    CHECK(!pk.progress().is_read("alpha", 2));
    CHECK(pk.progress().is_read("alpha", 0));
}

TEST(picker_catalogue_sources_user_copy_cache_and_retry) {
    {  // the user's own reader.json wins and is never downloaded
        Env env;
        env.user.data[kCatalogueUserKey] = bytes(kCatalogue);
        AppConfig cfg;
        cfg.picker = true;
        App app(env.platform, cfg);
        settle(app);
        CHECK(app.picker()->catalogue() != nullptr);
        CHECK_EQ(env.catalogue_requests, 0);
    }
    {  // the last download shows while the new one fails
        Env env;
        env.catalogue_ok = false;
        env.files.data[AssetCache::cache_key(kCatalogueUrl, {})] = bytes(kCatalogue);
        AppConfig cfg;
        cfg.picker = true;
        App app(env.platform, cfg);
        settle(app);
        CHECK(app.picker()->catalogue() != nullptr);
        CHECK_EQ(env.catalogue_requests, 1);
        CHECK(app.picker()->ready());
    }
    {  // nothing at all: an error with Retry, which downloads again
        Env env;
        env.catalogue_ok = false;
        AppConfig cfg;
        cfg.picker = true;
        App app(env.platform, cfg);
        settle(app);
        const Picker& pk = *app.picker();
        CHECK(pk.catalogue() == nullptr);
        CHECK_EQ(pk.status(), "Could not download the chapter list");
        CHECK_EQ(pk.focused_label(), "Retry");
        env.catalogue_ok = true;
        step(app, {ev(InputEvent::Kind::Advance)});
        settle(app);
        CHECK_EQ(env.catalogue_requests, 2);
        REQUIRE(pk.catalogue() != nullptr);
        CHECK_EQ(pk.focused_label(), "Start \xC2\xB7 Beta");
        // the fresh copy was written to the disk cache for next time
        CHECK(env.files.data.count(AssetCache::cache_key(kCatalogueUrl, {})) == 1);
    }
}

TEST(picker_opens_a_screen_from_the_command_line) {
    Env env;
    AppConfig cfg;
    cfg.picker = true;
    cfg.picker_screen = "end=alpha:0";
    App app(env.platform, cfg);
    settle(app);
    const Picker& pk = *app.picker();
    CHECK(pk.screen() == Picker::Screen::End);
    CHECK_EQ(pk.focused_label(), "Continue to Second");
    step(app, {ev(InputEvent::Kind::Back)});  // B on the end card: back to the index, on the next part
    CHECK(pk.screen() == Picker::Screen::Chapter);
    CHECK_EQ(pk.focused_label(), "Second");

    Env env2;
    AppConfig c2;
    c2.picker = true;
    c2.picker_screen = "chapter=nope";
    App app2(env2.platform, c2);
    settle(app2);
    CHECK(app2.picker()->screen() == Picker::Screen::Home);
}

TEST(reading_arrows_still_advance_and_go_back) {
    Env env;
    AppConfig cfg;
    cfg.script_ids = {"9900000010"};  // --scripts: no picker
    App app(env.platform, cfg);
    CHECK(app.picker() == nullptr);
    for (int i = 0; i < 50 && app.phase() != App::Phase::Playing; ++i) step(app);
    REQUIRE(app.phase() == App::Phase::Playing);
    for (int i = 0; i < 400 && app.player()->status() != Status::AwaitAdvance; ++i) step(app, {}, 0.05);
    CHECK_EQ(app.player()->text().say->text.plain(), "Hello.");
    step(app, {nav(NavDir::Right)});
    for (int i = 0; i < 400 && app.player()->status() != Status::AwaitAdvance; ++i) step(app, {}, 0.05);
    CHECK_EQ(app.player()->text().say->text.plain(), "Bye.");
    step(app, {nav(NavDir::Left)});
    for (int i = 0; i < 400 && app.player()->status() != Status::AwaitAdvance; ++i) step(app, {}, 0.05);
    CHECK_EQ(app.player()->text().say->text.plain(), "Hello.");
    step(app, {ev(InputEvent::Kind::Cancel)});
    CHECK(app.quit_requested());  // no picker: Esc quits as before
}
