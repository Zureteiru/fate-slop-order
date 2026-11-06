#include "vnr/rich_text.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "vnr/script_lines.h"

namespace vnr {

namespace {

// Splits text into plain runs and top-level "[...]" groups (nested brackets stay inside the group).
std::vector<std::string_view> split_groups(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t start = 0, depth = 0, open = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '[') {
            if (depth == 0) {
                if (i > start) out.push_back(s.substr(start, i - start));
                open = i;
            }
            ++depth;
        } else if (s[i] == ']' && depth > 0) {
            if (--depth == 0) {
                out.push_back(s.substr(open, i - open + 1));
                start = i + 1;
            }
        }
    }
    if (depth > 0) start = open;  // unclosed bracket: keep it as text
    if (start < s.size()) out.push_back(s.substr(start));
    return out;
}

bool is_hex(std::string_view s) {
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return !s.empty();
}

// Runs of two or more "uXXXX" escapes are decoded (the game writes some symbols this way).
std::string decode_unicode_runs(std::string_view s) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t j = i;
        std::vector<char32_t> cps;
        while (j + 5 <= s.size() && s[j] == 'u' && is_hex(s.substr(j + 1, 4))) {
            cps.push_back(static_cast<char32_t>(std::strtoul(std::string(s.substr(j + 1, 4)).c_str(), nullptr, 16)));
            j += 5;
        }
        if (cps.size() >= 2) {
            for (char32_t cp : cps) utf8::append(out, cp);
            i = j;
        } else {
            out += s[i];
            ++i;
        }
    }
    return out;
}

std::optional<float> font_scale(std::string_view v) {
    if (v.empty() || v == "-") return std::nullopt;
    struct Named { const char* name; float px; };
    static const Named kNamed[] = {{"x-small", 19}, {"small", 24}, {"medium", 29}, {"large", 48}, {"x-large", 64}};
    for (const auto& n : kNamed)
        if (v == n.name) return n.px / 29.f;
    const auto num = parse_number(v);
    if (!num || *num <= 0) return std::nullopt;
    return std::clamp(static_cast<float>(*num / 29.0), 0.45f, 2.4f);
}

struct Builder {
    RichText out;
    TextStyle style;
    std::vector<std::optional<Rgba>> colors;  // colour stack (for [-])
    std::vector<float> fonts;                 // font stack (for [-])
    std::vector<char> tag_order;              // 'c' or 'f', most recent last

    void text(std::string_view s) {
        if (s.empty()) return;
        const std::string decoded = decode_unicode_runs(s);
        std::size_t i = 0, run_start = 0, glyphs = 0;
        while (i < decoded.size()) {
            if (decoded[i] == '\n') {
                flush_text(std::string_view(decoded).substr(run_start, i - run_start), glyphs);
                line_break();
                ++i;
                run_start = i;
                glyphs = 0;
                continue;
            }
            utf8::next(decoded, i);
            ++glyphs;
        }
        flush_text(std::string_view(decoded).substr(run_start), glyphs);
    }
    void flush_text(std::string_view s, std::size_t glyphs) {
        if (s.empty()) return;
        if (!out.spans.empty() && out.spans.back().kind == Span::Kind::Text && out.spans.back().style == style) {
            out.spans.back().text += s;
            out.spans.back().glyphs += glyphs;
        } else {
            out.spans.push_back({Span::Kind::Text, std::string(s), {}, style, glyphs});
        }
        out.glyph_count += glyphs;
    }
    void line_break() { out.spans.push_back({Span::Kind::Break, {}, {}, style, 0}); }
    void ruby(std::string_view base, std::string_view reading) {
        const std::size_t g = utf8::count(base);
        out.spans.push_back({Span::Kind::Ruby, std::string(base), std::string(reading), style, g});
        out.glyph_count += g;
    }
    void image(std::string_view name) {
        out.spans.push_back({Span::Kind::Image, std::string(name), {}, style, 1});
        out.glyph_count += 1;
    }
    void push_color(std::optional<Rgba> c) {
        colors.push_back(style.color);
        tag_order.push_back('c');
        style.color = c;
    }
    void push_font(float s) {
        fonts.push_back(style.scale);
        tag_order.push_back('f');
        style.scale = s;
    }
    void pop(char kind) {  // kind 0 = most recent of either
        for (std::size_t k = tag_order.size(); k-- > 0;) {
            if (kind && tag_order[k] != kind) continue;
            if (tag_order[k] == 'c' && !colors.empty()) { style.color = colors.back(); colors.pop_back(); }
            if (tag_order[k] == 'f' && !fonts.empty()) { style.scale = fonts.back(); fonts.pop_back(); }
            tag_order.erase(tag_order.begin() + static_cast<std::ptrdiff_t>(k));
            return;
        }
    }
};

}  // namespace

std::string RichText::plain() const {
    std::string s;
    for (const auto& sp : spans) {
        if (sp.kind == Span::Kind::Break) s += '\n';
        else if (sp.kind != Span::Kind::Image) s += sp.text;
    }
    return s;
}

RichText parse_rich_text(std::string_view src, const TextContext& ctx, int line) {
    Builder b;
    for (std::string_view g : split_groups(src)) {
        if (g.front() != '[' || g.back() != ']' || g.size() < 2) {
            b.text(g);
            continue;
        }
        const std::string_view inner = g.substr(1, g.size() - 2);
        if (!inner.empty() && inner[0] == '#') {  // [#base:ruby] or [#text]
            const std::string_view body = inner.substr(1);
            std::size_t sep = body.find(':');
            std::size_t sep_len = 1;
            const std::size_t fw = body.find("：");
            if (fw != std::string_view::npos && (sep == std::string_view::npos || fw < sep)) { sep = fw; sep_len = 3; }
            if (sep == std::string_view::npos) {
                b.text(body);
            } else {
                const std::string_view reading = trim(body.substr(sep + sep_len));
                if (reading.empty()) b.text(body.substr(0, sep));
                else b.ruby(body.substr(0, sep), reading);
            }
            continue;
        }
        if (!inner.empty() && inner[0] == '&' && inner.find(':') != std::string_view::npos) {  // [&male:female]
            const std::string_view body = inner.substr(1);
            const std::size_t sep = body.find(':');
            b.text(ctx.gender == Gender::Female ? body.substr(sep + 1) : body.substr(0, sep));
            continue;
        }
        const std::vector<std::string> tok = tokenize(inner);
        if (tok.empty()) continue;
        const std::string& name = tok[0];
        auto arg = [&](std::size_t i) -> std::string { return i < tok.size() ? tok[i] : std::string(); };

        if (name.size() == 6 && is_hex(name) && tok.size() == 1) {  // [RRGGBB]
            auto c = parse_color(name);
            if (c && c->r == 255 && c->g == 255 && c->b == 255) c.reset();  // FFFFFF = default ink
            b.push_color(c);
            continue;
        }
        if (name == "-") { b.pop(0); continue; }
        if (name == "line" || (name.size() > 4 && name.compare(0, 4, "line") == 0 && is_hex(name.substr(4)) &&
                               std::isdigit(static_cast<unsigned char>(name[4])))) {
            int n = 1;
            if (tok.size() > 1) n = std::atoi(tok[1].c_str());
            else if (name.size() > 4) n = std::atoi(name.c_str() + 4);
            n = std::max(1, n);
            std::string bar;
            for (int k = 0; k < n; ++k) bar += "━";
            b.text(bar);
            continue;
        }
        if (name == "r" || name == "sr" || name == "csr") { b.line_break(); continue; }
        if (name == "%1") { b.text(ctx.master_name); continue; }
        if (name == "k" || name == "q" || name == "page") continue;
        if (name == "image" || name == "i") {
            if (!arg(1).empty()) b.image(arg(1));
            continue;
        }
        if (name == "f" || name == "font" || name == "fontSize" || name == "s") {
            const auto s = font_scale(arg(1));
            if (s) b.push_font(*s);
            else b.pop('f');
            continue;
        }
        if (name == "align") {
            const std::string a = arg(1);
            b.style.align = a == "center" ? Align::Center : a == "right" ? Align::Right : Align::Left;
            continue;
        }
        if (name == "speed") {
            const auto v = parse_number(arg(1));
            b.style.speed = (v && *v > 0) ? static_cast<float>(*v) : 0.f;
            continue;
        }
        if (name == "servantName") {
            // [servantName id:jp:en] -> the last non-empty part
            std::string rest(trim(inner.substr(name.size())));
            std::vector<std::string> parts;
            std::size_t st = 0;
            while (true) {
                const std::size_t c = rest.find(':', st);
                parts.push_back(rest.substr(st, c == std::string::npos ? std::string::npos : c - st));
                if (c == std::string::npos) break;
                st = c + 1;
            }
            if (parts.size() >= 3 && !parts[2].empty()) b.text(parts[2]);
            else if (parts.size() >= 2) b.text(parts[1]);
            continue;
        }
        // Anything else is a script command written inside the text.
        if (auto cmd = parse_command(g, line)) b.out.commands.push_back({b.out.glyph_count, std::move(*cmd)});
    }
    // Drop trailing breaks so "text[r]" does not leave an empty last line.
    while (!b.out.spans.empty() && b.out.spans.back().kind == Span::Kind::Break) b.out.spans.pop_back();
    return std::move(b.out);
}

std::vector<double> reveal_times(const RichText& text, double cps) {
    std::vector<double> times;
    times.reserve(text.glyph_count);
    if (cps <= 0) {
        times.assign(text.glyph_count, 0.0);
        return times;
    }
    double t = 0;
    for (const auto& sp : text.spans) {
        const double rate = sp.style.speed > 0 ? cps * sp.style.speed / 20.0 : cps;
        for (std::size_t k = 0; k < sp.glyphs; ++k) {
            t += 1.0 / rate;
            times.push_back(t);
        }
    }
    return times;
}

}  // namespace vnr
