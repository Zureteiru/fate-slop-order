#include "vnr/rt/picker.h"

#include <algorithm>
#include <cstdlib>

#include "vnr/rt/app.h"

namespace vnr::rt {

namespace {

// Fate Café's reader home palette (dark brown page, gold accents, cream titles).
constexpr Color kBg{0.149f, 0.122f, 0.106f, 1};
constexpr Color kPanel{0.110f, 0.090f, 0.078f, 1};
constexpr Color kBorder{0.353f, 0.290f, 0.267f, 1};
constexpr Color kLine{0.235f, 0.192f, 0.169f, 1};
constexpr Color kGold{0.831f, 0.639f, 0.451f, 1};
constexpr Color kCream{0.937f, 0.890f, 0.827f, 1};
constexpr Color kBody{0.796f, 0.725f, 0.651f, 1};
constexpr Color kDim{0.561f, 0.490f, 0.439f, 1};
constexpr Color kInk{0.110f, 0.094f, 0.078f, 1};
constexpr Color kFocusFill{0.235f, 0.188f, 0.161f, 1};

constexpr const char* kDot = " \xC2\xB7 ";
constexpr const char* kEllipsis = "\xE2\x80\xA6";
constexpr const char* kArrowSuffix = "  \xE2\x86\x92";

Color faded(Color c, float a) { return {c.r, c.g, c.b, c.a * a}; }

void outline(DrawList& out, const Rect& r, float t, Color c) {
    out.rect({r.x, r.y, r.w, t}, c);
    out.rect({r.x, r.y + r.h - t, r.w, t}, c);
    out.rect({r.x, r.y + t, t, r.h - 2 * t}, c);
    out.rect({r.x + r.w - t, r.y + t, t, r.h - 2 * t}, c);
}

void pop_codepoint(std::string& s) {
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
}

}  // namespace

Picker::Picker(Platform& platform, AssetCache& cache, GlyphAtlas& atlas)
    : platform_(platform), cache_(cache), atlas_(atlas) {
    nav_.set_view(lay_.view_top, lay_.view_height);
    load_progress();
    load_catalogue();
    go(Screen::Home);
}

// ---- catalogue and progress

void Picker::load_catalogue() {
    std::vector<std::uint8_t> bytes;
    if (platform_.user && platform_.user->read(kCatalogueUserKey, bytes)) {
        if (adopt(bytes)) {
            from_user_ = true;
            platform_.log.write(std::string("catalogue: using ") + kCatalogueUserKey + " from the user folder");
            return;
        }
        platform_.log.write(std::string(kCatalogueUserKey) + " could not be read as a catalogue; downloading instead");
    }
    // The last download first, so the list shows at once; then one fresh download per launch.
    if (platform_.files.read(AssetCache::cache_key(kCatalogueUrl, {}), bytes)) adopt(bytes);
    refresh();
}

void Picker::refresh() {
    failed_ = false;
    refreshing_ = true;
    cache_.request_fresh_bytes(kCatalogueUrl);
}

bool Picker::adopt(const std::vector<std::uint8_t>& bytes) {
    auto parsed = parse_catalogue(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!parsed) return false;
    const std::string keep = cat_ && chapter_ < cat_->chapters.size() ? cat_->chapters[chapter_].id : std::string();
    cat_ = std::move(parsed);
    if (!keep.empty()) {
        if (const auto i = cat_->chapter_index(keep)) chapter_ = *i;
        else if (screen_ != Screen::Home) screen_ = Screen::Home;
    }
    return true;
}

void Picker::load_progress() {
    std::vector<std::uint8_t> bytes;
    Files* f = platform_.user ? platform_.user : &platform_.files;
    if (f->read(kProgressKey, bytes)) progress_ = parse_progress(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

void Picker::save_progress() {
    const std::string s = serialize_progress(progress_);
    Files* f = platform_.user ? platform_.user : &platform_.files;
    if (!f->write(kProgressKey, {s.begin(), s.end()})) platform_.log.write(std::string("could not save ") + kProgressKey);
}

void Picker::update(double) {
    if (!refreshing_) return;
    const AssetState st = cache_.state(kCatalogueUrl);
    if (st == AssetState::Loading) return;
    refreshing_ = false;
    if (st == AssetState::Ready) {
        const auto bytes = cache_.take_bytes(kCatalogueUrl);
        const bool had = cat_.has_value();
        if (adopt(bytes)) {
            platform_.log.write("catalogue: " + std::to_string(cat_->chapters.size()) + " chapters (data version " +
                                std::to_string(cat_->data_version) + ")");
            const auto focus = nav_.focus();
            build();
            if (!had) go(screen_);
            else if (focus) nav_.set_focus(*focus, false);
            return;
        }
        platform_.log.write("catalogue: the download is not a reader.json");
    } else {
        cache_.forget(kCatalogueUrl);
    }
    if (!cat_) {
        failed_ = true;
        go(Screen::Home);
    }
}

bool Picker::ready() const {
    if (!cat_ || refreshing_) return false;
    for (const Item& it : items_) {
        if (!on_screen(it.rect)) continue;
        const std::string* url = nullptr;
        if (it.target == Target::Chapter) url = &cat_->chapters[it.index].banner_url;
        if (url && !url->empty() && cache_.state(*url) == AssetState::Loading) return false;
    }
    if (screen_ == Screen::Chapter && chapter_ < cat_->chapters.size()) {
        const std::string& url = cat_->chapters[chapter_].banner_url;
        if (!url.empty() && cache_.state(url) == AssetState::Loading) return false;
    }
    return true;
}

std::string Picker::status() const {
    if (!cat_) return failed_ ? "Could not download the chapter list" : "Loading chapters...";
    switch (screen_) {
        case Screen::Home: return "Chapters";
        case Screen::Chapter: return "Chapter " + chapter_id();
        case Screen::End: return "End of part " + chapter_id() + " #" + std::to_string(section_);
    }
    return {};
}

std::string Picker::chapter_id() const {
    if (screen_ == Screen::Home || !cat_ || chapter_ >= cat_->chapters.size()) return {};
    return cat_->chapters[chapter_].id;
}

std::string Picker::focused_label() const {
    const auto f = nav_.focus();
    return f && *f < items_.size() ? items_[*f].label : std::string();
}

Rect Picker::item_rect(std::size_t i) const { return i < items_.size() ? to_screen(items_[i].rect) : Rect{}; }

// ---- screens

void Picker::open(const std::string& spec) {
    auto chapter_of = [&](const std::string& id) -> std::optional<std::size_t> {
        return cat_ ? cat_->chapter_index(id) : std::nullopt;
    };
    if (spec.rfind("chapter=", 0) == 0) {
        if (const auto c = chapter_of(spec.substr(8))) {
            chapter_ = *c;
            go(Screen::Chapter);
            return;
        }
    } else if (spec.rfind("end=", 0) == 0) {
        const std::string rest = spec.substr(4);
        const std::size_t colon = rest.find(':');
        if (const auto c = chapter_of(rest.substr(0, colon))) {
            chapter_ = *c;
            section_ = colon == std::string::npos ? 0 : static_cast<std::size_t>(std::atoi(rest.c_str() + colon + 1));
            if (section_ < cat_->chapters[*c].sections.size()) {
                go(Screen::End);
                return;
            }
        }
    }
    go(Screen::Home);
}

void Picker::go(Screen s, std::optional<std::size_t> focus_target) {
    screen_ = s;
    nav_.clear_focus();
    nav_.scroll_to(0);
    build();
    std::optional<std::size_t> f;
    if (focus_target) {
        const Target t = s == Screen::Home ? Target::Chapter : Target::Section;
        f = find_item(t, *focus_target);
    }
    if (!f) f = find_item(s == Screen::End ? Target::Continue : Target::Start, 0);
    if (!f && s == Screen::End) f = find_item(Target::BackToIndex, 0);
    if (!f && failed_) f = find_item(Target::Retry, 0);
    if (f) nav_.set_focus(*f);
}

std::optional<std::size_t> Picker::find_item(Target t, std::size_t index) const {
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (items_[i].target == t && (t == Target::Start || t == Target::Continue || t == Target::BackToIndex ||
                                      t == Target::Retry || t == Target::Region || items_[i].index == index))
            return i;
    return std::nullopt;
}

std::optional<std::size_t> Picker::next_playable(std::size_t chapter, std::size_t after) const {
    if (!cat_ || chapter >= cat_->chapters.size()) return std::nullopt;
    const auto& secs = cat_->chapters[chapter].sections;
    for (std::size_t i = after + 1; i < secs.size(); ++i)
        if (secs[i].playable()) return i;
    return std::nullopt;
}

void Picker::build() {
    items_.clear();
    const float L = lay_.left, W = lay_.width;
    float y = 0;
    switch (screen_) {
        case Screen::Home: {
            items_.push_back({Target::Region, 0, {L + W - 104, 28, 104, 34}, true, "Region"});
            if (!cat_) {
                if (failed_) items_.push_back({Target::Retry, 0, {L, 150, width_of("Retry", 17) + 48, 42}, true, "Retry"});
                content_height_ = lay_.view_height;
                break;
            }
            std::string label;
            const CatalogueChapter* last = cat_->chapter(progress_.last_chapter);
            std::size_t start_chapter = 0;
            if (last && progress_.last_section < last->sections.size() && last->sections[progress_.last_section].playable()) {
                label = std::string("Continue") + kDot + last->name;
                start_chapter = *cat_->chapter_index(last->id);
            } else {
                start_chapter = cat_->arcs.empty() || cat_->arcs[0].chapters.empty() ? 0 : cat_->arcs[0].chapters[0];
                label = std::string("Start") + kDot + cat_->chapters[start_chapter].name;
            }
            const float bw = std::min(W, width_of(label + kArrowSuffix, 17) + 56);
            items_.push_back({Target::Start, start_chapter, {L, 88, bw, 42}, true, label});
            y = 160;
            const float card_w = (W - lay_.card_gap * static_cast<float>(lay_.columns - 1)) / static_cast<float>(lay_.columns);
            const float card_h = card_w * lay_.banner_aspect + lay_.card_text_height;
            for (const CatalogueArc& arc : cat_->arcs) {
                y += 56;  // arc header
                for (std::size_t k = 0; k < arc.chapters.size(); ++k) {
                    const float col = static_cast<float>(k % static_cast<std::size_t>(lay_.columns));
                    const float row = static_cast<float>(k / static_cast<std::size_t>(lay_.columns));
                    const Rect r{L + col * (card_w + lay_.card_gap), y + row * (card_h + lay_.card_row_gap), card_w, card_h};
                    items_.push_back({Target::Chapter, arc.chapters[k], r, true, cat_->chapters[arc.chapters[k]].name});
                }
                const std::size_t rows = (arc.chapters.size() + static_cast<std::size_t>(lay_.columns) - 1) / static_cast<std::size_t>(lay_.columns);
                y += static_cast<float>(rows) * (card_h + lay_.card_row_gap) + 12;
            }
            content_height_ = y + 12;
            break;
        }
        case Screen::Chapter: {
            const CatalogueChapter& ch = cat_->chapters[chapter_];
            items_.push_back({Target::BackLink, 0, {L - 8, 12, width_of("\xE2\x86\x90 All chapters", 15) + 16, 30}, true, "All chapters"});
            if (const auto s = next_unread(ch, progress_)) {
                const std::string label = "Start: " + ch.sections[*s].name;
                items_.push_back({Target::Start, *s, {L, 214, std::min(W * 0.6f, width_of(label, 17) + 56), 44}, true, label});
            }
            y = 330;
            for (std::size_t i = 0; i < ch.sections.size(); ++i) {
                items_.push_back({Target::Section, i, {L, y, W, lay_.row_height}, ch.sections[i].playable(), ch.sections[i].name});
                y += lay_.row_height;
            }
            content_height_ = y + 40;
            break;
        }
        case Screen::End: {
            const CatalogueChapter& ch = cat_->chapters[chapter_];
            const float cx = 392, cw = 560;
            float by = 262;
            if (const auto next = next_playable(chapter_, section_)) {
                items_.push_back({Target::Continue, *next, {cx + 32, by, cw - 64, 62}, true, "Continue to " + ch.sections[*next].name});
                by += 62 + 14;
            }
            items_.push_back({Target::BackToIndex, 0, {cx + 32, by, cw - 64, 46}, true, "Back to index"});
            content_height_ = lay_.view_height;
            break;
        }
    }
    std::vector<Rect> rects;
    std::vector<bool> enabled;
    for (const Item& it : items_) {
        rects.push_back(it.rect);
        enabled.push_back(it.enabled);
    }
    nav_.set_items(std::move(rects), std::move(enabled), content_height_);
}

// ---- input

Picker::Action Picker::handle(const InputEvent& e) {
    using K = InputEvent::Kind;
    switch (e.kind) {
        case K::Nav: nav_.move(static_cast<NavDir>(e.index)); break;
        case K::Scroll: nav_.scroll_by(e.pos.y); break;
        case K::Pointer: nav_.press(e.pos); break;
        case K::PointerMove: nav_.drag(e.pos); break;
        case K::PointerUp:
            if (const auto i = nav_.release(e.pos)) return activate(*i);
            break;
        case K::Advance:
            if (const auto f = nav_.focus()) return activate(*f);
            nav_.move(NavDir::Down);
            break;
        case K::Back: back(); break;
        case K::Cancel:
            if (screen_ == Screen::Home) return {Action::Kind::Quit, {}};
            back();
            break;
        default: break;
    }
    return {};
}

void Picker::back() {
    switch (screen_) {
        case Screen::Home: break;
        case Screen::Chapter: go(Screen::Home, chapter_); break;
        case Screen::End: go(Screen::Chapter, next_playable(chapter_, section_).value_or(section_)); break;
    }
}

Picker::Action Picker::activate(std::size_t i) {
    if (i >= items_.size() || !items_[i].enabled) return {};
    const Item it = items_[i];
    switch (it.target) {
        case Target::Region:
            progress_.region = progress_.region == Region::NA ? Region::JP : Region::NA;
            save_progress();
            platform_.log.write(std::string("picker: reading in ") + (progress_.region == Region::JP ? "JP" : "EN"));
            build();
            break;
        case Target::Retry:
            refresh();
            build();
            break;
        case Target::Start:
            if (screen_ == Screen::Home) {
                const CatalogueChapter* last = cat_->chapter(progress_.last_chapter);
                if (last && it.index == *cat_->chapter_index(last->id)) return play(it.index, progress_.last_section);
                if (const auto s = next_unread(cat_->chapters[it.index], progress_)) return play(it.index, *s);
                break;
            }
            return play(chapter_, it.index);
        case Target::Chapter:
            chapter_ = it.index;
            go(Screen::Chapter);
            break;
        case Target::BackLink: go(Screen::Home, chapter_); break;
        case Target::Section: return play(chapter_, it.index);
        case Target::Continue: return play(chapter_, it.index);
        case Target::BackToIndex: back(); break;
    }
    return {};
}

Picker::Action Picker::play(std::size_t chapter, std::size_t section) {
    if (!cat_ || chapter >= cat_->chapters.size()) return {};
    auto part = part_for(*cat_, cat_->chapters[chapter].id, section, progress_.region);
    if (!part) return {};
    chapter_ = chapter;
    section_ = section;
    progress_.opened(part->chapter_id, section);
    save_progress();
    return {Action::Kind::Play, std::move(*part)};
}

void Picker::part_finished(const PartChoice& part) {
    progress_.mark_read(part.chapter_id, part.section);
    save_progress();
    if (cat_) {
        if (const auto c = cat_->chapter_index(part.chapter_id)) chapter_ = *c;
    }
    section_ = part.section;
    go(cat_ ? Screen::End : Screen::Home);
}

void Picker::part_left(const PartChoice& part) {
    if (cat_) {
        if (const auto c = cat_->chapter_index(part.chapter_id)) {
            chapter_ = *c;
            go(Screen::Chapter, part.section);
            return;
        }
    }
    go(Screen::Home);
}

// ---- drawing

float Picker::width_of(const std::string& s, float px) { return text_width(atlas_, parse_rich_text(s, {}), px); }

float Picker::text(DrawList& out, const std::string& s, float x, float baseline, float px, Color c, float max_w, bool right) {
    std::string shown = s;
    float w = width_of(shown, px);
    if (max_w > 0 && w > max_w) {
        while (!shown.empty() && w > max_w) {
            pop_codepoint(shown);
            while (!shown.empty() && shown.back() == ' ') shown.pop_back();
            w = width_of(shown + kEllipsis, px);
        }
        shown += kEllipsis;
    }
    const RichText rt = parse_rich_text(shown, {});
    TextStyleBox box;
    box.px = px;
    box.line_height = px * 1.3f;
    box.max_lines = 1;
    box.ink = c;
    box.box = {right ? x - w : x, baseline - px, 100000, px * 1.6f};
    draw_text(atlas_, rt, rt.glyph_count, box, out);
    return w;
}

Rect Picker::to_screen(const Rect& r) const { return {r.x, nav_.screen_y(r.y), r.w, r.h}; }

bool Picker::on_screen(const Rect& r) const {
    const Rect s = to_screen(r);
    return s.y + s.h > lay_.view_top && s.y < lay_.view_top + lay_.view_height;
}

void Picker::draw_focus(DrawList& out, const Rect& s) { outline(out, {s.x - 4, s.y - 4, s.w + 8, s.h + 8}, 2, kGold); }

void Picker::draw_banner(DrawList& out, const std::string& url, const Rect& s) {
    out.rect(s, kPanel);
    if (url.empty()) return;
    cache_.request_texture(url);
    if (const TextureAsset* t = cache_.texture(url)) {
        out.image(t->id, {0, 0, static_cast<float>(t->width), static_cast<float>(t->height)}, s);
    }
    outline(out, s, 1, faded(kBorder, 0.8f));
}

void Picker::draw(DrawList& out, const DrawList* backdrop) {
    out.clear = kBg;
    if (screen_ == Screen::End) {
        draw_end(out, backdrop);
    } else {
        out.rect({0, 0, kStageWidth, kStageHeight}, kBg);
        if (screen_ == Screen::Home) draw_home(out);
        else draw_chapter(out);
    }
    atlas_.flush();
}

void Picker::draw_home(DrawList& out) {
    const float L = lay_.left, W = lay_.width;
    auto sy = [&](float y) { return nav_.screen_y(y); };
    const auto focus = nav_.focus();
    text(out, "FGO Reader", L, sy(64), 38, kCream);
    // EN / JP switch (Fate Café's language pills); the lit half is the region parts are read in.
    {
        const Rect r = to_screen(items_[0].rect);
        out.rect(r, kPanel);
        outline(out, r, 1, kBorder);
        const bool jp = progress_.region == Region::JP;
        const Rect lit{jp ? r.x + r.w / 2 + 3 : r.x + 3, r.y + 3, r.w / 2 - 6, r.h - 6};
        out.rect(lit, kGold);
        text(out, "EN", r.x + r.w / 4 - width_of("EN", 15) / 2, r.y + 23, 15, jp ? kBody : kInk);
        text(out, "JP", r.x + 3 * r.w / 4 - width_of("JP", 15) / 2, r.y + 23, 15, jp ? kInk : kBody);
        if (focus == std::size_t{0}) draw_focus(out, r);
    }
    out.rect({L, sy(144), W, 1}, kLine);
    if (!cat_) {
        text(out, status(), L, sy(122), 18, kBody);
        if (failed_ && items_.size() > 1) {
            const Rect r = to_screen(items_[1].rect);
            out.rect(r, kGold);
            text(out, "Retry", r.x + 28, r.y + 27, 17, kInk);
            if (focus == std::size_t{1}) draw_focus(out, r);
        }
        return;
    }
    for (std::size_t i = 1; i < items_.size(); ++i) {
        const Item& it = items_[i];
        if (!on_screen(it.rect)) continue;
        const Rect r = to_screen(it.rect);
        if (it.target == Target::Start) {
            out.rect(r, kGold);
            text(out, it.label + kArrowSuffix, r.x + 28, r.y + 27, 17, kInk, r.w - 40);
        } else {
            const CatalogueChapter& ch = cat_->chapters[it.index];
            const float bh = r.w * lay_.banner_aspect;
            if (focus == i) out.rect({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, kFocusFill);
            draw_banner(out, ch.banner_url, {r.x, r.y, r.w, bh});
            text(out, ch.label, r.x, r.y + bh + 26, 12, kGold, r.w);
            text(out, ch.name, r.x, r.y + bh + 52, 21, kCream, r.w);
            text(out, chapter_meta(ch), r.x, r.y + bh + 74, 13, kDim, r.w);
        }
        if (focus == i) draw_focus(out, r);
    }
    // arc headers: "Arc 1 ——— 10 chapters", 56 above each arc's first card
    std::size_t item = 2;
    for (const CatalogueArc& arc : cat_->arcs) {
        if (item >= items_.size()) break;
        const float top = items_[item].rect.y - 56;
        const float base = sy(top + 36);
        if (base > -40 && base < lay_.view_height + 40) {
            const float nw = text(out, arc.name, L, base, 28, kCream);
            const std::string count = std::to_string(arc.chapters.size()) + (arc.chapters.size() == 1 ? " chapter" : " chapters");
            const float cw = text(out, count, L + W, base - 4, 13, kDim, 0, true);
            out.rect({L + nw + 16, base - 10, std::max(0.f, W - nw - cw - 32), 1}, kLine);
        }
        item += arc.chapters.size();
    }
}

void Picker::draw_chapter(DrawList& out) {
    const CatalogueChapter& ch = cat_->chapters[chapter_];
    const float L = lay_.left, W = lay_.width;
    auto sy = [&](float y) { return nav_.screen_y(y); };
    const auto focus = nav_.focus();
    const float banner_w = 300, text_w = W - banner_w - 32;
    text(out, ch.label, L, sy(84), 13, kGold, text_w);
    text(out, ch.name, L, sy(130), 40, kCream, text_w);
    text(out, ch.long_name, L, sy(164), 17, kBody, text_w);
    int scenes = 0;
    for (const auto& s : ch.sections) scenes += static_cast<int>(s.scripts.size());
    std::string meta = std::to_string(ch.sections.size()) + (ch.sections.size() == 1 ? " part" : " parts") + kDot +
                       std::to_string(scenes) + (scenes == 1 ? " scene" : " scenes");
    if (ch.duration > 0) meta += kDot + format_duration(ch.duration);
    meta += kDot + std::string(progress_.region == Region::JP ? "JP" : "EN");
    text(out, meta, L, sy(194), 14, kDim, text_w);
    draw_banner(out, ch.banner_url, {L + W - banner_w, sy(66), banner_w, banner_w * lay_.banner_aspect});
    // "Chapter parts 目次 ——— 31 scenes · 1h 44min"
    const float hb = sy(306);
    const float nw = text(out, "Chapter parts", L, hb, 28, kCream);
    const float kw = text(out, "\xE7\x9B\xAE\xE6\xAC\xA1", L + nw + 10, hb - 4, 14, kGold);
    std::string right = std::to_string(scenes) + (scenes == 1 ? " scene" : " scenes");
    if (ch.duration > 0) right += kDot + format_duration(ch.duration);
    const float rw = text(out, right, L + W, hb - 4, 13, kDim, 0, true);
    out.rect({L + nw + kw + 28, hb - 10, std::max(0.f, W - nw - kw - rw - 44), 1}, kLine);

    for (std::size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        if (!on_screen(it.rect)) continue;
        const Rect r = to_screen(it.rect);
        switch (it.target) {
            case Target::BackLink:
                text(out, "\xE2\x86\x90 All chapters", r.x + 8, r.y + 21, 15, focus == i ? kCream : kBody);
                if (focus == i) draw_focus(out, r);
                break;
            case Target::Start:
                out.rect(r, kGold);
                text(out, it.label, r.x + 28, r.y + 28, 17, kInk, r.w - 40);
                if (focus == i) draw_focus(out, r);
                break;
            case Target::Section: {
                const CatalogueSection& s = ch.sections[it.index];
                const float a = s.playable() ? 1.f : 0.45f;
                if (focus == i) {
                    out.rect(r, kFocusFill);
                    out.rect({r.x, r.y, 3, r.h}, kGold);
                }
                out.rect({r.x, r.y, r.w, 1}, kLine);
                if (s.number > 0) {
                    const std::string n = std::to_string(s.number);
                    text(out, n, r.x + 30 - width_of(n, 22) / 2, r.y + 38, 22, faded(kGold, a));
                }
                const bool read = progress_.is_read(ch.id, it.index);
                text(out, s.name, r.x + 64, r.y + 28, 18, faded(kCream, a), r.w - 64 - 90);
                text(out, section_meta(s), r.x + 64, r.y + 50, 13, faded(kDim, a), r.w - 64 - 90);
                if (read) text(out, "\xE2\x9C\x93 Read", r.x + r.w - 16, r.y + 37, 13, kGold, 0, true);
                break;
            }
            default: break;
        }
    }
}

void Picker::draw_end(DrawList& out, const DrawList* backdrop) {
    if (backdrop && !backdrop->quads.empty()) {
        out.quads = backdrop->quads;
        out.rect({0, 0, kStageWidth, kStageHeight}, {0, 0, 0, 0.6f});
    } else {
        out.rect({0, 0, kStageWidth, kStageHeight}, kBg);
    }
    const CatalogueChapter& ch = cat_->chapters[chapter_];
    const CatalogueSection& done = ch.sections[section_];
    const float cx = 392, cw = 560, top = 166;
    const float bottom = items_.empty() ? 330 : items_.back().rect.y + items_.back().rect.h + 30;
    out.rect({cx, top, cw, bottom - top}, faded(kPanel, 0.96f));
    outline(out, {cx, top, cw, bottom - top}, 1, kBorder);
    text(out, "END OF PART", cx + 32, top + 34, 13, kGold);
    text(out, done.label(), cx + 32, top + 70, 24, kCream, cw - 64);
    const auto focus = nav_.focus();
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        const Rect r = to_screen(it.rect);
        if (it.target == Target::Continue) {
            out.rect(r, kGold);
            const CatalogueSection& next = ch.sections[it.index];
            text(out, it.label, r.x + 20, r.y + (next.spot.empty() ? 37 : 28), 17, kInk, r.w - 60);
            if (!next.spot.empty()) text(out, next.spot, r.x + 20, r.y + 49, 13, faded(kInk, 0.7f), r.w - 60);
            text(out, "\xE2\x86\x92", r.x + r.w - 20, r.y + 38, 18, kInk, 0, true);
        } else {
            outline(out, r, 1, kBorder);
            text(out, "\xE2\x86\x90 " + it.label, r.x + 20, r.y + 29, 15, kBody, r.w - 40);
        }
        if (focus == i) draw_focus(out, r);
    }
}

}  // namespace vnr::rt
