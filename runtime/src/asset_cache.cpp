#include "vnr/rt/asset_cache.h"

#include <algorithm>
#include <cmath>

namespace vnr::rt {

std::string AssetCache::cache_key(const std::string& url, const std::string& range) {
    std::string s = url;
    const std::size_t scheme = s.find("://");
    if (scheme != std::string::npos) s = s.substr(scheme + 3);
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' ||
                        c == '.' || c == '-' || c == '_';
        out += ok ? c : '_';
    }
    while (out.find("..") != std::string::npos) out.replace(out.find(".."), 2, "_");  // never escape the cache dir
    if (!range.empty()) out += ".range_" + range;
    // Long URLs (svtScript with a dozen charaIds) would pass Windows' 260-character path limit: keep
    // the start and replace the rest with a hash of the whole key.
    if (out.size() > kMaxKeyLength) {
        std::uint64_t h = 1469598103934665603ull;  // FNV-1a
        for (const char c : out) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ull;
        }
        static const char* hex = "0123456789abcdef";
        std::string tail = "_h";
        for (int i = 15; i >= 0; --i) tail += hex[(h >> (i * 4)) & 0xF];
        out = out.substr(0, kMaxKeyLength - tail.size()) + tail;
    }
    return out;
}

AssetState AssetCache::request_bytes(const std::string& url, const std::string& range) { return request(url, range, false); }

AssetState AssetCache::request_texture(const std::string& url) { return request(url, {}, true); }

AssetState AssetCache::request_fresh_bytes(const std::string& url) { return request(url, {}, false, true); }

void AssetCache::forget(const std::string& url, const std::string& range) {
    const auto it = entries_.find(cache_key(url, range));
    if (it == entries_.end() || it->second.state == AssetState::Loading) return;
    if (it->second.texture && it->second.tex.id) {
        textures_.release(it->second.tex.id);
        for (auto* copies : {&gray_, &sepia_})
            if (const auto g = copies->find(it->second.tex.id); g != copies->end()) {
                textures_.release(g->second);
                copies->erase(g);
            }
    }
    entries_.erase(it);
}

AssetState AssetCache::request(const std::string& url, const std::string& range, bool texture, bool fresh) {
    const std::string key = cache_key(url, range);
    Entry& e = entries_[key];
    if (e.state != AssetState::Missing) return e.state;
    e.texture = texture;
    std::vector<std::uint8_t> cached;
    if (!fresh && platform_.files.read(key, cached) && !cached.empty()) {
        finish(e, key, std::move(cached), false);
        return e.state;
    }
    e.state = AssetState::Loading;
    inflight_[platform_.http.get({url, range})] = key;
    return e.state;
}

TextureId AssetCache::gray(TextureId id) const {
    const auto it = gray_.find(id);
    return it == gray_.end() ? id : it->second;
}

TextureId AssetCache::sepia(TextureId id) const {
    const auto it = sepia_.find(id);
    return it == sepia_.end() ? id : it->second;
}

namespace {
// Fate Café's backdrop: sepia(0.85) saturate(0.9) contrast(1.04) brightness(0.96), as CSS defines them.
void sepia_filter(Image& img) {
    const float a = 0.85f, b = 1 - a, s = 0.9f;
    for (std::size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        std::uint8_t* p = &img.rgba[i];
        const float r = p[0] / 255.f, g = p[1] / 255.f, bl = p[2] / 255.f;
        float o[3] = {(0.393f + 0.607f * b) * r + (0.769f - 0.769f * b) * g + (0.189f - 0.189f * b) * bl,
                      (0.349f - 0.349f * b) * r + (0.686f + 0.314f * b) * g + (0.168f - 0.168f * b) * bl,
                      (0.272f - 0.272f * b) * r + (0.534f - 0.534f * b) * g + (0.131f + 0.869f * b) * bl};
        const float sr[3][3] = {{0.213f + 0.787f * s, 0.715f - 0.715f * s, 0.072f - 0.072f * s},
                                {0.213f - 0.213f * s, 0.715f + 0.285f * s, 0.072f - 0.072f * s},
                                {0.213f - 0.213f * s, 0.715f - 0.715f * s, 0.072f + 0.928f * s}};
        float q[3];
        for (int k = 0; k < 3; ++k) q[k] = sr[k][0] * o[0] + sr[k][1] * o[1] + sr[k][2] * o[2];
        for (int k = 0; k < 3; ++k) {
            const float v = ((q[k] - 0.5f) * 1.04f + 0.5f) * 0.96f;
            p[k] = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255));
        }
    }
}
}  // namespace

void AssetCache::finish(Entry& e, const std::string& key, std::vector<std::uint8_t> data, bool from_network) {
    if (e.texture) {
        Image img;
        if (!decode_image(data, img)) {
            e.state = AssetState::Failed;
            ++failed_;
            platform_.log.write("could not decode image " + key);
            return;
        }
        e.tex.width = img.width;
        e.tex.height = img.height;
        Image gray;
        if (gray_copies_) {  // CSS grayscale(1): Rec. 709 luma
            gray = img;
            for (std::size_t i = 0; i + 3 < gray.rgba.size(); i += 4) {
                std::uint8_t* p = &gray.rgba[i];
                const auto l = static_cast<std::uint8_t>(std::lround(p[0] * 0.2126 + p[1] * 0.7152 + p[2] * 0.0722));
                p[0] = p[1] = p[2] = l;
            }
        }
        Image sep;
        if (sepia_copies_) {
            sep = img;
            sepia_filter(sep);
        }
        e.tex.id = textures_.create(std::move(img));
        if (gray_copies_) gray_[e.tex.id] = textures_.create(std::move(gray));
        if (sepia_copies_) sepia_[e.tex.id] = textures_.create(std::move(sep));
    } else {
        e.bytes = data;
    }
    if (from_network && !platform_.files.write(key, data)) platform_.log.write("cache write failed: " + key);
    e.state = AssetState::Ready;
}

void AssetCache::poll() {
    for (HttpResult& r : platform_.http.poll()) {
        const auto it = inflight_.find(r.id);
        if (it == inflight_.end()) continue;
        const std::string key = it->second;
        inflight_.erase(it);
        Entry& e = entries_[key];
        if ((r.status == 200 || r.status == 206) && !r.body.empty()) {
            finish(e, key, std::move(r.body), true);
        } else {
            e.state = AssetState::Failed;
            ++failed_;
            platform_.log.write("download failed (" + std::to_string(r.status) + "): " + key);
        }
    }
}

AssetState AssetCache::state(const std::string& url, const std::string& range) const {
    const auto it = entries_.find(cache_key(url, range));
    return it == entries_.end() ? AssetState::Missing : it->second.state;
}

const std::vector<std::uint8_t>* AssetCache::bytes(const std::string& url, const std::string& range) const {
    const auto it = entries_.find(cache_key(url, range));
    return (it == entries_.end() || it->second.state != AssetState::Ready || it->second.texture) ? nullptr
                                                                                                 : &it->second.bytes;
}

std::vector<std::uint8_t> AssetCache::take_bytes(const std::string& url, const std::string& range) {
    const auto it = entries_.find(cache_key(url, range));
    if (it == entries_.end() || it->second.state != AssetState::Ready || it->second.texture) return {};
    std::vector<std::uint8_t> out = std::move(it->second.bytes);
    entries_.erase(it);
    return out;
}

const TextureAsset* AssetCache::texture(const std::string& url) const {
    const auto it = entries_.find(cache_key(url, {}));
    return (it == entries_.end() || it->second.state != AssetState::Ready || !it->second.texture) ? nullptr
                                                                                                 : &it->second.tex;
}

}  // namespace vnr::rt
