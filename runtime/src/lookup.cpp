#include "vnr/rt/lookup.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "vnr/script_lines.h"
#include "vnpop_dict.h"
#include "vnpop_ledger.h"

namespace vnr::rt {

// ---- Dictionary

namespace {
// vnpop looks words up in a fixed line of VNPOP_MAX_CHARS_PER_LINE characters; longer text is
// looked up through a window that starts at the wanted character.
constexpr std::size_t kWindow = VNPOP_MAX_CHARS_PER_LINE;
constexpr std::size_t kPageCacheBytes = VNPOP_DICT_MAX_CACHE_PAGES * VNPOP_L2_PAGE_SIZE;

void fill_line(VnpopTextLine& line, const std::vector<char32_t>& text, std::size_t from) {
    std::memset(&line, 0, sizeof line);
    const std::size_t n = std::min(kWindow, text.size() - std::min(from, text.size()));
    for (std::size_t i = 0; i < n; ++i) line.chars[i].codepoint = static_cast<std::uint32_t>(text[from + i]);
    line.char_count = static_cast<std::uint16_t>(n);
}
}  // namespace

struct Dictionary::Impl {
    std::vector<std::uint8_t> bytes;  // vnpop reads from this memory
    std::vector<std::uint8_t> page_cache = std::vector<std::uint8_t>(kPageCacheBytes);
    VnpopDict dict{};
    bool loaded = false;
    VnpopTextLine line{};
    VnpopDictMatch matches[64];
};

Dictionary::Dictionary() : impl_(std::make_unique<Impl>()) {}

Dictionary::~Dictionary() {
    if (impl_->loaded) vnpop_dict_close(&impl_->dict);
}

bool Dictionary::load(std::vector<std::uint8_t> bytes) {
    Impl& m = *impl_;
    if (m.loaded) vnpop_dict_close(&m.dict);
    m.loaded = false;
    m.bytes = std::move(bytes);
    // vnpop keeps its index and page buffers in fixed pools; one dictionary at a time.
    vnpop_pool_reset(POOL_DICT_INDEX);
    vnpop_pool_reset(POOL_UI_STATE);
    if (m.bytes.empty() || !vnpop_dict_load_memory(&m.dict, m.bytes.data(), m.bytes.size())) {
        m.bytes.clear();
        return false;
    }
    vnpop_dict_set_page_cache(&m.dict, m.page_cache.data(), m.page_cache.size());
    m.loaded = true;
    return true;
}

bool Dictionary::loaded() const { return impl_->loaded; }

std::vector<DictEntry> Dictionary::lookup(const std::vector<char32_t>& text, std::size_t start, std::size_t max) {
    std::vector<DictEntry> out;
    Impl& m = *impl_;
    if (!m.loaded || start >= text.size()) return out;
    fill_line(m.line, text, start);
    const std::size_t cap = std::min(max, sizeof m.matches / sizeof m.matches[0]);
    const std::size_t n = vnpop_dict_lookup_all(&m.dict, &m.line, 0, m.matches, cap);
    char head[256], reading[256];
    std::vector<char> gloss(8192);
    for (std::size_t i = 0; i < n; ++i) {
        const VnpopDictMatch& mt = m.matches[i];
        if (!vnpop_dict_read_entry(&m.dict, &mt, head, sizeof head, reading, sizeof reading, gloss.data(), gloss.size()))
            continue;
        DictEntry e;
        e.headword = head;
        e.reading = reading;
        e.glossary = gloss.data();
        e.rule = mt.rule;
        e.matched = mt.matched_chars;
        e.pos_mask = mt.pos_mask;
        // 30000+: a word the frequency list does not have (dict_packer.py FREQ_RANK_LIMIT), or a
        // reading-only key (+32768): no rank to show
        e.freq_rank = mt.freq_rank < 30000 ? mt.freq_rank : 0;
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<std::size_t> Dictionary::word_starts(const std::vector<char32_t>& text) {
    std::vector<std::size_t> out;
    Impl& m = *impl_;
    if (!m.loaded) return out;
    static VnpopLineTokens tokens;  // ~400 bytes; the App is single-threaded
    std::size_t from = 0;
    while (from < text.size()) {
        fill_line(m.line, text, from);
        vnpop_dict_tokenize_line(&m.dict, &m.line, &tokens);
        std::size_t next = from + m.line.char_count;  // the window was fully tokenised
        for (std::uint16_t t = 0; t < tokens.token_count; ++t) {
            const VnpopTokenSpan& s = tokens.tokens[t];
            if (s.is_dict_word) out.push_back(from + s.start_char_idx);
            next = from + s.start_char_idx + s.char_count;
        }
        // A window that ran out of tokens or characters part-way through a word: carry on from the
        // last token's end (at least one character on, so this always advances).
        from = std::max(next, from + 1);
        if (tokens.token_count < VNPOP_MAX_TOKENS_PER_LINE && m.line.char_count < kWindow) break;
    }
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

int box_at(const std::vector<GlyphBox>& boxes, Vec2 p) {
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const Rect& r = boxes[i].rect;
        if (p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h) return static_cast<int>(i);
    }
    return -1;
}

// ---- LookupPopup: Hoshi Reader's layout (its popup.css) in a dark sepia theme: a rounded panel, the
// reading over a large headword, inflection pills, a frequency chip, then a rounded box per
// dictionary with numbered senses whose glosses are split by " | ". Drawn with the OS UI font
// (the App's popup atlas), not the game font.

namespace {

constexpr float kWidth = 600;
constexpr float kPad = 14;            // panel padding
constexpr float kMaxHeight = 400;
constexpr float kHeadPx = 38, kRubyPx = 17;
constexpr float kPillPx = 15, kPillPadX = 6, kPillPadY = 4, kPillGap = 4;
constexpr float kGlossPx = 20, kGlossLine = 28;
constexpr float kNumIndent = 30;      // "1." column
constexpr float kGroupPad = 10;
// Dark sepia: warm browns with parchment-coloured ink (all opaque, so edges show only at the rim).
constexpr Color kPanel{0.141f, 0.106f, 0.078f, 1};      // #241b14
constexpr Color kPanelEdge{0.42f, 0.34f, 0.25f, 1};     // #6b5640
constexpr Color kShadowC{0, 0, 0, 0.45f};
constexpr Color kText{0.937f, 0.89f, 0.80f, 1};         // #efe3cc
constexpr Color kReading{0.79f, 0.72f, 0.60f, 1};       // #c9b79a
constexpr Color kSubtle{0.63f, 0.55f, 0.44f, 1};        // #a08c70
constexpr Color kPill{0.24f, 0.19f, 0.14f, 1};          // #3d3024
constexpr Color kPillText{0.90f, 0.84f, 0.73f, 1};      // #e6d6bb
constexpr Color kFreq{0.72f, 0.54f, 0.29f, 1};          // #b8894a
constexpr Color kGroupFill{0.18f, 0.137f, 0.10f, 1};    // #2e231a
constexpr Color kGroupEdge{0.30f, 0.24f, 0.18f, 1};     // #4d3d2d
constexpr Color kRule{0.30f, 0.24f, 0.18f, 1};          // the hr between entries
constexpr Color kWordMark{0.72f, 0.54f, 0.29f, 0.38f};  // the sepia box over the looked-up word

// A run of plain text (no script tags: glossaries contain brackets the script parser would eat).
RichText plain(const std::string& s) {
    RichText rt;
    Span sp;
    sp.text = s;
    sp.glyphs = utf8::count(s);
    rt.glyph_count = sp.glyphs;
    rt.spans.push_back(std::move(sp));
    return rt;
}

TextStyleBox style(float x, float y, float w, float px, float line, Color ink, int max_lines = 1) {
    TextStyleBox tb;
    tb.box = {x, y, w, line * static_cast<float>(max_lines)};
    tb.px = px;
    tb.line_height = line;
    tb.max_lines = max_lines;
    tb.first_line = 0;
    tb.ink = ink;
    tb.snap = true;
    return tb;
}

// Top of a text box whose glyphs sit centred in a band [top, top + h) (cap height ~0.72 of the size).
float centred(float top, float h, float px) { return top + h / 2 + 0.36f * px - px; }

// A filled rectangle with rounded corners, from one rect per corner row (the DrawList has no paths).
void round_rect(DrawList& out, const Rect& r, float radius, Color c) {
    const float rad = std::min({radius, r.w / 2, r.h / 2});
    const int rows = static_cast<int>(std::ceil(rad));
    for (int k = 0; k < rows; ++k) {
        const float dy = rad - (static_cast<float>(k) + 0.5f);
        const float inset = rad - std::sqrt(std::max(0.f, rad * rad - dy * dy));
        const float h = std::min(1.f, rad - static_cast<float>(k));
        out.rect({r.x + inset, r.y + static_cast<float>(k), r.w - 2 * inset, h}, c);
        out.rect({r.x + inset, r.y + r.h - static_cast<float>(k) - h, r.w - 2 * inset, h}, c);
    }
    out.rect({r.x, r.y + rad, r.w, r.h - 2 * rad}, c);
}

// Rounded box with a 1-unit edge (both colours opaque, so the edge shows only at the rim).
void framed(DrawList& out, const Rect& r, float radius, Color fill, Color edge) {
    round_rect(out, r, radius, edge);
    round_rect(out, {r.x + 1, r.y + 1, r.w - 2, r.h - 2}, radius - 1, fill);
}

// "1) a; b 2) c" -> {"a | b", "c"}: one paragraph per sense, glosses split by " | " as Hoshi does.
std::vector<std::string> senses(const std::string& g) {
    std::vector<std::string> raw;
    std::size_t begin = 0;
    const auto strip_number = [](std::string s) {
        std::size_t j = 0;
        while (j < s.size() && s[j] >= '0' && s[j] <= '9') ++j;
        if (j > 0 && j + 1 < s.size() && s[j] == ')' && s[j + 1] == ' ') s.erase(0, j + 2);
        return s;
    };
    for (std::size_t i = 1; i + 2 < g.size(); ++i) {
        if (g[i - 1] != ' ' || !(g[i] >= '1' && g[i] <= '9')) continue;
        std::size_t j = i;
        while (j < g.size() && g[j] >= '0' && g[j] <= '9') ++j;
        if (j + 1 < g.size() && g[j] == ')' && g[j + 1] == ' ') {
            raw.push_back(strip_number(g.substr(begin, i - 1 - begin)));
            begin = i;
        }
    }
    raw.push_back(strip_number(g.substr(begin)));
    for (std::string& s : raw)
        for (std::size_t p = 0; (p = s.find("; ", p)) != std::string::npos; p += 3) s.replace(p, 2, " | ");
    return raw;
}

std::vector<std::string> pos_tags(std::uint16_t mask) {
    static const std::pair<std::uint16_t, const char*> kTags[] = {
        {VNPOP_POS_NOUN, "n"},        {VNPOP_POS_VERB_ICHIDAN, "v1"}, {VNPOP_POS_VERB_GODAN, "v5"},
        {VNPOP_POS_VERB_SURU, "vs"},  {VNPOP_POS_VERB_KURU, "vk"},    {VNPOP_POS_ADJ_I, "adj-i"},
        {VNPOP_POS_ADJ_NA, "adj-na"}, {VNPOP_POS_ADVERB, "adv"},      {VNPOP_POS_PREFIX, "pref"},
        {VNPOP_POS_SUFFIX, "suf"},    {VNPOP_POS_PARTICLE, "prt"},
    };
    std::vector<std::string> out;
    if (mask == VNPOP_POS_ALL) return out;
    for (const auto& [bit, name] : kTags)
        if (mask & bit) out.emplace_back(name);
    return out;
}

std::vector<std::string> split_rule(const std::string& rule) {
    std::vector<std::string> out;
    std::size_t b = 0;
    while (b <= rule.size()) {
        const std::size_t e = std::min(rule.find('+', b), rule.size());
        if (e > b) out.push_back(rule.substr(b, e - b));
        b = e + 1;
    }
    return out;
}

// Lays out (and with `out`, draws) one entry at (x, y), `w` wide. Returns its height. Lines past
// `bottom` are left out.
float entry(GlyphAtlas& atlas, const DictEntry& e, const std::string& dict_name, float x, float y, float w,
            float bottom, DrawList* out) {
    float cy = y;
    // reading as ruby, centred over the headword
    RichText head = plain(e.headword);
    const float hw = text_width(atlas, head, kHeadPx);
    if (!e.reading.empty() && e.reading != e.headword) {
        RichText rd = plain(e.reading);
        const float rw = text_width(atlas, rd, kRubyPx);
        const float rx = x + std::max(0.f, (hw - rw) / 2);
        if (out) draw_text(atlas, rd, rd.glyph_count, style(rx, cy, rw + 4, kRubyPx, kRubyPx * 1.3f, kReading), *out);
    }
    cy += kRubyPx * 1.3f;
    if (out) draw_text(atlas, head, head.glyph_count, style(x, cy, w, kHeadPx, kHeadPx * 1.25f, kText), *out);
    cy += kHeadPx * 1.25f;

    // pills: inflection steps, then the frequency chip
    float px = x;
    const float pill_h = kPillPx + 2 * kPillPadY;
    bool any = false;
    const auto place = [&](float pw) {
        if (px > x && px + pw > x + w) {
            px = x;
            cy += pill_h + kPillGap;
        }
        const float at = px;
        px += pw + kPillGap;
        any = true;
        return at;
    };
    for (const std::string& step : split_rule(e.rule)) {
        RichText t = plain(step);
        const float tw = text_width(atlas, t, kPillPx);
        const float at = place(tw + 2 * kPillPadX);
        if (out) {
            round_rect(*out, {at, cy, tw + 2 * kPillPadX, pill_h}, 4, kPill);
            draw_text(atlas, t, t.glyph_count, style(at + kPillPadX, centred(cy, pill_h, kPillPx), tw + 4, kPillPx, pill_h, kPillText), *out);
        }
    }
    if (e.freq_rank > 0) {
        RichText label = plain("Freq"), value = plain(std::to_string(e.freq_rank));
        const float lw = text_width(atlas, label, kPillPx) + 2 * kPillPadX;
        const float vw = text_width(atlas, value, kPillPx) + 2 * kPillPadX;
        const float at = place(lw + vw);
        if (out) {
            framed(*out, {at, cy, lw + vw, pill_h}, 4, kPanel, kFreq);
            round_rect(*out, {at, cy, lw + 2, pill_h}, 4, kFreq);
            out->rect({at + lw - 2, cy, 2, pill_h}, kFreq);  // square join with the value half
            draw_text(atlas, label, label.glyph_count, style(at + kPillPadX, centred(cy, pill_h, kPillPx), lw, kPillPx, pill_h, kPanel), *out);
            draw_text(atlas, value, value.glyph_count, style(at + lw + kPillPadX, centred(cy, pill_h, kPillPx), vw, kPillPx, pill_h, kText), *out);
        }
    }
    if (any) cy += pill_h + 8;

    // the dictionary's box: "▼ name", part-of-speech pills, numbered senses
    const float gy = cy + 4;
    const float gx = x - 4, gw = w + 8;
    float iy = gy + kGroupPad;
    const float ix = gx + kGroupPad, iw = gw - 2 * kGroupPad;
    DrawList inner;
    DrawList* io = out ? &inner : nullptr;
    {
        RichText name = plain(dict_name);
        if (io) {
            for (int k = 0; k < 6; ++k)  // ▼ from rows of a shrinking bar (no glyph needed)
                io->rect({ix + static_cast<float>(k) * 0.5f, iy + 5 + static_cast<float>(k), 9 - static_cast<float>(k), 1}, kSubtle);
            draw_text(atlas, name, name.glyph_count, style(ix + 15, iy, iw, kPillPx, kPillPx * 1.4f, kSubtle), *io);
        }
        iy += kPillPx * 1.4f + 4;
    }
    const std::vector<std::string> tags = pos_tags(e.pos_mask);
    if (!tags.empty()) {
        float tx = ix;
        for (const std::string& tg : tags) {
            RichText t = plain(tg);
            const float tw = text_width(atlas, t, kPillPx) + 2 * kPillPadX;
            if (tx > ix && tx + tw > ix + iw) {
                tx = ix;
                iy += pill_h + kPillGap;
            }
            if (io) {
                round_rect(*io, {tx, iy, tw, pill_h}, 4, kPill);
                draw_text(atlas, t, t.glyph_count, style(tx + kPillPadX, centred(iy, pill_h, kPillPx), tw, kPillPx, pill_h, kPillText), *io);
            }
            tx += tw + kPillGap;
        }
        iy += pill_h + 6;
    }
    const std::vector<std::string> ss = senses(e.glossary);
    for (std::size_t i = 0; i < ss.size(); ++i) {
        RichText t = plain(ss[i]);
        const float tx = ss.size() > 1 ? ix + kNumIndent : ix;  // numbered only when there are several
        const TextStyleBox measure = style(tx, iy, ix + iw - tx, kGlossPx, kGlossLine, kText, 99);
        const int lines = std::max(1, count_lines(atlas, t, measure));
        const int room = static_cast<int>(std::floor((bottom - iy) / kGlossLine + 0.01f));
        const int shown = out ? std::min(lines, room) : lines;
        if (shown <= 0) break;
        if (io) {
            if (ss.size() > 1) {
                RichText num = plain(std::to_string(i + 1) + ".");
                draw_text(atlas, num, num.glyph_count, style(ix + 2, iy, kNumIndent, kGlossPx, kGlossLine, kText), *io);
            }
            draw_text(atlas, t, t.glyph_count, style(tx, iy, ix + iw - tx, kGlossPx, kGlossLine, kText, shown), *io);
        }
        iy += static_cast<float>(shown) * kGlossLine;
    }
    iy += kGroupPad - 6;
    if (out) {
        const float gh = std::min(iy, bottom) - gy;
        out->rect({gx + 2, gy + gh, gw - 4, 1}, {0, 0, 0, 0.3f});  // 1px drop shadow
        framed(*out, {gx, gy, gw, gh}, 8, kGroupFill, kGroupEdge);
        out->quads.insert(out->quads.end(), inner.quads.begin(), inner.quads.end());
    }
    return iy - y + 8;
}

}  // namespace

bool LookupPopup::open(Dictionary& dict, const LookupTarget& target, std::size_t index) {
    std::vector<char32_t> text;
    text.reserve(target.boxes.size());
    for (const GlyphBox& b : target.boxes) text.push_back(b.cp);
    std::vector<DictEntry> found = dict.lookup(text, index);
    if (found.empty()) {
        close();
        return false;
    }
    if (!open_ || target.id != target_.id || text != text_) {
        words_.clear();
        words_ready_ = false;
    }
    open_ = true;
    target_ = target;
    text_ = std::move(text);
    start_ = index;
    entries_ = std::move(found);
    selected_ = 0;
    scroll_acc_ = 0;
    return true;
}

bool LookupPopup::open_first(Dictionary& dict, const LookupTarget& target) {
    std::vector<char32_t> text;
    for (const GlyphBox& b : target.boxes) text.push_back(b.cp);
    for (std::size_t w : dict.word_starts(text))
        if (open(dict, target, w)) return true;
    close();
    return false;
}

void LookupPopup::close() {
    open_ = false;
    entries_.clear();
    rect_ = {};
}

bool LookupPopup::hop(Dictionary& dict, int dir) {
    if (!open_) return false;
    if (!words_ready_) {
        words_ = dict.word_starts(text_);
        words_ready_ = true;
    }
    const LookupTarget target = target_;
    const std::vector<std::size_t> words = words_;
    const std::size_t from = start_;
    if (dir > 0) {
        for (std::size_t w : words)
            if (w > from && open(dict, target, w)) return true;
    } else {
        for (auto it = words.rbegin(); it != words.rend(); ++it)
            if (*it < from && open(dict, target, *it)) return true;
    }
    if (!open_) open(dict, target, from);  // nothing further that way: stay on the current word
    return false;
}

void LookupPopup::select(int delta) {
    if (entries_.empty()) return;
    const long n = static_cast<long>(entries_.size());
    selected_ = static_cast<std::size_t>(std::clamp(static_cast<long>(selected_) + delta, 0L, n - 1));
}

void LookupPopup::scroll(float dy) {
    scroll_acc_ += dy;
    constexpr float kStep = 40;
    while (scroll_acc_ >= kStep) { scroll_acc_ -= kStep; select(+1); }
    while (scroll_acc_ <= -kStep) { scroll_acc_ += kStep; select(-1); }
}

bool LookupPopup::contains(Vec2 p) const {
    return open_ && p.x >= rect_.x && p.x < rect_.x + rect_.w && p.y >= rect_.y && p.y < rect_.y + rect_.h;
}

void LookupPopup::draw(GlyphAtlas& atlas, DrawList& out) {
    if (!open_ || entries_.empty() || !atlas.ok()) return;
    // The grey box over the characters the top entry covers (Hoshi marks the word the same way).
    const std::size_t end = std::min(target_.boxes.size(), start_ + entries_[selected_].matched);
    float ax0 = 1e9f, ay0 = 1e9f, ax1 = -1e9f, ay1 = -1e9f;
    for (std::size_t i = start_; i < end; ++i) {
        const Rect& r = target_.boxes[i].rect;
        ax0 = std::min(ax0, r.x);
        ay0 = std::min(ay0, r.y);
        ax1 = std::max(ax1, r.x + r.w);
        ay1 = std::max(ay1, r.y + r.h);
    }
    if (ax0 > ax1) return;
    out.rect({ax0, ay0, ax1 - ax0, ay1 - ay0}, kWordMark);

    // Entries from the selected one down, as many as fit (the DrawList cannot clip, so the popup
    // pages by entry: up / down or the wheel move the top entry).
    const float iw = kWidth - 2 * kPad;
    std::vector<float> heights;
    float total = kPad;
    std::size_t last = selected_;
    for (std::size_t i = selected_; i < entries_.size(); ++i) {
        const float h = entry(atlas, entries_[i], dict_name_, 0, 0, iw, 1e9f, nullptr) + (i > selected_ ? 9 : 0);
        if (i > selected_ && total + h > kMaxHeight - kPad) break;
        heights.push_back(h);
        total += h;
        last = i + 1;
    }
    const bool paged = entries_.size() > 1;
    const float h = std::min(total + kPad - 6, kMaxHeight);

    // Above the word when there is room (the dialogue sits at the bottom), else below it.
    const float x_min = kViewSafeLeft + 8, x_max = kViewSafeLeft + kViewSafeWidth - 8 - kWidth;
    const float x = std::clamp(ax0 - 24, x_min, std::max(x_min, x_max));
    // Over the dialogue, the popup goes above the whole text so it hides none of it.
    float block_top = ay0;
    if (target_.id == 0)
        for (const GlyphBox& b : target_.boxes) block_top = std::min(block_top, b.rect.y);
    float y = block_top - 10 - h;
    if (y < kViewTop + 8) y = std::min(ay1 + 10, kViewTop + kViewHeight - 8 - h);
    y = std::max(y, kViewTop + 8);
    rect_ = {x, y, kWidth, h};
    round_rect(out, {x - 1, y + 1, kWidth + 2, h + 3}, 9, kShadowC);
    framed(out, rect_, 8, kPanel, kPanelEdge);

    float cy = y + kPad;
    const float bottom = y + h - kPad;
    for (std::size_t i = selected_; i < last && cy < bottom; ++i) {
        if (i > selected_) {
            out.rect({x + kPad, cy, iw, 1}, kRule);
            cy += 9;
        }
        cy += entry(atlas, entries_[i], dict_name_, x + kPad, cy, iw, bottom, &out);
    }
    if (paged) {
        RichText pos = plain(std::to_string(selected_ + 1) + " / " + std::to_string(entries_.size()));
        const float pw = text_width(atlas, pos, kPillPx);
        // top right, where Hoshi keeps its buttons
        draw_text(atlas, pos, pos.glyph_count, style(x + kWidth - kPad - pw, y + kPad, pw + 4, kPillPx, kPillPx * 1.4f, kSubtle), out);
    }
}

}  // namespace vnr::rt
