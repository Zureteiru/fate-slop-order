// Audio: mixer (fades, buses, loops, stop-all, resampling), MP3 decoding (whole, truncated,
// garbage), the director (voice end, missing files, fallbacks, late SEs) and rewind in the App.
#include <algorithm>
#include <cstdint>
#include <random>

#include "check.h"
#include "drive.h"
#include "fakes/fake_platform.h"
#include "vnr/rt/app.h"
#include "vnr/rt/audio.h"
#include "vnr/rt/audio_mixer.h"
#include "vnr/rt/mp3.h"

#ifndef VNR_FIXTURE_DIR
#error "VNR_FIXTURE_DIR must point at .cache/scripts"
#endif

using namespace vnr;
using namespace vnr::rt;

namespace {
const std::string kDir = VNR_FIXTURE_DIR;

std::unique_ptr<AudioSource> constant(float v, std::size_t frames, int rate = 1000, int channels = 2) {
    return std::make_unique<PcmSource>(std::vector<float>(frames * channels, v), channels, rate);
}

std::unique_ptr<AudioSource> ramp_source(std::size_t frames, int rate = 1000) {
    std::vector<float> s;
    for (std::size_t i = 0; i < frames; ++i) s.push_back(static_cast<float>(i) / 100.f);  // mono
    return std::make_unique<PcmSource>(std::move(s), 1, rate);
}

std::vector<float> render(Mixer& m, std::size_t frames) {
    std::vector<float> out(frames * 2, -9.f);
    m.render(out.data(), frames);
    return out;
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

const std::vector<std::uint8_t>& se_mp3() {
    static const std::vector<std::uint8_t> b = fakes::bytes_of(kDir + "/bac8.mp3");
    return b;
}
}  // namespace

// ---------------------------------------------------------------- mixer

TEST(mixer_fade_in_is_a_linear_ramp) {
    Mixer m(1000);
    PlayParams p;
    p.fade_in = 1.0;
    m.play(Bus::Bgm, constant(0.5f, 5000), p);
    const auto out = render(m, 1200);
    CHECK(near(out[0], 0.f));
    CHECK(near(out[1], 0.f));
    CHECK(near(out[2 * 250], 0.125f));
    CHECK(near(out[2 * 500], 0.25f));
    CHECK(near(out[2 * 750 + 1], 0.375f));
    CHECK(near(out[2 * 1000], 0.5f));
    CHECK(near(out[2 * 1199], 0.5f));
}

TEST(mixer_fade_out_then_removed_without_counting_as_ended) {
    Mixer m(1000);
    const SoundId id = m.play(Bus::Se, constant(1.f, 5000), {"ad9", 1, 0, false});
    render(m, 10);
    m.stop(id, 0.5);
    CHECK_EQ(m.active(Bus::Se), static_cast<std::size_t>(0));  // stopping: not active, still audible
    CHECK(m.playing(id));
    auto out = render(m, 250);
    CHECK(near(out[0], 1.f));
    CHECK(near(out[2 * 249], 0.502f, 1e-3f));
    out = render(m, 300);
    CHECK(near(out[2 * 260], 0.f));
    CHECK(!m.playing(id));
    const auto fin = m.take_finished();
    REQUIRE(fin.size() == 1u);
    CHECK_EQ(fin[0].id, id);
    CHECK(!fin[0].ended);
}

TEST(mixer_sound_bus_and_master_volumes_multiply) {
    Mixer m(1000);
    m.set_master(0.5f);
    m.set_bus_volume(Bus::Se, 0.5f);
    m.play(Bus::Se, constant(1.f, 100), {"x", 0.8f, 0, false});
    m.play(Bus::Bgm, constant(1.f, 100), {"y", 1.f, 0, false});
    m.set_bus_volume(Bus::Bgm, 0.f);
    auto out = render(m, 4);
    CHECK(near(out[0], 0.2f));
    m.set_muted(true);
    out = render(m, 4);
    CHECK(near(out[0], 0.f));
    m.set_muted(false);
    m.set_volume_named(Bus::Se, "x", 0.4f, 0);
    out = render(m, 4);
    CHECK(near(out[0], 0.1f));
}

TEST(mixer_loops_and_one_shots_end) {
    Mixer m(1000);
    const SoundId loop = m.play(Bus::Bgm, ramp_source(10), {"bgm", 1, 0, true});
    auto out = render(m, 25);
    CHECK(near(out[2 * 3], 0.03f));
    CHECK(near(out[2 * 10], 0.f));  // wrapped to the first frame
    CHECK(near(out[2 * 23 + 1], 0.03f));
    CHECK(m.playing(loop));

    Mixer m2(1000);
    const SoundId once = m2.play(Bus::Se, ramp_source(10), {"se", 1, 0, false});
    out = render(m2, 20);
    CHECK(near(out[2 * 9], 0.09f));
    CHECK(near(out[2 * 10], 0.f));
    CHECK(!m2.playing(once));
    const auto fin = m2.take_finished();
    REQUIRE(fin.size() == 1u);
    CHECK(fin[0].ended);
}

TEST(mixer_stop_all_and_stop_by_name) {
    Mixer m(1000);
    m.play(Bus::Se, constant(0.1f, 1000), {"a", 1, 0, true});
    m.play(Bus::Se, constant(0.1f, 1000), {"a", 1, 0, true});
    m.play(Bus::Se, constant(0.1f, 1000), {"b", 1, 0, true});
    m.play(Bus::Bgm, constant(0.1f, 1000), {"music", 1, 0, true});
    CHECK_EQ(m.active(Bus::Se), static_cast<std::size_t>(3));
    m.stop_named(Bus::Se, "a", 0);
    CHECK_EQ(m.active(Bus::Se), static_cast<std::size_t>(1));
    m.stop_bus(Bus::Se, 0);
    CHECK_EQ(m.active(Bus::Se), static_cast<std::size_t>(0));
    CHECK_EQ(m.active_name(Bus::Bgm), std::string("music"));
    CHECK_EQ(m.take_finished().size(), static_cast<std::size_t>(3));
    const auto out = render(m, 2);
    CHECK(near(out[0], 0.1f));
}

TEST(mixer_resamples_and_clamps) {
    Mixer m(1000);
    m.play(Bus::Se, ramp_source(10, 500), {"slow", 1, 0, false});  // half rate: every frame twice
    auto out = render(m, 6);
    CHECK(near(out[0], 0.f));
    CHECK(near(out[2], 0.005f));
    CHECK(near(out[4], 0.01f));
    CHECK(near(out[6], 0.015f));

    Mixer loud(1000);
    loud.play(Bus::Se, constant(0.8f, 10), {});
    loud.play(Bus::Se, constant(0.8f, 10), {});
    out = render(loud, 2);
    CHECK(near(out[0], 1.f));
    loud.play(Bus::Se, constant(-0.8f, 10), {});
    loud.play(Bus::Se, constant(-0.8f, 10), {});
    loud.play(Bus::Se, constant(-0.8f, 10), {});
    out = render(loud, 2);
    CHECK(near(out[0], -0.8f));
}

// ---------------------------------------------------------------- mp3

TEST(mp3_decodes_the_fixture) {
    DecodedPcm pcm;
    REQUIRE(decode_mp3(se_mp3().data(), se_mp3().size(), pcm));
    CHECK(pcm.channels == 1 || pcm.channels == 2);
    CHECK(pcm.sample_rate == 44100 || pcm.sample_rate == 48000 || pcm.sample_rate == 22050);
    CHECK(pcm.frames() > 1000u);
    float peak = 0;
    for (float s : pcm.samples) peak = std::max(peak, std::fabs(s));
    CHECK(peak > 0.01f);
    CHECK(peak <= 1.5f);

    // the streaming source gives the same frames
    auto src = Mp3Source::open(std::make_shared<const std::vector<std::uint8_t>>(se_mp3()));
    REQUIRE(src != nullptr);
    std::vector<float> buf(4096 * 2);
    std::size_t frames = 0, n = 0;
    while ((n = src->read(buf.data(), 4096)) > 0) frames += n;
    CHECK_EQ(frames, pcm.frames());
    CHECK(src->rewind());
    CHECK(src->read(buf.data(), 16) == 16u);
}

TEST(mp3_truncated_and_garbage_input_stays_in_bounds) {
    const auto& full = se_mp3();
    DecodedPcm whole;
    REQUIRE(decode_mp3(full.data(), full.size(), whole));
    // Every cut point over the first 600 bytes (headers, first frames), then every 7th byte.
    for (std::size_t len = 0; len < full.size(); len += (len < 600 ? 1 : 7)) {
        // exact-size heap copy, so ASan catches a read past the end
        std::vector<std::uint8_t> cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(len));
        DecodedPcm pcm;
        if (decode_mp3(cut.data(), cut.size(), pcm)) CHECK(pcm.frames() <= whole.frames());
        if (auto src = Mp3Source::open(std::make_shared<const std::vector<std::uint8_t>>(cut))) {
            Mixer m(48000);
            m.play(Bus::Se, std::move(src), {"cut", 1, 0, true});  // loop: rewinds over the cut end too
            std::vector<float> out(512 * 2);
            for (int i = 0; i < 4; ++i) m.render(out.data(), 512);
        }
    }
    std::mt19937 rng(12345);
    for (int round = 0; round < 64; ++round) {
        std::vector<std::uint8_t> junk(static_cast<std::size_t>(rng() % 3000));
        for (auto& b : junk) b = static_cast<std::uint8_t>(rng());
        if (round % 2 == 0 && junk.size() > 4) {  // a frame sync word up front, to get past the first check
            junk[0] = 0xFF;
            junk[1] = 0xFB;
        }
        DecodedPcm pcm;
        decode_mp3(junk.data(), junk.size(), pcm);
        std::vector<std::uint8_t> damaged = full;  // real file with a scrambled middle
        for (std::size_t i = 0; i < 64 && i < damaged.size(); ++i)
            damaged[(rng() % damaged.size())] = static_cast<std::uint8_t>(rng());
        decode_mp3(damaged.data(), damaged.size(), pcm);
    }
    DecodedPcm none;
    CHECK(!decode_mp3(nullptr, 0, none));
    CHECK(Mp3Source::open(nullptr) == nullptr);
}

// ---------------------------------------------------------------- director

namespace {
struct AudioEnv {
    fakes::FakeHttp http;
    fakes::MemFiles files;
    fakes::MemLog log;
    TextureStore textures;
    Platform platform{http, files, log, {}, 0};
    AssetCache cache{platform, textures};
    AudioDirector audio{cache, log, Region::NA, 48000};
    std::vector<std::string> missing;  // URL substrings answered with 404

    AudioEnv() {
        http.handler = [this](const HttpRequest& r) -> HttpResult {
            for (const auto& m : missing)
                if (r.url.find(m) != std::string::npos) return {0, 404, {}};
            if (r.url.find("/Audio/") != std::string::npos) return {0, 200, se_mp3()};
            return {0, 404, {}};
        };
    }
    void step(double dt = 0.01) {
        cache.poll();
        audio.update(dt);
    }
    // Plays `seconds` of output through the mixer, updating as an app frame would.
    void play_for(double seconds) {
        std::vector<float> out(480 * 2);
        for (double t = 0; t < seconds; t += 0.01) {
            audio.render(out.data(), 480);
            step(0.01);
        }
    }
    bool logged(const std::string& s) const {
        return std::any_of(log.lines.begin(), log.lines.end(), [&](const std::string& l) { return l.find(s) != std::string::npos; });
    }
};

AudioEvent ev(AudioEvent::Kind k, std::string name = {}, double fade = 0, float vol = 1, bool loop = false, std::string folder = {}) {
    return {k, std::move(name), std::move(folder), fade, vol, loop};
}
using K = AudioEvent::Kind;
}  // namespace

TEST(director_voice_end_reports_voice_finished_once) {
    AudioEnv env;
    env.audio.handle(ev(K::VoicePlay, "100100_0_A010"));
    env.step();
    env.step();
    CHECK(env.audio.voice_playing());
    CHECK(!env.audio.take_voice_finished());
    env.play_for(3.0);  // the clip is well under a second
    CHECK(!env.audio.voice_playing());
    CHECK(env.audio.take_voice_finished());
    CHECK(!env.audio.take_voice_finished());
    CHECK(env.logged("voice 100100_0_A010 playing"));
    CHECK(env.http.requests.at(0).url.find("/Audio/Servants_100100/0_A010.mp3") != std::string::npos);
}

TEST(director_new_voice_cuts_old_without_finishing_it) {
    AudioEnv env;
    env.audio.handle(ev(K::VoicePlay, "1_a", 0, 1, false, "ChrVoice_1"));
    env.step();
    env.step();
    env.play_for(0.05);
    env.audio.handle(ev(K::VoicePlay, "1_b", 0, 1, false, "ChrVoice_1"));
    env.step();
    env.step();
    env.play_for(0.05);
    CHECK(!env.audio.take_voice_finished());  // the cut voice does not end the new line's wait
    env.audio.handle(ev(K::VoiceStop));
    env.play_for(0.05);
    CHECK(!env.audio.voice_playing());
    CHECK(!env.audio.take_voice_finished());
}

TEST(director_missing_files_do_not_break_playback) {
    AudioEnv env;
    env.missing = {"/Audio/Bgm/", "/Audio/SE/wo1", "Servants_9", "ChrVoice_9"};
    env.audio.handle(ev(K::BgmPlay, "BGM_EVENT_2", 0.1, 1, true));  // primary 404 -> fallback
    env.audio.handle(ev(K::SePlay, "wo1"));                          // 404, no fallback
    env.audio.handle(ev(K::VoicePlay, "9_x"));                       // both voice URLs 404
    for (int i = 0; i < 5; ++i) env.step();
    CHECK_EQ(env.audio.bgm_playing(), std::string("BGM_EVENT_2"));
    CHECK(env.logged("se wo1 could not be loaded"));
    CHECK(env.logged("voice 9_x could not be loaded"));
    CHECK(env.audio.take_voice_finished());  // a voice that can't load must not hang [wait voice]
    env.audio.handle(ev(K::SePlay, "ad8"));
    env.step();
    env.step();
    CHECK_EQ(env.audio.se_playing(), static_cast<std::size_t>(1));
    env.play_for(0.1);
    CHECK_EQ(env.audio.bgm_playing(), std::string("BGM_EVENT_2"));

    // a file that downloads but isn't an MP3
    AudioEnv bad;
    bad.http.handler = [](const HttpRequest&) { return HttpResult{0, 200, {'n', 'o', 'p', 'e'}}; };
    bad.audio.handle(ev(K::VoicePlay, "1_a", 0, 1, false, "ChrVoice_1"));
    bad.step();
    bad.step();
    CHECK(bad.logged("could not be decoded"));
    CHECK(bad.audio.take_voice_finished());
}

TEST(director_bgm_crossfades_and_same_track_continues) {
    AudioEnv env;
    env.audio.handle(ev(K::BgmPlay, "BGM_A", 0, 1, true));
    env.step();
    env.step();
    env.play_for(0.1);
    const std::size_t lines = env.log.lines.size();
    env.audio.handle(ev(K::BgmPlay, "BGM_A", 1.0, 0.5f, true));  // same track: no restart
    env.step();
    CHECK_EQ(env.log.lines.size(), lines);
    env.audio.handle(ev(K::BgmPlay, "BGM_B", 1.0, 1, true));
    env.step();
    env.step();
    CHECK_EQ(env.audio.bgm_playing(), std::string("BGM_B"));
    env.audio.handle(ev(K::BgmStop, "BGM_B", 0.5));
    CHECK_EQ(env.audio.bgm_playing(), std::string());
}

TEST(director_late_se_is_dropped_and_stop_all_clears) {
    AudioEnv env;
    env.audio.handle(ev(K::SePlay, "ad1"));
    for (int i = 0; i < 70; ++i) env.audio.update(0.01);  // download not collected for 0.7 s
    env.step();
    env.step();
    CHECK(env.logged("se ad1 loaded too late"));
    CHECK_EQ(env.audio.se_playing(), static_cast<std::size_t>(0));
    env.audio.handle(ev(K::SePlay, "ad1"));  // cached now: starts at once
    CHECK_EQ(env.audio.se_playing(), static_cast<std::size_t>(1));
    env.audio.handle(ev(K::SeStopAll));
    CHECK_EQ(env.audio.se_playing(), static_cast<std::size_t>(0));
}

TEST(director_prefetches_and_reads_volume_settings) {
    const AudioVolumes v = parse_audio_settings(fakes::bytes_of(kDir + "/NA_0100000010.txt"));  // not JSON
    CHECK(near(v.master, 0.8f));
    const std::string json = R"({"volume":{"master":0.5,"bgm":0.25,"se":2,"voice":0}})";
    const AudioVolumes w = parse_audio_settings({json.begin(), json.end()});
    CHECK(near(w.master, 0.5f));
    CHECK(near(w.bgm, 0.25f));
    CHECK(near(w.se, 1.f));  // out of range: default kept
    CHECK(near(w.voice, 0.f));

    AudioEnv env;
    Diagnostics diag;
    const Program prog = compile(drive::read_file(kDir + "/NA_0100000011.txt"), {}, diag);
    env.audio.prefetch(prog);
    CHECK(env.audio.clips_loading() <= static_cast<std::size_t>(AudioDirector::kPrefetchParallel));
    for (int i = 0; i < 40; ++i) env.step();
    CHECK_EQ(env.audio.clips_loading(), static_cast<std::size_t>(0));
    bool bgm = false;
    for (const auto& r : env.http.requests) bgm |= r.url.find("BGM_EVENT_3") != std::string::npos;
    CHECK(bgm);
}

// ---------------------------------------------------------------- app

TEST(app_rewind_restores_the_bgm_and_does_not_replay_ses) {
    fakes::FakeHttp http;
    fakes::MemFiles files;
    fakes::MemLog log;
    Platform platform{http, files, log, fakes::bytes_of(kDir + "/MPLUS1p-Regular.ttf"), 0};
    http.handler = [](const HttpRequest& r) {
        if (r.url.find("/Audio/") != std::string::npos) return HttpResult{0, 200, se_mp3()};
        return fakes::serve_fixture(kDir, r);
    };
    AppConfig cfg;
    cfg.script_ids = {"0100000011"};
    App app(platform, cfg);
    for (int i = 0; i < 300 && app.phase() != App::Phase::Playing; ++i) app.update(1.0 / 60, {});
    REQUIRE(app.phase() == App::Phase::Playing);

    auto settle = [&] {
        for (int i = 0; i < 4; ++i) app.update(0, {});
    };
    auto tap = [&](InputEvent::Kind k) {
        app.update(0, {{k, {}, 0}});
        settle();
    };
    // forward to the first line under BGM_EVENT_3 (after the bgmStop and SE before it)
    for (int guard = 0; guard < 100000 && app.player()->bgm() != "BGM_EVENT_3"; ++guard) {
        const Player* p = app.player();
        if (p->pending_choice()) app.update(0, {{InputEvent::Kind::Choose, {}, 0}});
        else if (p->status() == Status::Typing || p->status() == Status::AwaitAdvance) app.update(0, {{InputEvent::Kind::Advance, {}, 0}});
        else app.update(drive::kDt, {});
    }
    REQUIRE(app.player()->bgm() == "BGM_EVENT_3");
    tap(InputEvent::Kind::Advance);
    tap(InputEvent::Kind::Advance);
    settle();
    CHECK_EQ(app.audio().bgm_playing(), std::string("BGM_EVENT_3"));

    const std::size_t lines = log.lines.size();
    for (int i = 0; i < 40 && app.player()->bgm() != "BGM_EVENT_2"; ++i) {
        tap(InputEvent::Kind::Back);
        CHECK_EQ(app.audio().bgm_playing(), app.player()->bgm());
        CHECK_EQ(app.audio().se_playing(), static_cast<std::size_t>(0));
    }
    CHECK_EQ(app.player()->bgm(), std::string("BGM_EVENT_2"));
    CHECK_EQ(app.audio().bgm_playing(), std::string("BGM_EVENT_2"));
    for (std::size_t i = lines; i < log.lines.size(); ++i)
        CHECK(log.lines[i].find("audio: se ") == std::string::npos);  // no old SE replayed

    // the LOG screen pauses the story, not the music
    tap(InputEvent::Kind::OpenLog);
    REQUIRE(app.backlog().is_open());
    CHECK_EQ(app.audio().bgm_playing(), std::string("BGM_EVENT_2"));

    // volume keys
    const float before = app.audio().volumes().master;
    tap(InputEvent::Kind::VolumeDown);
    CHECK(near(app.audio().volumes().master, before - 0.1f, 1e-3f));
    tap(InputEvent::Kind::Mute);
    CHECK(app.audio().volumes().muted);
}
