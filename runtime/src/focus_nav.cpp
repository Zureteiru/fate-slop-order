#include "vnr/rt/focus_nav.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vnr::rt {

namespace {
float centre_x(const Rect& r) { return r.x + r.w / 2; }
float centre_y(const Rect& r) { return r.y + r.h / 2; }
bool overlaps_vertically(const Rect& a, const Rect& b) { return a.y < b.y + b.h && b.y < a.y + a.h; }
}  // namespace

void FocusNav::set_view(float top, float height) {
    top_ = top;
    height_ = std::max(0.f, height);
    scroll_ = std::clamp(scroll_, 0.f, max_scroll());
}

void FocusNav::set_items(std::vector<Rect> rects, std::vector<bool> enabled, float content_height) {
    rects_ = std::move(rects);
    enabled_ = std::move(enabled);
    enabled_.resize(rects_.size(), true);
    content_ = content_height;
    scroll_ = std::clamp(scroll_, 0.f, max_scroll());
    if (focus_ && !usable(*focus_)) focus_.reset();
}

float FocusNav::max_scroll() const { return std::max(0.f, content_ - height_); }

void FocusNav::scroll_to(float y) { scroll_ = std::clamp(y, 0.f, max_scroll()); }

void FocusNav::set_focus(std::size_t index, bool reveal_it) {
    if (!usable(index)) return;
    focus_ = index;
    if (reveal_it) reveal(index);
}

bool FocusNav::visible(std::size_t i) const {
    const Rect& r = rects_[i];
    return r.y >= scroll_ - 0.5f && r.y + r.h <= scroll_ + height_ + 0.5f;
}

void FocusNav::reveal(std::size_t i) {
    const Rect& r = rects_[i];
    if (r.y - kRevealMargin < scroll_) scroll_to(r.y - kRevealMargin);
    else if (r.y + r.h + kRevealMargin > scroll_ + height_) scroll_to(r.y + r.h + kRevealMargin - height_);
}

bool FocusNav::move(NavDir d) {
    if (!focus_) {
        for (std::size_t i = 0; i < rects_.size(); ++i)
            if (usable(i) && visible(i)) {
                set_focus(i);
                return true;
            }
        for (std::size_t i = 0; i < rects_.size(); ++i)
            if (usable(i)) {
                set_focus(i);
                return true;
            }
        return false;
    }
    const Rect& cur = rects_[*focus_];
    std::optional<std::size_t> best;
    float best_primary = std::numeric_limits<float>::max(), best_secondary = best_primary;
    for (std::size_t i = 0; i < rects_.size(); ++i) {
        if (i == *focus_ || !usable(i)) continue;
        const Rect& r = rects_[i];
        float primary = 0, secondary = 0;
        switch (d) {
            case NavDir::Down:
                if (r.y < cur.y + cur.h - 1) continue;  // must start below the current button
                primary = r.y;
                secondary = std::fabs(centre_x(r) - centre_x(cur));
                break;
            case NavDir::Up:
                if (r.y + r.h > cur.y + 1) continue;
                primary = -(r.y + r.h);
                secondary = std::fabs(centre_x(r) - centre_x(cur));
                break;
            case NavDir::Right:
                if (!overlaps_vertically(r, cur) || centre_x(r) <= centre_x(cur)) continue;
                primary = centre_x(r) - centre_x(cur);
                secondary = std::fabs(centre_y(r) - centre_y(cur));
                break;
            case NavDir::Left:
                if (!overlaps_vertically(r, cur) || centre_x(r) >= centre_x(cur)) continue;
                primary = centre_x(cur) - centre_x(r);
                secondary = std::fabs(centre_y(r) - centre_y(cur));
                break;
        }
        // Up/Down: the nearest row first (1 unit of tolerance), then the closest horizontally.
        const bool rows = d == NavDir::Up || d == NavDir::Down;
        const bool better = rows ? (primary < best_primary - 1 || (primary <= best_primary + 1 && secondary < best_secondary))
                                 : (primary < best_primary || (primary == best_primary && secondary < best_secondary));
        if (better) {
            best = i;
            best_primary = primary;
            best_secondary = secondary;
        }
    }
    if (!best) {
        // Nothing further that way: still bring the edge of the content into view.
        if (d == NavDir::Up && scroll_ > 0) { scroll_to(rects_[*focus_].y - kRevealMargin); return false; }
        if (d == NavDir::Down && scroll_ < max_scroll()) { scroll_to(rects_[*focus_].y + rects_[*focus_].h + kRevealMargin - height_); }
        return false;
    }
    set_focus(*best);
    return true;
}

void FocusNav::keep_focus_visible() {
    if (!focus_ || visible(*focus_)) return;
    const float want = centre_y(rects_[*focus_]);
    std::optional<std::size_t> best;
    float best_d = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < rects_.size(); ++i) {
        if (!usable(i) || !visible(i)) continue;
        const float dist = std::fabs(centre_y(rects_[i]) - want) * 4 + std::fabs(centre_x(rects_[i]) - centre_x(rects_[*focus_]));
        if (dist < best_d) {
            best_d = dist;
            best = i;
        }
    }
    if (best) focus_ = best;
}

void FocusNav::scroll_by(float dy) {
    scroll_to(scroll_ + dy);
    keep_focus_visible();
}

void FocusNav::press(Vec2 p) {
    pressed_ = true;
    dragging_ = false;
    press_pos_ = p;
    press_scroll_ = scroll_;
}

void FocusNav::drag(Vec2 p) {
    if (!pressed_) return;
    if (!dragging_ && std::fabs(p.y - press_pos_.y) < kTapSlop && std::fabs(p.x - press_pos_.x) < kTapSlop) return;
    dragging_ = true;
    scroll_to(press_scroll_ - (p.y - press_pos_.y));
    keep_focus_visible();
}

std::optional<std::size_t> FocusNav::release(Vec2 p) {
    const bool tap = pressed_ && !dragging_;
    pressed_ = dragging_ = false;
    if (!tap) return std::nullopt;
    const auto h = hit(p);
    if (h) focus_ = h;
    return h;
}

std::optional<std::size_t> FocusNav::hit(Vec2 s) const {
    if (s.y < top_ || s.y > top_ + height_) return std::nullopt;
    const float cy = s.y - top_ + scroll_;
    for (std::size_t i = 0; i < rects_.size(); ++i) {
        const Rect& r = rects_[i];
        if (usable(i) && s.x >= r.x && s.x <= r.x + r.w && cy >= r.y && cy <= r.y + r.h) return i;
    }
    return std::nullopt;
}

}  // namespace vnr::rt
