// Desktop wrapper (Windows now, macOS/Linux later): window + SDL backend + OS services around the
// shared runtime. All reading logic lives in runtime/ and core/.
//
//   vnreader                    (the chapter picker)
//   vnreader [--region NA|JP] [--scripts 0100000010,0100000011]   (one part, no picker)
//            [--picker home|chapter=<id>|end=<id>:<section> --screenshot out.bmp]
//                               (hidden window: open that picker screen, wait for its images, save a frame, exit)
//            [--smoke N --screenshot out.bmp]   (hidden window: tap N lines, save a frame, exit)
//            [--until "text"]   (smoke: keep tapping until a line containing "text" is waiting)
//            [--wipe 0.5]       (smoke: stop mid-wipe once the cover reaches this value)
//            [--chara-fx 0.5]   (smoke: stop mid charaSpecialEffect at this progress)
//            [--log] [--auto]   (smoke: open the LOG screen / turn AUTO on before the capture)
//            [--no-audio]       (no sound device; the mixer still runs, so voice waits keep their timing)
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <SDL.h>

#include "desktop_os.h"
#include "sdl_backend.h"
#include "vnr/rt/app.h"
#include "vnr/rt/picker.h"

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
    std::string until;
    float wipe_at = -1, fx_at = -1;
    bool matched = false;
    bool open_log = false, auto_on = false, no_audio = false, scripts_given = false;
    std::string picker_shot;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--region" && i + 1 < argc) cfg.region = std::strcmp(argv[++i], "JP") == 0 ? Region::JP : Region::NA;
        else if (a == "--scripts" && i + 1 < argc) { cfg.script_ids = split(argv[++i], ','); scripts_given = true; }
        else if (a == "--picker" && i + 1 < argc) picker_shot = argv[++i];
        else if (a == "--smoke" && i + 1 < argc) smoke = std::atoi(argv[++i]);
        else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        else if (a == "--until" && i + 1 < argc) until = argv[++i];
        else if (a == "--wipe" && i + 1 < argc) wipe_at = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--chara-fx" && i + 1 < argc) fx_at = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--log") open_log = true;
        else if (a == "--auto") auto_on = true;
        else if (a == "--no-audio") no_audio = true;
    }

    // The picker unless a part was given on the command line (--scripts) or a story smoke run asked for
    // the default part (--smoke without --scripts plays Fuyuki part 1, as before the picker existed).
    cfg.picker = !picker_shot.empty() || (!scripts_given && smoke < 0);
    cfg.picker_screen = picker_shot;
    const bool hidden = smoke >= 0 || !picker_shot.empty();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    const Uint32 win_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (hidden ? SDL_WINDOW_HIDDEN : 0);
    SDL_Window* window = SDL_CreateWindow("vnreader", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 576, win_flags);  // 16:9, as the game
    const Uint32 r_flags = hidden ? SDL_RENDERER_SOFTWARE : (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
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
        // Declared after the App, so it closes first (before the App and its mixer go away).
        std::unique_ptr<sdl::AudioDevice> audio;
        if (!no_audio && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
            audio = std::make_unique<sdl::AudioDevice>(app.audio());
            if (audio->ok()) log.write("audio: " + std::string(SDL_GetCurrentAudioDriver()) + " at " + std::to_string(audio->rate()) + " Hz");
            else log.write(std::string("audio: no device (") + SDL_GetError() + "); running silent");
        }
        const bool silent = !audio || !audio->ok();
        std::vector<float> null_sink;
        double null_frames = 0;
        Uint64 last = SDL_GetPerformanceCounter();
        int taps = 0;
        double smoke_clock = 0;
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
            if (dt > 0.1) dt = 0.1;  // window drags / breakpoints: don't jump the script ahead
            sdl::poll_held(dt, input);

            if (smoke >= 0 && app.phase() == rt::App::Phase::Playing && app.player()) {
                // Smoke test: tap through `smoke` dialogue lines (first choice), then capture one frame.
                const Player& p = *app.player();
                smoke_clock += dt;
                // With --until, taps only start counting once that line is showing: --smoke 0 stops on
                // it, --smoke 1 one tap later (e.g. on the choice that follows it).
                if (!until.empty() && !matched) {
                    const TextView tv = p.text();
                    matched = tv.say && tv.say->text.plain().find(until) != std::string::npos;
                    if (!matched) taps = -1000000;
                    else taps = 0;
                }
                const Anim& cover = p.stage().wipe.cover;
                bool fx_now = false;
                if (fx_at >= 0 && (until.empty() || matched))
                    for (const auto& [code, el] : p.stage().elements)
                        fx_now = fx_now || (el.special && std::fabs(el.special->progress.value() - fx_at) < 0.03f);
                if (wipe_at >= 0 && (until.empty() || matched) && cover.active() && std::fabs(cover.value() - wipe_at) < 0.05f) {
                    running = false;  // --wipe: capture this frame, mid-wipe
                } else if (fx_now) {
                    running = false;  // --chara-fx: capture this frame, mid-effect
                } else if (taps < smoke && smoke_clock > 0.05) {
                    smoke_clock = 0;
                    if (p.pending_choice()) input.push_back({rt::InputEvent::Kind::Choose, {}, 0});
                    else if (p.status() == Status::Typing || p.status() == Status::AwaitAdvance) {
                        input.push_back({rt::InputEvent::Kind::Advance, {}, 0});
                        if (p.status() == Status::AwaitAdvance) ++taps;
                    }
                } else if (taps >= smoke && (p.status() == Status::AwaitAdvance || p.pending_choice())) {
                    running = false;
                    if (auto_on) input.push_back({rt::InputEvent::Kind::ToggleAuto, {}, 0});
                    if (open_log) input.push_back({rt::InputEvent::Kind::OpenLog, {}, 0});
                }
            }
            if (smoke >= 0 && app.phase() == rt::App::Phase::Finished) running = false;
            if (!picker_shot.empty()) {
                // Picker capture: once the screen's catalogue and banners are in (or after 30 s).
                smoke_clock += dt;
                const rt::Picker* pk = app.picker();
                if ((pk && pk->ready() && ++taps > 10) || smoke_clock > 30) running = false;
            }

            app.update(dt, input);
            if (silent) {  // no device: drain the mixer at real time so clips still end
                null_frames += dt * 48000;  // the director's default rate
                const auto n = static_cast<std::size_t>(null_frames);
                null_frames -= static_cast<double>(n);
                null_sink.resize(n * 2);
                if (n) app.audio().render(null_sink.data(), n);
            }
            renderer.sync(app.textures());
            renderer.draw(app.draw_list());
            if (!running && hidden) {
                std::fprintf(stderr, "[vnreader] smoke: %s\n", app.status().c_str());
                if (screenshot && !save_screenshot(sdl_renderer, screenshot)) std::fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
            }
            SDL_RenderPresent(sdl_renderer);
            if (hidden) SDL_Delay(1);
        }
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
