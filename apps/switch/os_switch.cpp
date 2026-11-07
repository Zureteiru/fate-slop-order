// Switch specifics: libcurl (devkitPro's build, TLS through the system ssl service, so the console's
// own CA store is used) on worker threads, folders on the SD card, the Standard shared font from pl.
#include <switch.h>

#include <curl/curl.h>

#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

#include "switch_os.h"

namespace vnr::nx {

namespace {

bool g_pl = false;

class CurlHttp final : public rt::Http {
public:
    explicit CurlHttp(int workers) {
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
        // One easy handle per worker, so connections (and their TLS sessions) are kept alive.
        CURL* curl = curl_easy_init();
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
                curl_easy_reset(curl);
                curl_easy_setopt(curl, CURLOPT_URL, job.req.url.c_str());
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "vnreader/0.1 (Switch)");
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
                curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
                curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
                curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
                curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &CurlHttp::on_body);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
                if (!job.req.range.empty()) curl_easy_setopt(curl, CURLOPT_RANGE, job.req.range.c_str());
                if (curl_easy_perform(curl) == CURLE_OK) {
                    long code = 0;
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
                    r.status = static_cast<int>(code);
                } else {
                    r.body.clear();  // status 0 = network error
                }
            }
            std::lock_guard<std::mutex> lock(mu_);
            done_.push_back(std::move(r));
        }
        if (curl) curl_easy_cleanup(curl);
    }
};

std::string ensure(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
}

}  // namespace

void init_services() {
    socketInitializeDefault();
    sslInitialize(3);  // refcounted; curl's libnx TLS backend uses it too
    curl_global_init(CURL_GLOBAL_DEFAULT);
    g_pl = R_SUCCEEDED(plInitialize(PlServiceType_User));
}

void exit_services() {
    if (g_pl) plExit();
    curl_global_cleanup();
    sslExit();
    socketExit();
}

std::string user_dir() { return ensure("sdmc:/switch/vnreader"); }
std::string cache_dir() { return ensure("sdmc:/switch/vnreader/cache"); }

std::vector<std::uint8_t> japanese_font() {
    if (!g_pl) return {};
    PlFontData font{};
    if (R_FAILED(plGetSharedFontByType(&font, PlSharedFontType_Standard)) || !font.address || !font.size) return {};
    const auto* p = static_cast<const std::uint8_t*>(font.address);
    return std::vector<std::uint8_t>(p, p + font.size);
}

std::unique_ptr<rt::Http> make_http(int worker_threads) { return std::make_unique<CurlHttp>(worker_threads); }

}  // namespace vnr::nx
