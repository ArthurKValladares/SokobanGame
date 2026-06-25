#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace sokoban {

struct ImageData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::byte> rgba;
};

[[nodiscard]] ImageData loadRgbaImage(const std::filesystem::path& path);

} // namespace sokoban
