// Chapter / section title cards (bit_chapterstart*, bit_queststart*), drawn like Fate Café's CSS card:
// a 2.6 s band across the middle of the view (black .62 fading out at both ends, gold hairlines above
// and below) that opens from the centre, with a small label and a large title in the game font.
#pragma once

#include <string>

#include "vnr/rt/draw_list.h"
#include "vnr/rt/text.h"
#include "vnr/stage.h"

namespace vnr::rt {

// What the cards say, from the catalogue for the part being read (empty = no card, as in Fate Café).
struct TitleTexts {
    std::string chapter_label;  // "PROLOGUE"
    std::string chapter_title;  // "Flame Contaminated City: Fuyuki"
    std::string section_label;  // "Section 3" / "第3節"
    std::string section_title;  // the section's name
};

// Fate Café's keyframes at time t (0..1 of the card): opacity, and how much of the band is open.
float title_card_opacity(float t);
float title_card_open(float t);
// CSS cubic-bezier(x1, y1, x2, y2) at x.
float css_bezier(float x1, float y1, float x2, float y2, float x);

void draw_title_card(const TitleCard& card, const TitleTexts& texts, GlyphAtlas& atlas, DrawList& out);

}  // namespace vnr::rt
