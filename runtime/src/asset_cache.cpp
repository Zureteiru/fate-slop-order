#include "vnr/rt/asset_cache.h"

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
    return out;
}

AssetState AssetCache::request_bytes(const std::string& url, const std::string& range) { return request(url, range, false); }

AssetState AssetCache::request_texture(const std::string& url) { return request(url, {}, true); }

AssetState AssetCache::request_fresh_bytes(const std::string& url) { return request(url, {}, false, true); }

void AssetCache::forget(const std::string& url, const std::string& range) {
    const auto it = entries_.find(cache_key(url, range));
    if (it == entries_.end() || it->second.state == AssetState::Loading) return;
    if (it->second.texture && it->second.tex.id) textures_.release(it->second.tex.id);
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
        e.tex.id = textures_.create(std::move(img));
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
