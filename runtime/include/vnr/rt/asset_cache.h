// Downloads with a disk cache: bytes (scripts, JSON, header ranges) and decoded textures.
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "vnr/rt/draw_list.h"
#include "vnr/rt/platform.h"

namespace vnr::rt {

enum class AssetState { Missing, Loading, Ready, Failed };

struct TextureAsset {
    TextureId id = 0;
    int width = 0, height = 0;
};

class AssetCache {
public:
    AssetCache(Platform& platform, TextureStore& textures) : platform_(platform), textures_(textures) {}

    // Starts a fetch (or loads from the disk cache immediately). Safe to call every frame.
    AssetState request_bytes(const std::string& url, const std::string& range = {});
    AssetState request_texture(const std::string& url);
    // Like request_bytes, but always downloads (the disk copy is only replaced on success). For files
    // that change upstream, e.g. the chapter catalogue.
    AssetState request_fresh_bytes(const std::string& url);
    // Drops what is known about `url` (e.g. after a failure, so it can be requested again).
    void forget(const std::string& url, const std::string& range = {});

    AssetState state(const std::string& url, const std::string& range = {}) const;
    const std::vector<std::uint8_t>* bytes(const std::string& url, const std::string& range = {}) const;
    const TextureAsset* texture(const std::string& url) const;
    // Moves the downloaded bytes out (e.g. into an audio clip) and forgets the entry, so memory is not
    // held twice; a later request reads the disk cache again. Empty unless the bytes are Ready.
    std::vector<std::uint8_t> take_bytes(const std::string& url, const std::string& range = {});

    void poll();                      // collect finished downloads
    std::size_t pending() const { return inflight_.size(); }
    std::size_t failed() const { return failed_; }

    // Cache key for a URL: host + path (+ query/range), safe as a relative file path. Keys longer than
    // kMaxKeyLength end in a hash instead, so the full path fits Windows' limit.
    static constexpr std::size_t kMaxKeyLength = 150;
    static std::string cache_key(const std::string& url, const std::string& range);

private:
    struct Entry {
        AssetState state = AssetState::Missing;
        bool texture = false;
        std::vector<std::uint8_t> bytes;
        TextureAsset tex;
    };
    Platform& platform_;
    TextureStore& textures_;
    std::map<std::string, Entry> entries_;          // by cache key
    std::map<RequestId, std::string> inflight_;     // request -> cache key
    std::size_t failed_ = 0;

    AssetState request(const std::string& url, const std::string& range, bool texture, bool fresh = false);
    void finish(Entry& e, const std::string& key, std::vector<std::uint8_t> data, bool from_network);
};

}  // namespace vnr::rt
