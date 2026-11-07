// Focus and scrolling for a list or grid of buttons, the same for every input: d-pad / stick /
// arrow keys move the focus to the neighbouring button, the wheel / stick / drag scroll, a tap picks.
// Used by the chapter picker; meant for the story's choice buttons too. Pure logic, no drawing.
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "vnr/assets.h"
#include "vnr/command.h"

namespace vnr::rt {

enum class NavDir { Up, Down, Left, Right };

class FocusNav {
public:
    static constexpr float kTapSlop = 10;    // a press that moves less than this is a tap
    static constexpr float kRevealMargin = 24;

    // The visible band of the screen (stage y) the content scrolls through.
    void set_view(float top, float height);
    // Buttons in content coordinates (y = 0 at the top of the scrolling content). Keeps the focus
    // index when it is still valid and enabled.
    void set_items(std::vector<Rect> rects, std::vector<bool> enabled, float content_height);

    std::optional<std::size_t> focus() const { return focus_; }
    void set_focus(std::size_t index, bool reveal = true);
    void clear_focus() { focus_.reset(); }
    // Moves to the nearest enabled button in that direction: Up/Down to the closest row above or
    // below (then the closest horizontally), Left/Right within the same row. With no focus yet it
    // focuses the first visible enabled button. Returns false when nothing changed.
    bool move(NavDir d);

    void scroll_by(float dy);  // + scrolls down; clamped. The focus moves onto the screen if it left it.
    void scroll_to(float y);
    float scroll() const { return scroll_; }
    float max_scroll() const;

    // Pointer in stage coordinates. release() returns the enabled button tapped (a press and release
    // that didn't drag), and focuses it.
    void press(Vec2 p);
    void drag(Vec2 p);
    std::optional<std::size_t> release(Vec2 p);
    bool dragging() const { return dragging_; }

    std::optional<std::size_t> hit(Vec2 screen) const;  // the enabled button under a stage point
    float screen_y(float content_y) const { return top_ + content_y - scroll_; }
    std::size_t size() const { return rects_.size(); }

private:
    std::vector<Rect> rects_;
    std::vector<bool> enabled_;
    float content_ = 0, top_ = 0, height_ = 0, scroll_ = 0;
    std::optional<std::size_t> focus_;
    bool pressed_ = false, dragging_ = false;
    Vec2 press_pos_;
    float press_scroll_ = 0;

    bool usable(std::size_t i) const { return i < rects_.size() && enabled_[i]; }
    bool visible(std::size_t i) const;
    void reveal(std::size_t i);
    void keep_focus_visible();
};

}  // namespace vnr::rt
