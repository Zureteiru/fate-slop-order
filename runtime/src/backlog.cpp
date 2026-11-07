#include "vnr/rt/backlog.h"

#include <algorithm>
#include <cmath>

namespace vnr::rt {

namespace {
constexpr Color kInk{0.96f, 0.96f, 0.96f, 1};
constexpr Color kName{0.78f, 0.88f, 1, 1};
constexpr Color kShadow{0, 0, 0, 0.75f};
}  // namespace

TextStyleBox Backlog::text_box() const {
    TextStyleBox tb;
    tb.box = {st_.text_x, 0, st_.text_w, st_.line_height};
    tb.px = st_.text_px;
    tb.line_height = st_.line_height;
    tb.ink = kInk;
    tb.shadow = kShadow;
    return tb;
}

void Backlog::open(const std::vector<BacklogEntry>& entries, GlyphAtlas& atlas) {
    rows_.clear();
    const TextStyleBox tb = text_box();
    float y = 0;
    for (const BacklogEntry& e : entries) {
        Row r;
        r.y = y;
        r.name = !e.speaker.spans.empty() && !e.speaker.plain().empty();
        r.lines = std::max(1, count_lines(atlas, e.text, tb));
        rows_.push_back(r);
        y += (r.name ? st_.name_height : 0) + static_cast<float>(r.lines) * st_.line_height + st_.entry_gap;
    }
    content_ = y;
    open_ = true;
    pressed_ = dragging_ = false;
    scroll_ = max_scroll();
}

void Backlog::close() {
    open_ = pressed_ = dragging_ = false;
}

float Backlog::max_scroll() const { return std::max(0.f, content_ - view_height()); }

void Backlog::scroll_by(float dy) { scroll_ = std::clamp(scroll_ + dy, 0.f, max_scroll()); }

void Backlog::press(Vec2 p) {
    pressed_ = true;
    dragging_ = false;
    press_pos_ = p;
    press_scroll_ = scroll_;
}

void Backlog::drag(Vec2 p) {
    if (!pressed_) return;
    const float dy = p.y - press_pos_.y;
    if (!dragging_ && std::hypot(p.x - press_pos_.x, dy) >= st_.tap_slop) dragging_ = true;
    if (dragging_) scroll_ = std::clamp(press_scroll_ - dy, 0.f, max_scroll());  // the list follows the finger
}

bool Backlog::release(Vec2 p) {
    if (!pressed_) return false;
    drag(p);
    const bool tap = !dragging_;
    pressed_ = dragging_ = false;
    return tap;
}

void Backlog::draw(const std::vector<BacklogEntry>& entries, GlyphAtlas& atlas, DrawList& out,
                   std::vector<LookupTarget>* targets) const {
    if (!open_) return;
    out.rect({0, 0, static_cast<float>(kStageWidth), static_cast<float>(kStageHeight)}, {0, 0, 0, st_.dim});

    // Lines are drawn only when their whole band is inside the visible band (the DrawList has no clip).
    const auto fits = [&](float y, float h) { return y >= st_.top - 0.5f && y + h <= st_.bottom + 0.5f; };
    const std::size_t n = std::min(entries.size(), rows_.size());
    for (std::size_t i = 0; i < n; ++i) {
        const Row& r = rows_[i];
        float y = st_.top + r.y - scroll_;
        const float h = (r.name ? st_.name_height : 0) + static_cast<float>(r.lines) * st_.line_height;
        if (y > st_.bottom || y + h < st_.top) continue;
        if (r.name) {
            if (fits(y, st_.name_height)) {
                TextStyleBox nb;
                nb.box = {st_.name_x, y + st_.baseline - st_.name_px, st_.text_w, st_.name_height};
                nb.px = st_.name_px;
                nb.max_lines = 1;
                nb.ink = kName;
                nb.shadow = kShadow;
                draw_text(atlas, entries[i].speaker, entries[i].speaker.glyph_count, nb, out);
            }
            y += st_.name_height;
        }
        int first = -1, count = 0;
        for (int l = 0; l < r.lines; ++l) {
            if (!fits(y + static_cast<float>(l) * st_.line_height, st_.line_height)) continue;
            if (first < 0) first = l;
            ++count;
        }
        if (count == 0) continue;
        TextStyleBox tb = text_box();
        tb.box.y = y + static_cast<float>(first) * st_.line_height + st_.baseline - st_.text_px;
        tb.first_line = first;
        tb.max_lines = count;
        if (targets) {
            targets->push_back({static_cast<int>(1 + i), {}});
            tb.boxes = &targets->back().boxes;
        }
        draw_text(atlas, entries[i].text, entries[i].text.glyph_count, tb, out);
    }

    // scrollbar: thin track, white thumb sized to the share of the log on screen
    const Rect& t = st_.track;
    const Color edge{0.8f, 0.8f, 0.85f, 0.7f};
    out.rect({t.x, t.y, t.w, 1}, edge);
    out.rect({t.x, t.y + t.h - 1, t.w, 1}, edge);
    out.rect({t.x, t.y, 1, t.h}, edge);
    out.rect({t.x + t.w - 1, t.y, 1, t.h}, edge);
    const float share = content_ > 0 ? std::min(1.f, view_height() / content_) : 1.f;
    const float th = std::max(28.f, (t.h - 4) * share);
    const float travel = t.h - 4 - th;
    const float ty = t.y + 2 + (max_scroll() > 0 ? travel * scroll_ / max_scroll() : 0);
    out.rect({t.x + 2, ty, t.w - 4, th}, {0.78f, 0.62f, 0.3f, 1});  // gold edge
    out.rect({t.x + 3.5f, ty + 1.5f, t.w - 7, th - 3}, {0.97f, 0.97f, 0.98f, 1});
}

}  // namespace vnr::rt
