#include "vnr/rt/mp3.h"

#define DR_MP3_NO_STDIO
#include "dr_mp3.h"

namespace vnr::rt {

struct Mp3Source::Impl {
    SharedBytes bytes;
    drmp3 mp3{};
    bool open = false;
    ~Impl() {
        if (open) drmp3_uninit(&mp3);
    }
};

Mp3Source::Mp3Source(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Mp3Source::~Mp3Source() = default;

std::unique_ptr<Mp3Source> Mp3Source::open(SharedBytes bytes) {
    if (!bytes || bytes->empty()) return nullptr;
    auto impl = std::make_unique<Impl>();
    impl->bytes = std::move(bytes);
    if (!drmp3_init_memory(&impl->mp3, impl->bytes->data(), impl->bytes->size(), nullptr)) return nullptr;
    impl->open = true;
    if (impl->mp3.channels < 1 || impl->mp3.channels > 2 || impl->mp3.sampleRate == 0) return nullptr;
    return std::unique_ptr<Mp3Source>(new Mp3Source(std::move(impl)));
}

int Mp3Source::channels() const { return static_cast<int>(impl_->mp3.channels); }
int Mp3Source::sample_rate() const { return static_cast<int>(impl_->mp3.sampleRate); }

std::size_t Mp3Source::read(float* out, std::size_t frames) {
    return static_cast<std::size_t>(drmp3_read_pcm_frames_f32(&impl_->mp3, frames, out));
}

bool Mp3Source::rewind() { return drmp3_seek_to_pcm_frame(&impl_->mp3, 0) != 0; }

bool decode_mp3(const std::uint8_t* data, std::size_t size, DecodedPcm& out) {
    out = {};
    if (!data || size == 0) return false;
    drmp3 mp3{};
    if (!drmp3_init_memory(&mp3, data, size, nullptr)) return false;
    if (mp3.channels < 1 || mp3.channels > 2 || mp3.sampleRate == 0) {
        drmp3_uninit(&mp3);
        return false;
    }
    out.channels = static_cast<int>(mp3.channels);
    out.sample_rate = static_cast<int>(mp3.sampleRate);
    float chunk[1152 * 2 * 4];
    const drmp3_uint64 per = sizeof(chunk) / sizeof(float) / mp3.channels;
    while (true) {
        const drmp3_uint64 n = drmp3_read_pcm_frames_f32(&mp3, per, chunk);
        if (n == 0) break;
        out.samples.insert(out.samples.end(), chunk, chunk + n * mp3.channels);
    }
    drmp3_uninit(&mp3);
    return true;
}

}  // namespace vnr::rt
