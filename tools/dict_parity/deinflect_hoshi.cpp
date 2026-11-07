// hoshidicts' deinflections for each input line: "text<TAB>form|conditions|trace ; ...".
// Only the first result for each (form, conditions) is printed: the one a lookup keeps.
// The trace is printed base first, as Hoshi Reader shows it.
#include <iostream>
#include <set>
#include <string>

#include "hoshidicts/deinflector.hpp"

int main() {
    std::ios::sync_with_stdio(false);
    Deinflector d;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::cout << line << '\t';
        std::set<std::pair<std::string, uint32_t>> seen;
        bool first = true;
        for (const auto& r : d.deinflect(line)) {
            if (!seen.insert({r.text, r.conditions}).second) continue;
            std::string trace;
            for (size_t i = r.trace.size(); i-- > 0;) trace += (trace.empty() ? "" : "+") + r.trace[i].name;
            std::cout << (first ? "" : " ; ") << r.text << '|' << r.conditions << '|' << trace;
            first = false;
        }
        std::cout << '\n';
    }
}
