// Japanese dictionary lookups: the vnpop dictionary (third_party/switch-vnpop) and the popup that
// shows its entries for a word of the dialogue or the log.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "vnr/rt/draw_list.h"
#include "vnr/rt/text.h"

namespace vnr::rt {

// The packed dictionary (tools/install_dict.sh), in the user folder. Never committed.
inline constexpr const char* kDictKey = "dict.bin";

struct DictEntry {
    std::string headword, reading, glossary;
    std::string rule;          // inflections undone, e.g. "-te+past"; empty for an exact match
    std::size_t matched = 0;   // characters of the text this entry covers
    int freq_rank = 0;         // 1 = most common; 0 = unranked
    std::uint16_t pos_mask = 0;  // VNPOP_POS_* bits
};

class Dictionary {
public:
    Dictionary();
    ~Dictionary();
    Dictionary(const Dictionary&) = delete;
    Dictionary& operator=(const Dictionary&) = delete;

    // Takes the whole dict.bin. False (and not loaded) when it is not a vnpop dictionary.
    bool load(std::vector<std::uint8_t> bytes);
    bool loaded() const;

    // Entries for the text starting at `start`, in Yomitan order (longest match, fewest
    // inflection steps, most common first).
    std::vector<DictEntry> lookup(const std::vector<char32_t>& text, std::size_t start, std::size_t max = 16);
    // Where the dictionary words of `text` start (the d-pad hops between these).
    std::vector<std::size_t> word_starts(const std::vector<char32_t>& text);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Text the reader can look words up in: the drawn characters of one dialogue line or log entry.
struct LookupTarget {
    int id = 0;  // which text (0 = the story's dialogue, 1 + n = the n-th drawn log entry)
    std::vector<GlyphBox> boxes;
};

// Index of the box under `p`, or -1.
int box_at(const std::vector<GlyphBox>& boxes, Vec2 p);

class LookupPopup {
public:
    bool is_open() const { return open_; }
    // Looks up the word starting at character `index`. False (and closed) when nothing matches.
    bool open(Dictionary& dict, const LookupTarget& target, std::size_t index);
    // Opens on the first word that has an entry (the controller's lookup button).
    bool open_first(Dictionary& dict, const LookupTarget& target);
    void close();

    // Next (+1) / previous (-1) dictionary word of the same text. False when there is none.
    bool hop(Dictionary& dict, int dir);
    void select(int delta);   // moves the top entry shown (the popup pages by entry)
    void scroll(float dy);    // wheel / stick: + shows later entries
    bool contains(Vec2 p) const;  // over the popup as last drawn

    int target_id() const { return target_.id; }
    std::size_t start() const { return start_; }
    const std::vector<DictEntry>& entries() const { return entries_; }
    std::size_t selected() const { return selected_; }

    // The looked-up word's highlight and the popup above (or below) it, in Hoshi Reader's style.
    void draw(GlyphAtlas& atlas, DrawList& out);

private:
    bool open_ = false;
    LookupTarget target_;
    std::vector<char32_t> text_;
    std::vector<std::size_t> words_;  // filled on the first hop
    bool words_ready_ = false;
    std::size_t start_ = 0;
    std::vector<DictEntry> entries_;
    std::size_t selected_ = 0;
    std::string dict_name_ = "JMdict";  // the dictionary box's title (dict.bin has no name field)
    float scroll_acc_ = 0;
    Rect rect_{};  // the popup as last drawn
};

}  // namespace vnr::rt
