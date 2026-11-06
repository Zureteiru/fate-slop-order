// MP3 decoding with the vendored dr_mp3 (pure C, no OS codecs): a streaming AudioSource over
// bytes kept compressed in memory, and a whole-file decode for tests and short clips.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "vnr/rt/audio_mixer.h"

namespace vnr::rt {

using SharedBytes = std::shared_ptr<const std::vector<std::uint8_t>>;

class Mp3Source final : public AudioSource {
public:
    // Null when the bytes are not a decodable MP3. Truncated or damaged data ends the clip early.
    static std::unique_ptr<Mp3Source> open(SharedBytes bytes);
    ~Mp3Source() override;

    int channels() const override;
    int sample_rate() const override;
    std::size_t read(float* out, std::size_t frames) override;
    bool rewind() override;

private:
    struct Impl;
    explicit Mp3Source(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

struct DecodedPcm {
    int channels = 0, sample_rate = 0;
    std::vector<float> samples;  // interleaved
    std::size_t frames() const { return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0; }
};
// Decodes everything; false (and empty) when the data is not an MP3.
bool decode_mp3(const std::uint8_t* data, std::size_t size, DecodedPcm& out);

}  // namespace vnr::rt
