// Windows specifics: WinHTTP downloads on worker threads, %LOCALAPPDATA% cache, system Japanese font.
#include <windows.h>
#include <winhttp.h>

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

#include "desktop_os.h"

namespace vnr::desktop {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

class WinHttp final : public rt::Http {
public:
    explicit WinHttp(int workers) {
        session_ = WinHttpOpen(L"vnreader/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
        for (int i = 0; i < workers; ++i) threads_.emplace_back([this] { work(); });
    }
    ~WinHttp() override {
        {
            std::lock_guard<std::mutex> lock(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : threads_) t.join();
        if (session_) WinHttpCloseHandle(session_);
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
    HINTERNET session_ = nullptr;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    std::vector<rt::HttpResult> done_;
    std::vector<std::thread> threads_;
    rt::RequestId next_ = 1;
    bool stop_ = false;

    void work() {
        while (true) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_) return;
                job = std::move(queue_.front());
                queue_.pop_front();
            }
            rt::HttpResult r = fetch(job.req);
            r.id = job.id;
            std::lock_guard<std::mutex> lock(mu_);
            done_.push_back(std::move(r));
        }
    }

    rt::HttpResult fetch(const rt::HttpRequest& req) {
        rt::HttpResult res;
        if (!session_) return res;
        const std::wstring url = widen(req.url);
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256] = {}, path[2048] = {};
        uc.lpszHostName = host;
        uc.dwHostNameLength = 256;
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = 2048;
        wchar_t extra[2048] = {};
        uc.lpszExtraInfo = extra;
        uc.dwExtraInfoLength = 2048;
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return res;
        const std::wstring object = std::wstring(path) + extra;
        HINTERNET conn = WinHttpConnect(session_, host, uc.nPort, 0);
        if (!conn) return res;
        const DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET h = WinHttpOpenRequest(conn, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (h) {
            std::wstring headers;
            if (!req.range.empty()) headers = L"Range: bytes=" + widen(req.range) + L"\r\n";
            const BOOL sent = WinHttpSendRequest(h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                                 headers.empty() ? 0 : static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA,
                                                 0, 0, 0);
            if (sent && WinHttpReceiveResponse(h, nullptr)) {
                DWORD status = 0, size = sizeof(status);
                WinHttpQueryHeaders(h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                    &status, &size, WINHTTP_NO_HEADER_INDEX);
                res.status = static_cast<int>(status);
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(h, &avail) && avail > 0) {
                    const std::size_t old = res.body.size();
                    res.body.resize(old + avail);
                    DWORD read = 0;
                    if (!WinHttpReadData(h, res.body.data() + old, avail, &read)) break;
                    res.body.resize(old + read);
                }
            }
            WinHttpCloseHandle(h);
        }
        WinHttpCloseHandle(conn);
        return res;
    }
};

}  // namespace

namespace {
// Portable data: a `userdata` folder next to the exe or in one of its parents (the work folder, when
// running from build-desktop/...) holds the user files and the cache instead of %LOCALAPPDATA%.
std::filesystem::path portable_dir() {
    wchar_t exe[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::error_code ec;
    for (std::filesystem::path d = std::filesystem::path(exe).parent_path(); !d.empty(); d = d.parent_path()) {
        if (std::filesystem::is_directory(d / "userdata", ec)) return d / "userdata";
        if (d == d.parent_path()) break;
    }
    return {};
}

std::filesystem::path app_dir() {
    if (const std::filesystem::path p = portable_dir(); !p.empty()) return p;
    char buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", buf, MAX_PATH);
    std::filesystem::path p = (n > 0 && n < MAX_PATH) ? std::filesystem::path(buf) : std::filesystem::temp_directory_path();
    return p / "vnreader";
}
std::string ensure(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
}
}  // namespace

std::string cache_dir() { return ensure(app_dir() / "cache"); }

// <work folder>\userdata when it exists (portable), else %LOCALAPPDATA%\vnreader: the key
// "ui/003.png" is <that folder>\ui\003.png.
std::string user_dir() { return ensure(app_dir()); }

std::vector<std::string> japanese_font_paths() {
    char win[MAX_PATH] = {};
    GetWindowsDirectoryA(win, MAX_PATH);
    const std::string fonts = std::string(win) + "\\Fonts\\";
    return {fonts + "YuGothB.ttc", fonts + "YuGothM.ttc", fonts + "meiryob.ttc", fonts + "meiryo.ttc", fonts + "msgothic.ttc"};
}

std::unique_ptr<rt::Http> make_http(int worker_threads) { return std::make_unique<WinHttp>(worker_threads); }

}  // namespace vnr::desktop
