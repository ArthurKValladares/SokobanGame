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
};

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
