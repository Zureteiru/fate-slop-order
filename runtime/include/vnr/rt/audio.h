// Turns the Player's AudioEvents into sound: fetches the MP3s through the AssetCache (disk cache,
// fallback URLs), decodes them with dr_mp3 and feeds the Mixer. Reports when the current voice
// ends so the App can call Player::voice_finished().
//
// Threads: everything is called from the app's main thread except render() (and
// set_output_rate()), which the platform's audio callback calls. The Mixer is the only state the
// two share, and it is guarded by one mutex; downloads and decoder setup stay on the main thread.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "vnr/assets.h"
#include "vnr/player.h"
#include "vnr/program.h"
#include "vnr/rt/asset_cache.h"
#include "vnr/rt/audio_mixer.h"
#include "vnr/rt/mp3.h"
#include "vnr/rt/platform.h"

namespace vnr::rt {

struct AudioVolumes {
    float master = 0.8f, bgm = 1, se = 1, voice = 1;  // 0..1
    bool muted = false;
};
// `{"volume": {"master": 0.8, "bgm": 1, "se": 1, "voice": 1}}` (settings.json in the user folder).
// Missing or bad values keep the defaults.
AudioVolumes parse_audio_settings(const std::vector<std::uint8_t>& json);

class AudioDirector {
public:
    static constexpr double kSeLateLimit = 0.5;  // an SE that loads later than this is dropped
    static constexpr int kPrefetchParallel = 4;

    AudioDirector(AssetCache& cache, Log& log, Region region, int output_rate = 48000);

    // ---- main thread
    void prefetch(const Program& program);  // fetch every clip the script uses, in script order
    void handle(const AudioEvent& e);
    void on_back();                          // the reader went back: SEs of the later line stop
    void update(double dt);                  // downloads, delayed starts, finished sounds
    bool take_voice_finished();              // true once after the current voice ended (or failed)
    void stop_all(double fade);
    // An app sound already in memory (e.g. the choice click), on the Ui bus.
    void play_ui(const std::string& name, const SharedBytes& bytes, float volume);
    void set_volumes(const AudioVolumes& v);
    const AudioVolumes& volumes() const { return volumes_; }

    // inspection (tests, logs)
    std::string bgm_playing() const;  // the BGM in the mix that is not fading out; empty if none
    std::size_t se_playing() const;
    bool voice_playing() const;
    std::size_t ui_playing() const;
    std::size_t clips_loading() const;

    // ---- any thread
    void set_output_rate(int rate);
    void render(float* out, std::size_t frames);  // stereo interleaved

private:
    enum class ClipState { Queued, Loading, Ready, Failed };
    struct Clip {
        std::vector<std::string> urls;  // primary, then fallback
        std::size_t tried = 0;
        ClipState state = ClipState::Queued;
        SharedBytes bytes;
    };
    struct Pending {
        AudioEvent ev;
        std::string clip;  // key in clips_ (empty = no URL for it)
        double age = 0;
    };

    AssetCache& cache_;
    Log& log_;
    Region region_;
    AudioVolumes volumes_;

    std::map<std::string, Clip> clips_;
    std::deque<std::string> prefetch_;
    std::vector<Pending> pending_;
    SoundId voice_id_ = 0;
    bool voice_done_ = false;

    mutable std::mutex mu_;
    Mixer mixer_;  // guarded by mu_

    std::string clip_for(AudioEvent::Kind kind, const std::string& name, const std::string& folder);
    void load(Clip& c);   // start / continue a download
    void advance_loads();
    void pump();          // start pending plays whose clip is ready
    bool start(const Pending& p);
    void drop_pending(AudioEvent::Kind kind, const std::string* name = nullptr);
    static const char* label(AudioEvent::Kind kind);
};

}  // namespace vnr::rt
