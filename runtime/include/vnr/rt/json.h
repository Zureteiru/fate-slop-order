// A small JSON reader for Atlas API responses (svtScript, later war/quest data).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vnr::rt {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    static std::optional<Json> parse(std::string_view text);

    Type type() const { return type_; }
    bool is_object() const { return type_ == Type::Object; }
    bool is_array() const { return type_ == Type::Array; }

    // Lookups never throw: a missing key or wrong type yields a Null value.
    const Json& operator[](std::string_view key) const;
    const Json& operator[](std::size_t index) const;
    std::size_t size() const;  // array length or object member count

    double number(double fallback = 0) const { return type_ == Type::Number ? number_ : fallback; }
    int integer(int fallback = 0) const { return type_ == Type::Number ? static_cast<int>(number_) : fallback; }
    bool boolean(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    const std::string& string() const;  // empty when not a string

    const std::vector<Json>& items() const { return array_; }
    const std::vector<std::pair<std::string, Json>>& members() const { return object_; }

private:
    friend class JsonParser;
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::vector<Json> array_;
    std::vector<std::pair<std::string, Json>> object_;
};

}  // namespace vnr::rt
