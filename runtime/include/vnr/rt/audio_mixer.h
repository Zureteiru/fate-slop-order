// Software mixer: three buses (BGM, SE, voice), per-sound fades and volume, loops, resampling to
// the output rate. Single-threaded and lock-free on purpose: AudioDirector owns one and guards it
// with its own mutex, so tests can drive it sample-exactly.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vnr::rt {

// A stream of interleaved float PCM frames (1 or 2 channels). Once handed to the Mixer it is only
// touched from the mixing thread.
class AudioSource {
public:
    virtual ~AudioSource() = default;
    virtual int channels() const = 0;
    virtual int sample_rate() const = 0;
    // Reads up to `frames` frames into `out` (frames * channels floats). Fewer at the end; 0 = done.
    virtual std::size_t read(float* out, std::size_t frames) = 0;
    virtual bool rewind() = 0;  // back to the first frame (for loops)
};

// Already-decoded PCM in memory (tests, short generated sounds).
class PcmSource final : public AudioSource {
public:
    PcmSource(std::vector<float> samples, int channels, int rate)
        : samples_(std::move(samples)), channels_(channels), rate_(rate) {}
    int channels() const override { return channels_; }
    int sample_rate() const override { return rate_; }
    std::size_t read(float* out, std::size_t frames) override;
    bool rewind() override {
        pos_ = 0;
        return true;
    }

private:
    std::vector<float> samples_;
    int channels_, rate_;
    std::size_t pos_ = 0;  // in frames
};

enum class Bus { Bgm = 0, Se = 1, Voice = 2 };
constexpr int kBusCount = 3;

using SoundId = std::uint64_t;

struct PlayParams {
    std::string name;     // what SeStop / SeVolume match against
    float volume = 1;     // per-sound volume, 0..1
    double fade_in = 0;   // seconds from silence to `volume`
    bool loop = false;
};

class Mixer {
public:
    explicit Mixer(int output_rate = 48000);

    void set_output_rate(int rate);
    int output_rate() const { return rate_; }

    SoundId play(Bus bus, std::unique_ptr<AudioSource> source, PlayParams params);
    void stop(SoundId id, double fade);  // fade 0 = cut now
    void stop_bus(Bus bus, double fade);
    void stop_named(Bus bus, const std::string& name, double fade);
    void set_volume(SoundId id, float volume, double fade);
    void set_volume_named(Bus bus, const std::string& name, float volume, double fade);

    void set_bus_volume(Bus bus, float v) { bus_[static_cast<int>(bus)] = v; }
    float bus_volume(Bus bus) const { return bus_[static_cast<int>(bus)]; }
    void set_master(float v) { master_ = v; }
    float master() const { return master_; }
    void set_muted(bool m) { muted_ = m; }
    bool muted() const { return muted_; }

    // Mixes `frames` stereo frames into `out` (overwrites; 2 * frames floats, clamped to [-1, 1]).
    void render(float* out, std::size_t frames);

    bool playing(SoundId id) const;  // still in the mix (including while fading out)
    const std::string* name_of(SoundId id) const;
    std::size_t active(Bus bus) const;  // sounds on the bus not already stopping
    std::string active_name(Bus bus) const;  // the newest such sound's name; empty if none
    // Sounds that left the mix since the last call: `ended` = reached the end of the clip by itself.
    struct Finished {
        SoundId id;
        Bus bus;
        bool ended;
    };
    std::vector<Finished> take_finished();

private:
    struct Sound {
        SoundId id = 0;
        Bus bus = Bus::Se;
        std::unique_ptr<AudioSource> src;
        PlayParams params;
        // gain ramp, per output frame
        float gain = 0, target = 0, step = 0;
        int ramp_left = 0;
        bool stopping = false;  // removed when the ramp reaches 0
        // decoding + linear resampling
        std::vector<float> buf;  // decoded source frames (interleaved, src channels)
        std::size_t buf_pos = 0, buf_len = 0;
        float a[2] = {0, 0}, b[2] = {0, 0};  // current and next source frame
        double frac = 0;
        bool primed = false, b_valid = false, eof = false, done = false, ended = false;
    };
    int rate_;
    float bus_[kBusCount] = {1, 1, 1};
    float master_ = 1;
    bool muted_ = false;
    SoundId next_id_ = 1;
    std::vector<Sound> sounds_;
    std::vector<Finished> finished_;

    Sound* find(SoundId id);
    const Sound* find(SoundId id) const;
    void ramp(Sound& s, float to, double seconds);
    bool next_frame(Sound& s, float out[2]);
};

}  // namespace vnr::rt
