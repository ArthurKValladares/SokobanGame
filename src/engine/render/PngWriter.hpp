#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace sokoban {

// Minimal 8-bit greyscale PNG encoder.
//
// The engine only reads images (stb_image); this is the one place that writes
// one, for splat maps painted in the level editor. It is deliberately narrow
// rather than a general image library: greyscale is exactly what a splat
// weight map is, and a quarter of the bytes of RGBA.
//
// Self-contained - no zlib, no stb_image_write. The deflate stream uses fixed
// Huffman codes with greedy LZ77 matching, which is not the smallest possible
// encoding but is a fraction of the size of stored blocks and is verified by
// round-tripping through stb_image in tests/PngWriterTests.cpp.
[[nodiscard]] std::vector<std::byte> encodeGrayscalePng(
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& pixels);

// Encodes and writes atomically, so an interrupted or failed save cannot
// leave a half-written map that the content pipeline would then reject.
// Throws on encode or IO failure.
void writeGrayscalePng(
    const std::filesystem::path& path,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& pixels);

} // namespace sokoban
