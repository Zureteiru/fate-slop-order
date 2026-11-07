// In-memory platform for headless runtime tests: HTTP answered by a handler, files in a map.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "drive.h"
#include "vnr/rt/platform.h"

namespace fakes {

struct FakeHttp : vnr::rt::Http {
    std::function<vnr::rt::HttpResult(const vnr::rt::HttpRequest&)> handler;
    std::vector<vnr::rt::HttpResult> done;
    std::vector<vnr::rt::HttpRequest> requests;
    vnr::rt::RequestId next = 1;

    vnr::rt::RequestId get(const vnr::rt::HttpRequest& req) override {
        requests.push_back(req);
        vnr::rt::HttpResult r = handler ? handler(req) : vnr::rt::HttpResult{0, 404, {}};
        r.id = next++;
        done.push_back(std::move(r));
        return done.back().id;
    }
    std::vector<vnr::rt::HttpResult> poll() override {
        std::vector<vnr::rt::HttpResult> out;
        out.swap(done);
        return out;
    }
};

struct MemFiles : vnr::rt::Files {
    std::map<std::string, std::vector<std::uint8_t>> data;
    bool read(const std::string& key, std::vector<std::uint8_t>& out) override {
        const auto it = data.find(key);
        if (it == data.end()) return false;
        out = it->second;
        return true;
    }
    bool write(const std::string& key, const std::vector<std::uint8_t>& bytes) override {
        data[key] = bytes;
        return true;
    }
};

struct MemLog : vnr::rt::Log {
    std::vector<std::string> lines;
    void write(std::string_view m) override { lines.emplace_back(m); }
};

inline std::vector<std::uint8_t> bytes_of(const std::string& path) {
    const std::string s = drive::read_file(path);
    return {s.begin(), s.end()};
}

// Serves the pinned fixtures in .cache/scripts for the Atlas URLs they came from; 404 otherwise.
inline vnr::rt::HttpResult serve_fixture(const std::string& dir, const vnr::rt::HttpRequest& req) {
    struct Route { const char* suffix; const char* range; const char* file; };
    static const Route kRoutes[] = {
        {"/NA/Script/01/0100000010.txt", "", "NA_0100000010.txt"},
        {"/NA/Script/01/0100000011.txt", "", "NA_0100000011.txt"},
        {"/NA/Script/01/0100081810.txt", "", "NA_0100081810.txt"},
        {"/JP/Script/01/0100000011.txt", "", "JP_0100000011.txt"},
        {"/NA/CharaFigure/98001000/98001000_merged.png", "", "98001000_merged.png"},
        {"/NA/CharaFigure/98004000/98004000_merged.png", "", "98004000_merged.png"},
        {"/NA/CharaFigure/98001000/98001000.png", "0-31", "body_98001000.bin"},
        {"/NA/CharaFigure/98004000/98004000.png", "0-31", "body_98004000.bin"},
        {"/NA/Back/back10310.png", "", "back10310.png"},
        {"/NA/Back/back10200.png", "", "back10200.png"},
    };
    auto ends_with = [](const std::string& s, const char* suf) {
        const std::string t(suf);
        return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
    };
    if (req.url.find("/raw/NA/svtScript?") != std::string::npos)
        return {0, 200, bytes_of(dir + "/svt_98001000_98004000.json")};
    for (const Route& r : kRoutes)
        if (ends_with(req.url, r.suffix) && req.range == r.range)
            return {0, req.range.empty() ? 200 : 206, bytes_of(dir + "/" + r.file)};
    return {0, 404, {}};
}

}  // namespace fakes
