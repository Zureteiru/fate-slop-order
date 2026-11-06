// Desktop wrapper (Windows now, macOS/Linux later): window + SDL backend + OS services around the
// shared runtime. All reading logic lives in runtime/ and core/.
//
//   vnreader [--region NA|JP] [--scripts 0100000010,0100000011]
//            [--smoke N --screenshot out.bmp]   (hidden window: tap N lines, save a frame, exit)
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <SDL.h>

#include "desktop_os.h"
#include "sdl_backend.h"
#include "vnr/rt/app.h"

using namespace vnr;

namespace {

struct DiskFiles final : rt::Files {
    std::filesystem::path root;
    explicit DiskFiles(std::string dir) : root(std::move(dir)) {}
    bool read(const std::string& key, std::vector<std::uint8_t>& out) override {
        std::ifstream f(root / std::filesystem::u8path(key), std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }
    bool write(const std::string& key, const std::vector<std::uint8_t>& data) override {
        const auto p = root / std::filesystem::u8path(key);
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream f(p, std::ios::binary);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return static_cast<bool>(f);
    }
};

struct StderrLog final : rt::Log {
    void write(std::string_view m) override { std::fprintf(stderr, "[vnreader] %.*s\n", static_cast<int>(m.size()), m.data()); }
};

std::vector<std::uint8_t> load_font(int& index) {
    index = 0;
    for (const std::string& path : desktop::japanese_font_paths()) {
        std::ifstream f(path, std::ios::binary);
        if (!f) continue;
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (!bytes.empty()) return bytes;
    }
    return {};
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, sep))
        if (!item.empty()) out.push_back(item);
    return out;
}

bool save_screenshot(SDL_Renderer* r, const char* path) {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(r, &w, &h);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return false;
    const bool ok = SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0 &&
                    SDL_SaveBMP(s, path) == 0;
    SDL_FreeSurface(s);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    rt::AppConfig cfg;
    cfg.script_ids = {"0100000010", "0100000011"};  // Fuyuki part 1
    int smoke = -1;
    const char* screenshot = nullptr;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--region" && i + 1 < argc) cfg.region = std::strcmp(argv[++i], "JP") == 0 ? Region::JP : Region::NA;
        else if (a == "--scripts" && i + 1 < argc) cfg.script_ids = split(argv[++i], ',');
        else if (a == "--smoke" && i + 1 < argc) smoke = std::atoi(argv[++i]);
        else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    const Uint32 win_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (smoke >= 0 ? SDL_WINDOW_HIDDEN : 0);
    SDL_Window* window = SDL_CreateWindow("vnreader", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1344, 626, win_flags);
    const Uint32 r_flags = smoke >= 0 ? SDL_RENDERER_SOFTWARE : (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Renderer* sdl_renderer = window ? SDL_CreateRenderer(window, -1, r_flags) : nullptr;
    if (!sdl_renderer) {
        std::fprintf(stderr, "SDL window/renderer: %s\n", SDL_GetError());
        return 1;
    }
    sdl::open_controllers();

    const auto http = desktop::make_http();
    DiskFiles files(desktop::cache_dir());
    StderrLog log;
    int font_index = 0;
    DiskFiles user_files(desktop::user_dir());
    rt::Platform platform{*http, files, log, load_font(font_index), font_index, &user_files};
    if (platform.font.empty()) log.write("no Japanese system font found; text will not be drawn");

    {
        rt::App app(platform, cfg);
        sdl::Renderer renderer(sdl_renderer);
        Uint64 last = SDL_GetPerformanceCounter();
        int taps = 0;
        double smoke_clock = 0;
        bool running = true;
        while (running) {
            std::vector<rt::InputEvent> input;
            SDL_Event e;
            while (SDL_PollEvent(&e))
                if (!sdl::translate_event(e, renderer, input)) running = false;

            const Uint64 now = SDL_GetPerformanceCounter();
            double dt = static_cast<double>(now - last) / static_cast<double>(SDL_GetPerformanceFrequency());
            last = now;
            if (dt > 0.1) dt = 0.1;  // window drags / breakpoints: don't jump the script ahead

            if (smoke >= 0 && app.phase() == rt::App::Phase::Playing && app.player()) {
                // Smoke test: tap through `smoke` dialogue lines (first choice), then capture one frame.
                const Player& p = *app.player();
                smoke_clock += dt;
                if (taps < smoke && smoke_clock > 0.05) {
                    smoke_clock = 0;
                    if (p.pending_choice()) input.push_back({rt::InputEvent::Kind::Choose, {}, 0});
                    else if (p.status() == Status::Typing || p.status() == Status::AwaitAdvance) {
                        input.push_back({rt::InputEvent::Kind::Advance, {}, 0});
                        if (p.status() == Status::AwaitAdvance) ++taps;
                    }
                } else if (taps >= smoke && (p.status() == Status::AwaitAdvance || p.pending_choice())) {
                    running = false;
                }
            }
            if (smoke >= 0 && app.phase() == rt::App::Phase::Finished) running = false;

            app.update(dt, input);
            renderer.sync(app.textures());
            renderer.draw(app.draw_list());
            if (!running && smoke >= 0) {
                std::fprintf(stderr, "[vnreader] smoke: %s\n", app.status().c_str());
                if (screenshot && !save_screenshot(sdl_renderer, screenshot)) std::fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
            }
            SDL_RenderPresent(sdl_renderer);
            if (smoke >= 0) SDL_Delay(1);
        }
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
