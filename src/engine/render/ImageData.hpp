#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace sokoban {

struct ImageData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::byte> rgba;
    // Original encoded channel count; generated RGBA images default to four.
    // A legacy grayscale terrain mask uses its red channel as layer-1 weight.
    uint8_t sourceChannels = 4;
};

// Converts encoded grayscale masks to (1-R,R,0,0) and clears synthesized alpha
// on RGB masks. Authored RGBA alpha remains the fourth material weight.
void convertLegacySplatWeights(ImageData& image);

[[nodiscard]] ImageData loadRgbaImage(const std::filesystem::path& path);
[[nodiscard]] ImageData loadRgbaImage(
    std::span<const std::byte> encoded,
    std::string_view label);

// Reads only image metadata and returns the exact RGBA payload size produced
// by loadRgbaImage(). File-backed inspection reads the encoded source once;
// callers can cache the result outside request and frame paths.
[[nodiscard]] uint64_t inspectRgbaImagePayloadBytes(
    const std::filesystem::path& path);
[[nodiscard]] uint64_t inspectRgbaImagePayloadBytes(
    std::span<const std::byte> encoded,
    std::string_view label);

} // namespace sokoban
