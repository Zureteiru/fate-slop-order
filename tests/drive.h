// Helpers for driving a Player deterministically in tests.
#pragma once

#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "vnr/player.h"

namespace drive {

inline std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

constexpr double kDt = 1.0 / 240.0;

// Plays forward: taps through dialogue instantly, picks choices from `picks` (then option 0),
// and advances time in kDt steps while waiting. Stops when `done()` is true (checked before
// every action) or the script ends / max_seconds of waiting pass. Returns done().
inline bool until(vnr::Player& p, const std::function<bool()>& done, std::vector<std::size_t> picks = {},
                  double max_seconds = 600) {
    std::size_t pick = 0;
    double waited = 0;
    for (int guard = 0; guard < 10000000; ++guard) {
        if (done()) return true;
        switch (p.status()) {
            case vnr::Status::AwaitChoice:
                p.choose(pick < picks.size() ? picks[pick] : 0);
                ++pick;
                break;
            case vnr::Status::Typing:
            case vnr::Status::AwaitAdvance:
                p.advance();
                break;
            case vnr::Status::Waiting:
            case vnr::Status::Running:
                p.update(kDt);
                waited += kDt;
                if (waited > max_seconds) return done();
                break;
            case vnr::Status::Ended:
                return done();
        }
    }
    return done();
}

// Seconds of update() needed until `done()` (no tapping). -1 if it never happens within limit.
inline double time_until(vnr::Player& p, const std::function<bool()>& done, double limit = 30) {
    double t = 0;
    while (t < limit) {
        if (done()) return t;
        p.update(kDt);
        t += kDt;
    }
    return done() ? t : -1;
}

inline bool at_say(const vnr::Player& p, int line) {
    const auto v = p.text();
    return (p.status() == vnr::Status::Typing || p.status() == vnr::Status::AwaitAdvance) && v.say &&
           v.say->line == line;
}

}  // namespace drive
