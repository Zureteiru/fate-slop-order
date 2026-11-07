#include "vnr/rt/audio.h"

#include <algorithm>
#include <cmath>
#include <variant>

#include "vnr/rt/json.h"

namespace vnr::rt {

using K = AudioEvent::Kind;

AudioVolumes parse_audio_settings(const std::vector<std::uint8_t>& json) {
    AudioVolumes v;
    const auto j = Json::parse(std::string(json.begin(), json.end()));
    if (!j) return v;
    const Json& vol = (*j)["volume"];
    auto get = [&](const char* key, float& out) {
        const double d = vol[key].number(-1);
        if (d >= 0 && d <= 1) out = static_cast<float>(d);
    };
    get("master", v.master);
    get("bgm", v.bgm);
    get("se", v.se);
    get("voice", v.voice);
    v.muted = vol["muted"].boolean(false);
    return v;
}

AudioDirector::AudioDirector(AssetCache& cache, Log& log, Region region, int output_rate)
    : cache_(cache), log_(log), region_(region), mixer_(output_rate) {
    set_volumes(volumes_);
}

const char* AudioDirector::label(K kind) {
    switch (kind) {
        case K::BgmPlay:
        case K::BgmStop: return "bgm";
        case K::VoicePlay:
        case K::VoiceStop: return "voice";
        default: return "se";
    }
}

std::string AudioDirector::clip_for(K kind, const std::string& name, const std::string& folder) {
    if (name.empty()) return {};
    std::vector<std::string> urls;
    if (kind == K::BgmPlay) {
        const AssetUrl u = bgm_url(name, region_);
        urls = {u.primary, u.fallback};
    } else if (kind == K::SePlay) {
        urls = {se_url(name, region_, folder)};
    } else if (kind == K::VoicePlay) {
        if (!folder.empty()) {
            urls = {tvoice_url(folder, name, region_)};
        } else if (const auto u = voice_url(name, region_)) {
            urls = {u->primary, u->fallback};
        }
    }
    urls.erase(std::remove(urls.begin(), urls.end(), std::string()), urls.end());
    if (urls.empty()) return {};
    const std::string key = urls.front();
    Clip& c = clips_[key];
    if (c.urls.empty()) c.urls = std::move(urls);
    return key;
}

void AudioDirector::load(Clip& c) {
    while (c.state == ClipState::Queued || c.state == ClipState::Loading) {
        const std::string& url = c.urls[c.tried];
        const AssetState s = c.state == ClipState::Queued ? cache_.request_bytes(url) : cache_.state(url);
        c.state = ClipState::Loading;
        if (s == AssetState::Loading) return;
        if (s == AssetState::Ready) {
            c.bytes = std::make_shared<const std::vector<std::uint8_t>>(cache_.take_bytes(url));
            c.state = ClipState::Ready;
            return;
        }
        // Failed (or forgotten): try the fallback URL, if any
        if (++c.tried >= c.urls.size()) {
            c.state = ClipState::Failed;
            return;
        }
        c.state = ClipState::Queued;
    }
}

void AudioDirector::prefetch(const Program& program) {
    for (const Instr& in : program.code) {
        if (const auto* say = std::get_if<SayOp>(&in)) {
            if (!say->tvoice_file.empty()) {
                const std::string k = clip_for(K::VoicePlay, say->tvoice_file, say->tvoice_folder);
                if (!k.empty()) prefetch_.push_back(k);
            }
            continue;
        }
        const auto* ex = std::get_if<ExecOp>(&in);
        if (!ex) continue;
        const Command& c = ex->cmd;
        std::string k;
        if (c.name == "bgm") k = clip_for(K::BgmPlay, c.arg(0), {});
        else if (c.name == "se" || c.name == "seLoop") k = clip_for(K::SePlay, c.arg(0), {});
        else if (c.name == "cueSe" || c.name == "cueSeLoop") k = clip_for(K::SePlay, c.arg(1), c.arg(0));
        else if (c.name == "voice") k = clip_for(K::VoicePlay, c.arg(0), {});
        if (!k.empty()) prefetch_.push_back(k);
    }
    advance_loads();
}

void AudioDirector::advance_loads() {
    int loading = 0;
    for (auto& [key, c] : clips_) {
        if (c.state == ClipState::Loading) load(c);
        if (c.state == ClipState::Loading) ++loading;
    }
    while (!prefetch_.empty() && loading < kPrefetchParallel) {
        Clip& c = clips_[prefetch_.front()];
        prefetch_.pop_front();
        if (c.state != ClipState::Queued) continue;
        load(c);
        if (c.state == ClipState::Loading) ++loading;
    }
}

void AudioDirector::drop_pending(K kind, const std::string* name) {
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                  [&](const Pending& p) { return p.ev.kind == kind && (!name || p.ev.name == *name); }),
                   pending_.end());
}

void AudioDirector::handle(const AudioEvent& e) {
    switch (e.kind) {
        case K::BgmPlay: {
            drop_pending(K::BgmPlay);
            if (bgm_playing() == e.name) {  // the same BGM keeps playing; only its volume follows
                std::lock_guard<std::mutex> lock(mu_);
                mixer_.set_volume_named(Bus::Bgm, e.name, e.volume, e.fade);
                return;
            }
            {
                std::lock_guard<std::mutex> lock(mu_);
                mixer_.stop_bus(Bus::Bgm, e.fade);  // crossfade over the new track's fade-in
            }
            pending_.push_back({e, clip_for(K::BgmPlay, e.name, {}), 0});
            break;
        }
        case K::BgmStop: {
            drop_pending(K::BgmPlay);
            std::lock_guard<std::mutex> lock(mu_);
            mixer_.stop_bus(Bus::Bgm, e.fade);
            return;
        }
        case K::SePlay:
            pending_.push_back({e, clip_for(K::SePlay, e.name, e.folder), 0});
            break;
        case K::SeStop: {
            drop_pending(K::SePlay, &e.name);
            std::lock_guard<std::mutex> lock(mu_);
            mixer_.stop_named(Bus::Se, e.name, e.fade);
            return;
        }
        case K::SeVolume: {
            for (Pending& p : pending_)
                if (p.ev.kind == K::SePlay && p.ev.name == e.name) p.ev.volume = e.volume;
            std::lock_guard<std::mutex> lock(mu_);
            mixer_.set_volume_named(Bus::Se, e.name, e.volume, e.fade);
            return;
        }
        case K::SeStopAll: {
            drop_pending(K::SePlay);
            std::lock_guard<std::mutex> lock(mu_);
            mixer_.stop_bus(Bus::Se, e.fade);
            return;
        }
        case K::VoicePlay: {
            drop_pending(K::VoicePlay);
            {
                std::lock_guard<std::mutex> lock(mu_);
                mixer_.stop_bus(Bus::Voice, 0);  // a new line cuts the previous voice
            }
            voice_id_ = 0;
            voice_done_ = false;
            Pending p{e, clip_for(K::VoicePlay, e.name, e.folder), 0};
            if (p.clip.empty()) {
                log_.write("audio: voice " + e.name + " has no URL; skipped");
                voice_done_ = true;
                return;
            }
            pending_.push_back(std::move(p));
            break;
        }
        case K::VoiceStop: {
            drop_pending(K::VoicePlay);
            {
                std::lock_guard<std::mutex> lock(mu_);
                mixer_.stop_bus(Bus::Voice, 0);
            }
            voice_id_ = 0;
            voice_done_ = false;
            return;
        }
        case K::Movie: return;  // the App reports movies as finished; no playback yet
    }
    advance_loads();
    pump();
}

bool AudioDirector::start(const Pending& p) {
    const Clip& c = clips_[p.clip];
    auto src = Mp3Source::open(c.bytes);
    const char* what = label(p.ev.kind);
    if (!src) {
        log_.write(std::string("audio: ") + what + " " + p.ev.name + " could not be decoded");
        return false;
    }
    PlayParams params{p.ev.name, p.ev.volume, p.ev.kind == K::SePlay || p.ev.kind == K::BgmPlay ? p.ev.fade : 0,
                      p.ev.kind == K::BgmPlay || p.ev.loop};
    const Bus bus = p.ev.kind == K::BgmPlay ? Bus::Bgm : p.ev.kind == K::VoicePlay ? Bus::Voice : Bus::Se;
    SoundId id = 0;
    {
        std::lock_guard<std::mutex> lock(mu_);
        id = mixer_.play(bus, std::move(src), params);
    }
    if (bus == Bus::Voice) voice_id_ = id;
    std::string msg = std::string("audio: ") + what + " " + p.ev.name + " playing";
    if (params.loop) msg += " (loop)";
    if (p.age > 0.05) msg += " (" + std::to_string(static_cast<int>(p.age * 1000)) + " ms after its cue)";
    log_.write(msg);
    return id != 0;
}

void AudioDirector::pump() {
    for (auto it = pending_.begin(); it != pending_.end();) {
        const Pending& p = *it;
        const auto ci = p.clip.empty() ? clips_.end() : clips_.find(p.clip);
        const ClipState s = ci == clips_.end() ? ClipState::Failed : ci->second.state;
        if (s == ClipState::Queued || s == ClipState::Loading) {
            if (s == ClipState::Queued) load(ci->second);
            if (ci->second.state != ClipState::Ready) {
                ++it;
                continue;
            }
        }
        const bool is_voice = p.ev.kind == K::VoicePlay;
        if (ci == clips_.end() || ci->second.state == ClipState::Failed) {
            log_.write(std::string("audio: ") + label(p.ev.kind) + " " + p.ev.name + " could not be loaded; skipped");
            if (is_voice) voice_done_ = true;
        } else if (p.ev.kind == K::SePlay && p.age > kSeLateLimit) {
            log_.write("audio: se " + p.ev.name + " loaded too late; skipped");
        } else if (!start(p) && is_voice) {
            voice_done_ = true;
        }
        it = pending_.erase(it);
    }
}

void AudioDirector::update(double dt) {
    for (Pending& p : pending_) p.age += dt;
    advance_loads();
    pump();
    std::vector<Mixer::Finished> fin;
    {
        std::lock_guard<std::mutex> lock(mu_);
        fin = mixer_.take_finished();
    }
    for (const Mixer::Finished& f : fin) {
        if (f.id != voice_id_) continue;
        voice_id_ = 0;
        if (f.ended) voice_done_ = true;
    }
}

bool AudioDirector::take_voice_finished() {
    const bool d = voice_done_;
    voice_done_ = false;
    return d;
}

void AudioDirector::on_back() {
    drop_pending(K::SePlay);
    std::lock_guard<std::mutex> lock(mu_);
    mixer_.stop_bus(Bus::Se, 0.2);
}

void AudioDirector::play_ui(const std::string& name, const SharedBytes& bytes, float volume) {
    auto src = Mp3Source::open(bytes);
    if (!src) {
        log_.write("audio: " + name + " could not be decoded");
        return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    mixer_.stop_named(Bus::Ui, name, 0);  // quick repeats restart the click
    mixer_.play(Bus::Ui, std::move(src), {name, volume, 0, false});
}

void AudioDirector::stop_all(double fade) {
    pending_.clear();
    voice_id_ = 0;
    voice_done_ = false;
    std::lock_guard<std::mutex> lock(mu_);
    for (Bus b : {Bus::Bgm, Bus::Se, Bus::Voice, Bus::Ui}) mixer_.stop_bus(b, fade);
}

void AudioDirector::set_volumes(const AudioVolumes& v) {
    volumes_ = v;
    auto clamp = [](float x) { return std::clamp(std::isfinite(x) ? x : 1.f, 0.f, 1.f); };
    std::lock_guard<std::mutex> lock(mu_);
    mixer_.set_master(clamp(v.master));
    mixer_.set_bus_volume(Bus::Bgm, clamp(v.bgm));
    mixer_.set_bus_volume(Bus::Se, clamp(v.se));
    mixer_.set_bus_volume(Bus::Voice, clamp(v.voice));
    mixer_.set_bus_volume(Bus::Ui, clamp(v.se));
    mixer_.set_muted(v.muted);
}

std::string AudioDirector::bgm_playing() const {
    std::lock_guard<std::mutex> lock(mu_);
    return mixer_.active_name(Bus::Bgm);
}

std::size_t AudioDirector::se_playing() const {
    std::lock_guard<std::mutex> lock(mu_);
    return mixer_.active(Bus::Se);
}

std::size_t AudioDirector::ui_playing() const {
    std::lock_guard<std::mutex> lock(mu_);
    return mixer_.active(Bus::Ui);
}

bool AudioDirector::voice_playing() const {
    std::lock_guard<std::mutex> lock(mu_);
    return mixer_.active(Bus::Voice) > 0;
}

std::size_t AudioDirector::clips_loading() const {
    std::size_t n = 0;
    for (const auto& [k, c] : clips_)
        if (c.state == ClipState::Loading) ++n;
    return n;
}

void AudioDirector::set_output_rate(int rate) {
    std::lock_guard<std::mutex> lock(mu_);
    mixer_.set_output_rate(rate);
}

void AudioDirector::render(float* out, std::size_t frames) {
    std::lock_guard<std::mutex> lock(mu_);
    mixer_.render(out, frames);
}

}  // namespace vnr::rt
