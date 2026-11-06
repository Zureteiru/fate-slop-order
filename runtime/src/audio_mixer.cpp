#include "vnr/rt/audio_mixer.h"

#include <algorithm>
#include <cmath>

namespace vnr::rt {

namespace {
constexpr std::size_t kChunk = 1024;  // source frames decoded at a time
}

std::size_t PcmSource::read(float* out, std::size_t frames) {
    const std::size_t total = channels_ > 0 ? samples_.size() / static_cast<std::size_t>(channels_) : 0;
    const std::size_t n = std::min(frames, total - std::min(total, pos_));
    std::copy_n(samples_.begin() + static_cast<std::ptrdiff_t>(pos_ * channels_), n * channels_, out);
    pos_ += n;
    return n;
}

Mixer::Mixer(int output_rate) : rate_(output_rate > 0 ? output_rate : 48000) {}

void Mixer::set_output_rate(int rate) {
    if (rate > 0) rate_ = rate;
}

Mixer::Sound* Mixer::find(SoundId id) {
    for (Sound& s : sounds_)
        if (s.id == id && !s.done) return &s;
    return nullptr;
}

const Mixer::Sound* Mixer::find(SoundId id) const {
    for (const Sound& s : sounds_)
        if (s.id == id && !s.done) return &s;
    return nullptr;
}

void Mixer::ramp(Sound& s, float to, double seconds) {
    const double frames = std::round(seconds * rate_);
    if (!(frames >= 1)) {  // also catches NaN
        s.gain = to;
        s.ramp_left = 0;
        if (s.stopping && to <= 0) s.done = true;
        return;
    }
    s.target = to;
    s.ramp_left = static_cast<int>(std::min(frames, 1e9));
    s.step = (to - s.gain) / static_cast<float>(s.ramp_left);
}

SoundId Mixer::play(Bus bus, std::unique_ptr<AudioSource> source, PlayParams params) {
    if (!source || source->channels() < 1 || source->sample_rate() <= 0) return 0;
    Sound s;
    s.id = next_id_++;
    s.bus = bus;
    s.src = std::move(source);
    s.params = std::move(params);
    s.params.volume = std::clamp(s.params.volume, 0.f, 1.f);
    s.buf.resize(kChunk * static_cast<std::size_t>(s.src->channels()));
    s.gain = 0;
    ramp(s, s.params.volume, s.params.fade_in);
    sounds_.push_back(std::move(s));
    return sounds_.back().id;
}

void Mixer::stop(SoundId id, double fade) {
    Sound* s = find(id);
    if (!s) return;
    s->stopping = true;
    ramp(*s, 0, fade);
    if (s->gain <= 0 && s->ramp_left == 0) s->done = true;
    if (s->done) {
        finished_.push_back({s->id, s->bus, false});
        sounds_.erase(sounds_.begin() + (s - sounds_.data()));
    }
}

void Mixer::stop_bus(Bus bus, double fade) {
    std::vector<SoundId> ids;
    for (const Sound& s : sounds_)
        if (s.bus == bus && !s.done) ids.push_back(s.id);
    for (SoundId id : ids) stop(id, fade);
}

void Mixer::stop_named(Bus bus, const std::string& name, double fade) {
    std::vector<SoundId> ids;
    for (const Sound& s : sounds_)
        if (s.bus == bus && !s.done && s.params.name == name) ids.push_back(s.id);
    for (SoundId id : ids) stop(id, fade);
}

void Mixer::set_volume(SoundId id, float volume, double fade) {
    Sound* s = find(id);
    if (!s || s->stopping) return;
    s->params.volume = std::clamp(volume, 0.f, 1.f);
    ramp(*s, s->params.volume, fade);
}

void Mixer::set_volume_named(Bus bus, const std::string& name, float volume, double fade) {
    for (Sound& s : sounds_)
        if (s.bus == bus && !s.done && !s.stopping && s.params.name == name) {
            s.params.volume = std::clamp(volume, 0.f, 1.f);
            ramp(s, s.params.volume, fade);
        }
}

bool Mixer::playing(SoundId id) const { return find(id) != nullptr; }

const std::string* Mixer::name_of(SoundId id) const {
    const Sound* s = find(id);
    return s ? &s->params.name : nullptr;
}

std::size_t Mixer::active(Bus bus) const {
    std::size_t n = 0;
    for (const Sound& s : sounds_)
        if (s.bus == bus && !s.done && !s.stopping) ++n;
    return n;
}

std::string Mixer::active_name(Bus bus) const {
    for (auto it = sounds_.rbegin(); it != sounds_.rend(); ++it)
        if (it->bus == bus && !it->done && !it->stopping) return it->params.name;
    return {};
}

std::vector<Mixer::Finished> Mixer::take_finished() {
    std::vector<Finished> out;
    out.swap(finished_);
    return out;
}

// The next source frame as stereo; false at the end of the clip (after looping, if it loops).
bool Mixer::next_frame(Sound& s, float out[2]) {
    if (s.buf_pos >= s.buf_len) {
        if (s.eof) return false;
        std::size_t n = s.src->read(s.buf.data(), kChunk);
        if (n == 0 && s.params.loop && s.src->rewind()) n = s.src->read(s.buf.data(), kChunk);
        n = std::min(n, kChunk);
        if (n == 0) {
            s.eof = true;
            return false;
        }
        s.buf_len = n;
        s.buf_pos = 0;
    }
    const std::size_t ch = static_cast<std::size_t>(s.src->channels());
    const float* f = &s.buf[s.buf_pos * ch];
    out[0] = f[0];
    out[1] = ch > 1 ? f[1] : f[0];
    ++s.buf_pos;
    return true;
}

void Mixer::render(float* out, std::size_t frames) {
    std::fill_n(out, frames * 2, 0.f);
    const float master = muted_ ? 0.f : master_;
    for (Sound& s : sounds_) {
        if (s.done) continue;
        if (!s.primed) {
            s.primed = true;
            if (!next_frame(s, s.a)) {
                s.done = s.ended = true;
                continue;
            }
            s.b_valid = next_frame(s, s.b);
            if (!s.b_valid) s.b[0] = s.b[1] = 0;
        }
        const double ratio = static_cast<double>(s.src->sample_rate()) / rate_;
        const float bus = bus_[static_cast<int>(s.bus)] * master;
        for (std::size_t i = 0; i < frames; ++i) {
            const float g = s.gain * bus;
            const float t = static_cast<float>(s.frac);
            out[2 * i] += (s.a[0] + (s.b[0] - s.a[0]) * t) * g;
            out[2 * i + 1] += (s.a[1] + (s.b[1] - s.a[1]) * t) * g;
            if (s.ramp_left > 0) {
                s.gain += s.step;
                if (--s.ramp_left == 0) s.gain = s.target;
                if (s.stopping && s.ramp_left == 0) {
                    s.done = true;
                    break;
                }
            }
            s.frac += ratio;
            while (s.frac >= 1) {
                s.frac -= 1;
                if (!s.b_valid) {
                    s.done = s.ended = true;
                    break;
                }
                s.a[0] = s.b[0];
                s.a[1] = s.b[1];
                s.b_valid = next_frame(s, s.b);
                if (!s.b_valid) s.b[0] = s.b[1] = 0;
            }
            if (s.done) break;
        }
    }
    for (std::size_t i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.f, 1.f);
    for (auto it = sounds_.begin(); it != sounds_.end();) {
        if (it->done) {
            finished_.push_back({it->id, it->bus, it->ended});
            it = sounds_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace vnr::rt
