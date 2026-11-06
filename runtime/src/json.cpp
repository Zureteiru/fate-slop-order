#include "vnr/rt/json.h"

#include <cctype>
#include <cstdlib>

#include "vnr/script_lines.h"

namespace vnr::rt {

namespace {
const Json& null_json() {
    static const Json n;
    return n;
}
}  // namespace

const Json& Json::operator[](std::string_view key) const {
    if (type_ == Type::Object)
        for (const auto& [k, v] : object_)
            if (k == key) return v;
    return null_json();
}

const Json& Json::operator[](std::size_t index) const {
    return (type_ == Type::Array && index < array_.size()) ? array_[index] : null_json();
}

std::size_t Json::size() const {
    return type_ == Type::Array ? array_.size() : type_ == Type::Object ? object_.size() : 0;
}

const std::string& Json::string() const {
    static const std::string empty;
    return type_ == Type::String ? string_ : empty;
}

class JsonParser {
public:
    explicit JsonParser(std::string_view s) : s_(s) {}

    std::optional<Json> document() {
        Json v;
        if (!value(v, 0)) return std::nullopt;
        ws();
        if (i_ != s_.size()) return std::nullopt;
        return v;
    }

private:
    std::string_view s_;
    std::size_t i_ = 0;
    static constexpr int kMaxDepth = 64;

    void ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }
    bool lit(std::string_view w) {
        if (s_.substr(i_, w.size()) != w) return false;
        i_ += w.size();
        return true;
    }

    bool value(Json& out, int depth) {
        if (depth > kMaxDepth) return false;
        ws();
        if (i_ >= s_.size()) return false;
        const char c = s_[i_];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out.type_ = Json::Type::String;
            return str(out.string_);
        }
        if (lit("true")) { out.type_ = Json::Type::Bool; out.bool_ = true; return true; }
        if (lit("false")) { out.type_ = Json::Type::Bool; out.bool_ = false; return true; }
        if (lit("null")) { out.type_ = Json::Type::Null; return true; }
        return number(out);
    }

    bool number(Json& out) {
        const std::size_t start = i_;
        if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
        while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '.' || s_[i_] == 'e' ||
                                  s_[i_] == 'E' || s_[i_] == '-' || s_[i_] == '+'))
            ++i_;
        if (i_ == start) return false;
        const std::string tok(s_.substr(start, i_ - start));
        char* end = nullptr;
        out.number_ = std::strtod(tok.c_str(), &end);
        if (end == tok.c_str() || *end != '\0') return false;
        out.type_ = Json::Type::Number;
        return true;
    }

    static int hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool hex4(unsigned& v) {
        if (i_ + 4 > s_.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const int h = hex(s_[i_ + static_cast<std::size_t>(k)]);
            if (h < 0) return false;
            v = v * 16 + static_cast<unsigned>(h);
        }
        i_ += 4;
        return true;
    }

    bool str(std::string& out) {
        ++i_;  // opening quote
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i_ >= s_.size()) return false;
            const char e = s_[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && s_.substr(i_, 2) == "\\u") {
                        i_ += 2;
                        unsigned lo = 0;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8::append(out, static_cast<char32_t>(cp));
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    bool array(Json& out, int depth) {
        out.type_ = Json::Type::Array;
        ++i_;
        ws();
        if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
        while (true) {
            Json v;
            if (!value(v, depth + 1)) return false;
            out.array_.push_back(std::move(v));
            ws();
            if (i_ >= s_.size()) return false;
            if (s_[i_] == ',') { ++i_; continue; }
            if (s_[i_] == ']') { ++i_; return true; }
            return false;
        }
    }

    bool object(Json& out, int depth) {
        out.type_ = Json::Type::Object;
        ++i_;
        ws();
        if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
        while (true) {
            ws();
            if (i_ >= s_.size() || s_[i_] != '"') return false;
            std::string key;
            if (!str(key)) return false;
            ws();
            if (i_ >= s_.size() || s_[i_] != ':') return false;
            ++i_;
            Json v;
            if (!value(v, depth + 1)) return false;
            out.object_.emplace_back(std::move(key), std::move(v));
            ws();
            if (i_ >= s_.size()) return false;
            if (s_[i_] == ',') { ++i_; continue; }
            if (s_[i_] == '}') { ++i_; return true; }
            return false;
        }
    }
};

std::optional<Json> Json::parse(std::string_view text) { return JsonParser(text).document(); }

}  // namespace vnr::rt
