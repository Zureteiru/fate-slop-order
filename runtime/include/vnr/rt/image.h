// Image decoding (PNG/JPEG via stb_image) and reading PNG dimensions from a header.
#pragma once

#include <cstdint>
#include <vector>

namespace vnr::rt {

struct Image {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;  // width * height * 4, straight alpha
};

bool decode_image(const std::vector<std::uint8_t>& bytes, Image& out);

// Width/height from a PNG's IHDR; needs only the first 24 bytes (used with HTTP range requests).
bool png_size(const std::vector<std::uint8_t>& bytes, int& width, int& height);

}  // namespace vnr::rt
