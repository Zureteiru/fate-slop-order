// The LOG screen: past dialogue (Player::backlog) listed over the dimmed story screen, scrolled with
// the wheel, a drag or the controller, closed by a tap. Loosely after the game's log (names, the
// dialogue's own text column, a thin scrollbar on the right); not measured.
#pragma once

#include <vector>

#include "vnr/player.h"
#include "vnr/rt/draw_list.h"
#include "vnr/rt/text.h"

namespace vnr::rt {

struct BacklogStyle {
    float top = 18, bottom = 562;    // visible band (stage y; the game view is y 0-576)
    float name_x = 190, text_x = 232, text_w = 860;
    float name_px = 29, text_px = 28.7f;
    float name_height = 40, line_height = 47, entry_gap = 22;
    float baseline = 37;             // from the top of a line's band (leaves room for ruby above)
    Rect track{1156, 8, 17, 560};    // scrollbar
    float dim = 0.8f;                // black over the story screen (the current line stays faintly visible)
    float tap_slop = 10;             // a press that moves less than this is a tap (closes the log)
};

class Backlog {
public:
    // Lays the entries out and scrolls to the newest.
    void open(const std::vector<BacklogEntry>& entries, GlyphAtlas& atlas);
    void close();
    bool is_open() const { return open_; }

    void scroll_by(float dy);  // + shows newer lines, - older; clamped
    float scroll() const { return scroll_; }
    float max_scroll() const;
    float content_height() const { return content_; }

    // Pointer: press, move while pressed (drags the list), release. release() returns true for a tap.
    void press(Vec2 p);
    void drag(Vec2 p);
    bool release(Vec2 p);
    bool dragging() const { return dragging_; }

    void draw(const std::vector<BacklogEntry>& entries, GlyphAtlas& atlas, DrawList& out) const;
    const BacklogStyle& style() const { return st_; }

private:
    struct Row {
        float y = 0;  // from the top of the content
        int lines = 1;
        bool name = false;
    };
    BacklogStyle st_;
    std::vector<Row> rows_;
    float content_ = 0, scroll_ = 0;
    bool open_ = false, pressed_ = false, dragging_ = false;
    Vec2 press_pos_;
    float press_scroll_ = 0;

    TextStyleBox text_box() const;
    float view_height() const { return st_.bottom - st_.top; }
};

}  // namespace vnr::rt
