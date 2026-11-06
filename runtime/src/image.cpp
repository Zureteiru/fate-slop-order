#include "vnr/rt/image.h"

#include <cstring>

#include "stb_image.h"

namespace vnr::rt {

bool decode_image(const std::vector<std::uint8_t>& bytes, Image& out) {
    if (bytes.empty() || bytes.size() > 0x7fffffff) return false;
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (!data) return false;
    out.width = w;
    out.height = h;
    out.rgba.assign(data, data + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    stbi_image_free(data);
    return true;
}

bool png_size(const std::vector<std::uint8_t>& b, int& width, int& height) {
    static const std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (b.size() < 24 || std::memcmp(b.data(), kSig, 8) != 0 || std::memcmp(b.data() + 12, "IHDR", 4) != 0) return false;
    auto be32 = [&](std::size_t o) {
        return static_cast<int>((static_cast<std::uint32_t>(b[o]) << 24) | (static_cast<std::uint32_t>(b[o + 1]) << 16) |
                                (static_cast<std::uint32_t>(b[o + 2]) << 8) | static_cast<std::uint32_t>(b[o + 3]));
    };
    width = be32(16);
    height = be32(20);
    return width > 0 && height > 0;
}

}  // namespace vnr::rt
