// Switch wrapper: fullscreen SDL window + SDL backend + Switch services around the shared runtime.
// Opens the chapter picker; Start (+) on the picker home quits back to the Homebrew Menu.
// All reading logic lives in runtime/ and core/.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include "sdl_backend.h"
#include "switch_os.h"
#include "vnr/rt/app.h"

using namespace vnr;

namespace {

struct DiskFiles final : rt::Files {
    std::filesystem::path root;
    explicit DiskFiles(std::string dir) : root(std::move(dir)) {}
    bool read(const std::string& key, std::vector<std::uint8_t>& out) override {
        std::ifstream f(root / key, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }
    bool write(const std::string& key, const std::vector<std::uint8_t>& data) override {
        const auto p = root / key;
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream f(p, std::ios::binary);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return static_cast<bool>(f);
    }
};

// stdout (nxlink -s shows it) and <user folder>/vnreader.log, so problems can be read off the SD card.
struct SwitchLog final : rt::Log {
    std::FILE* file = nullptr;
    explicit SwitchLog(const std::string& dir) { file = std::fopen((dir + "/vnreader.log").c_str(), "w"); }
    ~SwitchLog() override {
        if (file) std::fclose(file);
    }
    void write(std::string_view m) override {
        std::printf("[vnreader] %.*s\n", static_cast<int>(m.size()), m.data());
        if (file) {
            std::fprintf(file, "%.*s\n", static_cast<int>(m.size()), m.data());
            std::fflush(file);
        }
    }
};

}  // namespace

int main(int, char**) {
    nx::init_services();
    const std::string user = nx::user_dir();
    {
        SwitchLog log(user);

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
            log.write(std::string("SDL_Init: ") + SDL_GetError());
            nx::exit_services();
            return 1;
        }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        SDL_Window* window = SDL_CreateWindow("vnreader", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_FULLSCREEN);
        SDL_Renderer* sdl_renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
        if (!sdl_renderer) {
            log.write(std::string("SDL window/renderer: ") + SDL_GetError());
            SDL_Quit();
            nx::exit_services();
            return 1;
        }
        sdl::open_controllers();

        const auto http = nx::make_http();
        DiskFiles files(nx::cache_dir());
        DiskFiles user_files(user);
        rt::Platform platform{*http, files, log, nx::japanese_font(), 0, &user_files};
        if (platform.font.empty()) log.write("no shared font from pl; text will not be drawn");

        rt::AppConfig cfg;
        cfg.picker = true;
        {
            rt::App app(platform, cfg);
            sdl::Renderer renderer(sdl_renderer);
            // Declared after the App, so it closes first (before the App and its mixer go away).
            std::unique_ptr<sdl::AudioDevice> audio;
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
                audio = std::make_unique<sdl::AudioDevice>(app.audio());
                if (audio->ok()) log.write("audio: " + std::to_string(audio->rate()) + " Hz");
                else log.write(std::string("audio: no device (") + SDL_GetError() + "); running silent");
            }
            const bool silent = !audio || !audio->ok();
            std::vector<float> null_sink;
            double null_frames = 0;
            Uint64 last = SDL_GetPerformanceCounter();
            bool running = true;
            while (running) {
                std::vector<rt::InputEvent> input;
                SDL_Event e;
                while (SDL_PollEvent(&e))
                    if (!sdl::translate_event(e, renderer, input)) running = false;
                if (app.quit_requested()) running = false;

                const Uint64 now = SDL_GetPerformanceCounter();
                double dt = static_cast<double>(now - last) / static_cast<double>(SDL_GetPerformanceFrequency());
                last = now;
                if (dt > 0.1) dt = 0.1;  // back from the HOME menu / sleep: don't jump the script ahead
                sdl::poll_held(dt, input);

                app.update(dt, input);
                if (silent) {  // no device: drain the mixer at real time so clips still end
                    null_frames += dt * 48000;
                    const auto n = static_cast<std::size_t>(null_frames);
                    null_frames -= static_cast<double>(n);
                    null_sink.resize(n * 2);
                    if (n) app.audio().render(null_sink.data(), n);
                }
                renderer.sync(app.textures());
                renderer.draw(app.draw_list());
                SDL_RenderPresent(sdl_renderer);
            }
        }
        SDL_DestroyRenderer(sdl_renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
    nx::exit_services();
    return 0;
}
