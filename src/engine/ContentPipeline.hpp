#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace sokoban {

struct ContentSourceRoots {
    std::filesystem::path assets;
    std::filesystem::path levels;
    std::filesystem::path shaders;
};

struct ContentFile {
    std::filesystem::path source;
    std::filesystem::path destination;
    std::uintmax_t size = 0;
};

struct ContentInventory {
    std::vector<ContentFile> files;
    std::uintmax_t totalBytes = 0;
};

// Resolves and validates every file needed by a distributable build. Manifest
// references remain relative to the assets root, while levels and compiled
// shaders are added under levels/ and shaders/ respectively.
[[nodiscard]] ContentInventory collectContentInventory(const ContentSourceRoots& roots);

// Replaces outputRoot with a clean, complete content tree and writes a
// versioned content.index. Staging through a sibling temporary directory keeps
// interrupted runs from leaving a partially updated package.
[[nodiscard]] ContentInventory stageContent(
    const ContentSourceRoots& roots,
    const std::filesystem::path& outputRoot,
    std::string_view gameVersion);

} // namespace sokoban
