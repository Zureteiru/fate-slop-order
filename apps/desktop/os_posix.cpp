// Linux specifics (PortMaster handhelds and desktop Linux): libcurl downloads on worker threads, folders,
// a Japanese font. libcurl is loaded at run time (dlopen), so the binary has no link dependency on it:
// handhelds without it (or without Wi-Fi) still run, offline, from a copied cache.
#include <dlfcn.h>
#include <unistd.h>

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

#include "desktop_os.h"

namespace vnr::desktop {

namespace {

// libcurl without its headers: the handful of option numbers used here are part of libcurl's stable ABI
// (curl.h: CURLOPTTYPE_LONG 0, OBJECTPOINT 10000, FUNCTIONPOINT 20000; CURLINFO_LONG 0x200000).
using CURL = void;
using CURLcode = int;
using CURLoption = int;
using CURLINFO = int;
constexpr CURLcode CURLE_OK = 0;
constexpr long CURL_GLOBAL_DEFAULT = 3;
constexpr CURLoption CURLOPT_WRITEDATA = 10001, CURLOPT_URL = 10002, CURLOPT_RANGE = 10007, CURLOPT_USERAGENT = 10018,
                     CURLOPT_LOW_SPEED_LIMIT = 19, CURLOPT_LOW_SPEED_TIME = 20, CURLOPT_FOLLOWLOCATION = 52,
                     CURLOPT_CONNECTTIMEOUT = 78, CURLOPT_NOSIGNAL = 99, CURLOPT_ACCEPT_ENCODING = 10102,
                     CURLOPT_WRITEFUNCTION = 20011;
constexpr CURLINFO CURLINFO_RESPONSE_CODE = 0x200000 + 2;

// The few libcurl entry points used, resolved from libcurl.so.4.
struct CurlApi {
    CURLcode (*global_init)(long) = nullptr;
    CURL* (*easy_init)() = nullptr;
    CURLcode (*easy_setopt)(CURL*, CURLoption, ...) = nullptr;
    CURLcode (*easy_perform)(CURL*) = nullptr;
    CURLcode (*easy_getinfo)(CURL*, CURLINFO, ...) = nullptr;
    void (*easy_reset)(CURL*) = nullptr;
    void (*easy_cleanup)(CURL*) = nullptr;

    bool load() {
        void* lib = nullptr;
        for (const char* name : {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"})
            if ((lib = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
        if (!lib) return false;
        global_init = reinterpret_cast<decltype(global_init)>(dlsym(lib, "curl_global_init"));
        easy_init = reinterpret_cast<decltype(easy_init)>(dlsym(lib, "curl_easy_init"));
        easy_setopt = reinterpret_cast<decltype(easy_setopt)>(dlsym(lib, "curl_easy_setopt"));
        easy_perform = reinterpret_cast<decltype(easy_perform)>(dlsym(lib, "curl_easy_perform"));
        easy_getinfo = reinterpret_cast<decltype(easy_getinfo)>(dlsym(lib, "curl_easy_getinfo"));
        easy_reset = reinterpret_cast<decltype(easy_reset)>(dlsym(lib, "curl_easy_reset"));
        easy_cleanup = reinterpret_cast<decltype(easy_cleanup)>(dlsym(lib, "curl_easy_cleanup"));
        return global_init && easy_init && easy_setopt && easy_perform && easy_getinfo && easy_reset && easy_cleanup &&
               global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    }
};

class CurlHttp final : public rt::Http {
public:
    explicit CurlHttp(int workers) {
        ok_ = api_.load();
        if (!ok_) std::fprintf(stderr, "[vnreader] libcurl not found: offline, only cached files can be read\n");
        for (int i = 0; i < workers; ++i) threads_.emplace_back([this] { work(); });
    }
    ~CurlHttp() override {
        {
            std::lock_guard<std::mutex> lock(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : threads_) t.join();
    }

    rt::RequestId get(const rt::HttpRequest& req) override {
        std::lock_guard<std::mutex> lock(mu_);
        const rt::RequestId id = next_++;
        queue_.push_back({id, req});
        cv_.notify_one();
        return id;
    }

    std::vector<rt::HttpResult> poll() override {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<rt::HttpResult> out;
        out.swap(done_);
        return out;
    }

private:
    struct Job {
        rt::RequestId id;
        rt::HttpRequest req;
    };
    CurlApi api_;
    bool ok_ = false;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    std::vector<rt::HttpResult> done_;
    std::vector<std::thread> threads_;
    rt::RequestId next_ = 1;
    bool stop_ = false;

    static size_t on_body(char* data, size_t size, size_t n, void* user) {
        auto* body = static_cast<std::vector<std::uint8_t>*>(user);
        body->insert(body->end(), reinterpret_cast<std::uint8_t*>(data), reinterpret_cast<std::uint8_t*>(data) + size * n);
        return size * n;
    }

    void work() {
        CURL* curl = ok_ ? api_.easy_init() : nullptr;  // one handle per worker: connections stay alive
        while (true) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_) break;
                job = std::move(queue_.front());
                queue_.pop_front();
            }
            rt::HttpResult r;
            r.id = job.id;
            if (curl) {
                api_.easy_reset(curl);
                api_.easy_setopt(curl, CURLOPT_URL, job.req.url.c_str());
                api_.easy_setopt(curl, CURLOPT_USERAGENT, "vnreader/0.1 (Linux)");
                api_.easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                api_.easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
                api_.easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
                api_.easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
                api_.easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
                api_.easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
                api_.easy_setopt(curl, CURLOPT_WRITEFUNCTION, &CurlHttp::on_body);
                api_.easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
                if (!job.req.range.empty()) api_.easy_setopt(curl, CURLOPT_RANGE, job.req.range.c_str());
                if (api_.easy_perform(curl) == CURLE_OK) {
                    long code = 0;
                    api_.easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
                    r.status = static_cast<int>(code);
                } else {
                    r.body.clear();  // status 0 = network error
                }
            }
            std::lock_guard<std::mutex> lock(mu_);
            done_.push_back(std::move(r));
        }
        if (curl) api_.easy_cleanup(curl);
    }
};

std::filesystem::path exe_dir() {
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : exe.parent_path();
}

// Portable data, as on Windows: a `userdata` folder next to the binary or in one of its parents. A
// PortMaster port keeps it in its own folder (ports/vnreader/userdata).
std::filesystem::path portable_dir() {
    std::error_code ec;
    for (std::filesystem::path d = exe_dir(); !d.empty(); d = d.parent_path()) {
        if (std::filesystem::is_directory(d / "userdata", ec)) return d / "userdata";
        if (d == d.parent_path()) break;
    }
    return {};
}

std::filesystem::path env_dir(const char* var, const char* home_rel) {
    if (const char* v = std::getenv(var); v && *v) return v;
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home && *home ? home : "/tmp") / home_rel;
}

std::string ensure(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
}

}  // namespace

std::string cache_dir() {
    if (const auto p = portable_dir(); !p.empty()) return ensure(p / "cache");
    return ensure(env_dir("XDG_CACHE_HOME", ".cache") / "vnreader");
}

std::string user_dir() {
    if (const auto p = portable_dir(); !p.empty()) return ensure(p);
    return ensure(env_dir("XDG_DATA_HOME", ".local/share") / "vnreader");
}

std::vector<std::string> japanese_font_paths() {
    // Handheld firmwares ship no CJK font, so a font in the user folder comes first; the game font
    // (fonts/FOT-SkipStd-B.otf) is the last resort, so the popup still has Japanese glyphs.
    const std::string user = user_dir() + "/fonts/";
    return {user + "system.ttf",
            user + "system.otf",
            user + "system.ttc",
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
            "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf",
            user + "FOT-SkipStd-B.otf"};
}

std::unique_ptr<rt::Http> make_http(int worker_threads) { return std::make_unique<CurlHttp>(worker_threads); }

}  // namespace vnr::desktop
