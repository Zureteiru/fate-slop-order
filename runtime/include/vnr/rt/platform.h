// The contract between the shared runtime and each OS wrapper (Windows, macOS, Android, Switch).
// The runtime never includes OS or SDL headers; everything OS-specific comes through here.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vnr::rt {

using RequestId = std::uint64_t;

struct HttpRequest {
    std::string url;
    std::string range;  // "0-31" for a byte range, empty for the whole body
};

struct HttpResult {
    RequestId id = 0;
    int status = 0;  // HTTP status (200/206…); 0 = network error
    std::vector<std::uint8_t> body;
};

// Asynchronous HTTP. The wrapper owns any worker threads; the runtime only calls get() and
// poll() from its own (main) thread.
class Http {
public:
    virtual ~Http() = default;
    virtual RequestId get(const HttpRequest& request) = 0;
    virtual std::vector<HttpResult> poll() = 0;  // completed requests since the last poll
};

// Persistent storage for the download cache and settings. Keys are relative paths with '/'
// separators (e.g. "static.atlasacademy.io/NA/Back/back10310.png"); the wrapper maps them
// into its own cache directory (%LOCALAPPDATA%, ~/Library/Caches, app files dir, sdmc:/…).
class Files {
public:
    virtual ~Files() = default;
    virtual bool read(const std::string& key, std::vector<std::uint8_t>& out) = 0;
    virtual bool write(const std::string& key, const std::vector<std::uint8_t>& data) = 0;
};

class Log {
public:
    virtual ~Log() = default;
    virtual void write(std::string_view message) = 0;
};

struct Platform {
    Http& http;
    Files& files;
    Log& log;
    std::vector<std::uint8_t> font;  // a Japanese-capable TTF/TTC from the OS
    int font_index = 0;              // face index inside a .ttc
};

}  // namespace vnr::rt
