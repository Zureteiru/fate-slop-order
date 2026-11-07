#include "vnr/rt/save_screen.h"

#include <algorithm>

#include "vnr/rich_text.h"
#include "vnr/rt/app.h"

namespace vnr::rt {
namespace {

constexpr Color kWhite{1, 1, 1, 1};
constexpr Color kSoft{1, 1, 1, 0.55f};
constexpr Color kGold{0.831f, 0.639f, 0.451f, 1};
constexpr Color kCard{0, 0, 0, 0.7f};
constexpr Color kCardFocus{0.235f, 0.188f, 0.161f, 0.9f};
constexpr float kSaveViewHeight = 576;

void outline(DrawList& out, const Rect& r, float t, Color c) {
    out.rect({r.x, r.y, r.w, t}, c);
    out.rect({r.x, r.y + r.h - t, r.w, t}, c);
    out.rect({r.x, r.y + t, t, r.h - 2 * t}, c);
    out.rect({r.x + r.w - t, r.y + t, t, r.h - 2 * t}, c);
}

bool inside(const Rect& r, Vec2 p) { return p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h; }

// One line of text from the top-left of `r` (wrapped text keeps only its first line).
void label(GlyphAtlas& atlas, const std::string& s, Rect r, float px, Color ink, DrawList& out) {
    if (s.empty()) return;
    const RichText t = parse_rich_text(s, {});
    TextStyleBox box;
    box.box = r;
    box.px = px;
    box.line_height = px * 1.4f;
    box.max_lines = 1;
    box.first_line = 0;
    box.ink = ink;
    box.shadow = {0, 0, 0, 0.6f};
    box.shadow_offset = {1.5f, 1.5f};
    draw_text(atlas, t, t.glyph_count, box, out);
}

void centred(GlyphAtlas& atlas, const std::string& s, Rect r, float px, Color ink, DrawList& out) {
    const float w = text_width(atlas, parse_rich_text(s, {}), px);
    label(atlas, s, {r.x + (r.w - w) / 2, r.y + (r.h - px * 1.4f) / 2, w + 4, px * 1.4f}, px, ink, out);
}

std::string slot_name(int slot) {
    if (slot == kAutosaveSlot) return "Auto";
    if (slot == kQuickSlot) return "Quick";
    const int n = slot + 1;
    return std::string("No.") + (n < 10 ? "0" : "") + std::to_string(n);
}

}  // namespace

const std::optional<SaveEntry>& SaveScreen::entry(const SaveFile& saves, int slot) {
    static const std::optional<SaveEntry> none;
    if (slot == kAutosaveSlot) return saves.autosave;
    if (slot == kQuickSlot) return saves.quick;
    if (slot >= 0 && static_cast<std::size_t>(slot) < kSaveSlots) return saves.slots[static_cast<std::size_t>(slot)];
    return none;
}

void SaveScreen::open(Mode mode, const SaveFile& saves, bool confirm_load) {
    mode_ = mode;
    open_ = true;
    confirm_load_ = confirm_load;
    confirm_.reset();
    page_ = 0;
    if (mode == Mode::Load && !saves.autosave && !saves.quick) page_ = 1;
    nav_ = FocusNav();
    nav_.set_view(0, kSaveViewHeight);
    refresh(saves);
    nav_.set_focus(page_, false);  // the current tab, unless a card can be used
    for (std::size_t i = 0; i < card_count(); ++i)
        if (mode_ == Mode::Save || entry(saves, slot_of_card(i))) {
            nav_.set_focus(page_count() + i, false);
            break;
        }
}

void SaveScreen::close() {
    open_ = false;
    confirm_.reset();
}

void SaveScreen::set_page(std::size_t page, const SaveFile& saves) {
    page_ = std::min(page, page_count() - 1);
    nav_.clear_focus();
    refresh(saves);
    nav_.set_focus(page_, false);  // stay on the tab
}

int SaveScreen::slot_of_card(std::size_t i) const {
    if (mode_ == Mode::Load) {
        if (page_ == 0) return i == 0 ? kAutosaveSlot : kQuickSlot;
        return static_cast<int>((page_ - 1) * kSlotsPerPage + i);
    }
    return static_cast<int>(page_ * kSlotsPerPage + i);
}

std::string SaveScreen::tab_label(std::size_t i) const {
    if (mode_ == Mode::Load) return i == 0 ? "Auto / Quick" : std::to_string(i);
    return std::to_string(i + 1);
}

Rect SaveScreen::tab_rect(std::size_t i) const {
    // Right-aligned to the grid's right edge; the wide "Auto / Quick" tab comes first on Load.
    const float right = lay_.grid_x + 2 * lay_.card_w + lay_.col_gap;
    auto width = [&](std::size_t k) { return mode_ == Mode::Load && k == 0 ? 150.f : 64.f; };
    float total = 0;
    for (std::size_t k = 0; k < page_count(); ++k) total += width(k) + (k ? lay_.tab_gap : 0);
    float x = right - total;
    for (std::size_t k = 0; k < i; ++k) x += width(k) + lay_.tab_gap;
    return {x, lay_.tabs_y, width(i), lay_.tab_h};
}

Rect SaveScreen::card_rect(std::size_t i) const {
    const float col = static_cast<float>(i % 2), row = static_cast<float>(i / 2);
    return {lay_.grid_x + col * (lay_.card_w + lay_.col_gap), lay_.grid_y + row * (lay_.card_h + lay_.row_gap),
            lay_.card_w, lay_.card_h};
}

void SaveScreen::refresh(const SaveFile& saves) {
    std::vector<Rect> rects;
    std::vector<bool> enabled;
    for (std::size_t i = 0; i < page_count(); ++i) {
        rects.push_back(tab_rect(i));
        enabled.push_back(true);
    }
    for (std::size_t i = 0; i < card_count(); ++i) {
        rects.push_back(card_rect(i));
        enabled.push_back(mode_ == Mode::Save || entry(saves, slot_of_card(i)).has_value());
    }
    nav_.set_items(std::move(rects), std::move(enabled), kSaveViewHeight);
}

Rect SaveScreen::yes_rect() const {
    const Rect& d = lay_.dialog;
    return {d.x + d.w / 2 - 170, d.y + d.h - 62, 150, 44};
}

Rect SaveScreen::no_rect() const {
    const Rect& d = lay_.dialog;
    return {d.x + d.w / 2 + 20, d.y + d.h - 62, 150, 44};
}

SaveScreen::Action SaveScreen::answer(bool yes) {
    const Action a = yes ? confirm_->action : Action{};
    confirm_.reset();
    return a;
}

SaveScreen::Action SaveScreen::activate(std::size_t item, const SaveFile& saves) {
    if (item < page_count()) {
        set_page(item, saves);
        return {};
    }
    const int slot = slot_of_card(item - page_count());
    const bool filled = entry(saves, slot).has_value();
    if (mode_ == Mode::Save) {
        const Action a{Action::Kind::Save, slot};
        if (!filled) return a;
        confirm_ = Confirm{a, "Overwrite " + slot_name(slot) + "?", false};
        return {};
    }
    if (!filled) return {};
    const Action a{Action::Kind::Load, slot};
    if (!confirm_load_) return a;
    confirm_ = Confirm{a, "Load " + slot_name(slot) + "? Reading since the last save is lost.", false};
    return {};
}

SaveScreen::Action SaveScreen::handle(const InputEvent& e, const SaveFile& saves) {
    using K = InputEvent::Kind;
    if (confirm_) {
        switch (e.kind) {
            case K::Nav: {
                const auto d = static_cast<NavDir>(e.index);
                if (d == NavDir::Left || d == NavDir::Right) confirm_->yes = !confirm_->yes;
                return {};
            }
            case K::Advance: return answer(confirm_->yes);
            case K::Back:
            case K::Cancel: return answer(false);
            case K::PointerUp:
                if (inside(yes_rect(), e.pos)) return answer(true);
                if (inside(no_rect(), e.pos)) return answer(false);
                return {};
            default: return {};
        }
    }
    switch (e.kind) {
        case K::Nav: nav_.move(static_cast<NavDir>(e.index)); return {};
        case K::Advance:
            if (const auto f = nav_.focus()) return activate(*f, saves);
            return {};
        case K::Back:
        case K::Cancel: return {Action::Kind::Close, 0};
        case K::Pointer: nav_.press(e.pos); return {};
        case K::PointerMove: nav_.drag(e.pos); return {};
        case K::PointerUp:
            if (const auto hit = nav_.release(e.pos)) return activate(*hit, saves);
            return {};
        case K::OpenLog:  // X on a controller: delete the focused save
        case K::DeleteSave: {
            const auto f = nav_.focus();
            if (!f || *f < page_count()) return {};
            const int slot = slot_of_card(*f - page_count());
            if (!entry(saves, slot)) return {};
            confirm_ = Confirm{{Action::Kind::Delete, slot}, "Delete " + slot_name(slot) + "?", false};
            return {};
        }
        case K::OpenSave:
        case K::OpenLoad: {
            const Mode m = e.kind == K::OpenSave ? Mode::Save : Mode::Load;
            if (m != mode_) open(m, saves, confirm_load_);
            return {};
        }
        default: return {};
    }
}

void SaveScreen::draw(const SaveFile& saves, AssetCache& cache, GlyphAtlas& atlas, DrawList& out) const {
    if (!open_) return;
    out.rect({0, 0, static_cast<float>(kStageWidth), static_cast<float>(kStageHeight)}, {0, 0, 0, lay_.dim});
    label(atlas, mode_ == Mode::Save ? "SAVE" : "LOAD", lay_.title, 30, kWhite, out);
    const auto focus = nav_.focus();

    for (std::size_t i = 0; i < page_count(); ++i) {
        const Rect r = tab_rect(i);
        const bool current = i == page_, focused = focus && *focus == i;
        if (current) out.rect(r, {1, 1, 1, 0.18f});
        outline(out, r, focused ? 2.5f : 1.5f, focused ? kGold : kSoft);
        centred(atlas, tab_label(i), r, 18, current ? kWhite : kSoft, out);
    }

    for (std::size_t i = 0; i < card_count(); ++i) {
        const Rect r = card_rect(i);
        const int slot = slot_of_card(i);
        const auto& e = entry(saves, slot);
        const bool focused = focus && *focus == page_count() + i;
        out.rect(r, focused ? kCardFocus : kCard);
        outline(out, r, focused ? 2.5f : 1.5f, focused ? kGold : kSoft);
        const Rect th{r.x + lay_.thumb.x, r.y + lay_.thumb.y, lay_.thumb.w, lay_.thumb.h};
        const float tx = th.x + th.w + 12, tw = r.x + r.w - tx - 10;
        if (!e) {
            out.rect(th, {1, 1, 1, 0.06f});
            label(atlas, slot_name(slot), {tx, r.y + 8, tw, 26}, 18, kSoft, out);
            label(atlas, "Empty", {tx, r.y + 40, tw, 26}, 18, {1, 1, 1, 0.35f}, out);
            continue;
        }
        const TextureAsset* tex = e->background.empty() ? nullptr : cache.texture(background_url(e->background, e->region));
        if (tex && tex->width > 0 && tex->height > 0) {
            // cover the thumbnail, centred
            const float s = std::max(th.w / static_cast<float>(tex->width), th.h / static_cast<float>(tex->height));
            const float sw = th.w / s, sh = th.h / s;
            out.image(tex->id, {(static_cast<float>(tex->width) - sw) / 2, (static_cast<float>(tex->height) - sh) / 2, sw, sh}, th);
        } else {
            out.rect(th, {1, 1, 1, 0.1f});
        }
        outline(out, th, 1, kSoft);
        const std::string date = format_save_time(e->saved_at);
        const float dw = text_width(atlas, parse_rich_text(date, {}), 15);
        label(atlas, slot_name(slot), {tx, r.y + 6, tw - dw - 8, 24}, 17, kGold, out);
        label(atlas, date, {r.x + r.w - 10 - dw, r.y + 8, dw + 4, 22}, 15, kSoft, out);
        std::string where = e->chapter_label;
        if (!e->part_label.empty()) where += (where.empty() ? "" : " \xC2\xB7 ") + e->part_label;
        label(atlas, where, {tx, r.y + 32, tw, 24}, 16, kWhite, out);
        const std::string line = e->speaker.empty() ? e->text : e->speaker + ": " + e->text;
        label(atlas, line, {tx, r.y + 56, tw, 24}, 16, kSoft, out);
    }

    label(atlas, "A / tap: choose    B: close    X / Del: delete", {lay_.grid_x, 542, 900, 24}, 14, {1, 1, 1, 0.4f}, out);

    if (confirm_) {
        out.rect({0, 0, static_cast<float>(kStageWidth), static_cast<float>(kStageHeight)}, {0, 0, 0, 0.5f});
        const Rect& d = lay_.dialog;
        out.rect(d, {0.08f, 0.07f, 0.06f, 0.97f});
        outline(out, d, 2, kGold);
        centred(atlas, confirm_->message, {d.x + 20, d.y + 24, d.w - 40, 40}, 19, kWhite, out);
        const Rect yr = yes_rect(), nr = no_rect();
        outline(out, yr, confirm_->yes ? 2.5f : 1.5f, confirm_->yes ? kGold : kSoft);
        outline(out, nr, confirm_->yes ? 1.5f : 2.5f, confirm_->yes ? kSoft : kGold);
        centred(atlas, "Yes", yr, 19, kWhite, out);
        centred(atlas, "No", nr, 19, kWhite, out);
    }
}

}  // namespace vnr::rt
