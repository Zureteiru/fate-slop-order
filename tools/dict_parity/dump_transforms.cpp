// Writes hoshidicts' copy of Yomitan's Japanese transforms (rules + groups) as JSON, in the order
// its deinflector tries them. Built against hoshidicts by tools/dict_parity/run.sh; the output is
// third_party/switch-vnpop/tools/yomitan_transforms.json.
#define private public  // the rule table is private; the layout is unchanged
#include "hoshidicts/deinflector.hpp"
#undef private

#include <cstdio>
#include <string>

static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (c == '\n') { o += "\\n"; continue; }
        o += c;
    }
    return o;
}

int main() {
    Deinflector d;
    std::printf("{\n  \"max_length\": %zu,\n  \"groups\": [\n", d.max_length_);
    for (size_t i = 0; i < d.groups_.size(); ++i)
        std::printf("    {\"name\": \"%s\", \"description\": \"%s\"}%s\n", esc(d.groups_[i].name).c_str(),
                    esc(d.groups_[i].description).c_str(), i + 1 < d.groups_.size() ? "," : "");
    std::printf("  ],\n  \"rules\": [\n");
    bool first = true;
    for (const auto& [suffix, rules] : d.transforms_) {
        for (const auto& r : rules) {
            std::printf("%s    [\"%s\", \"%s\", %u, %u, %d]", first ? "" : ",\n", esc(r.from).c_str(), esc(r.to).c_str(),
                        r.conditions_in, r.conditions_out, r.group_id);
            first = false;
        }
    }
    std::printf("\n  ]\n}\n");
}
