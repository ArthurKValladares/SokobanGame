#pragma once

#include <filesystem>
#include <string>

namespace sokoban {

// Keep native paths for file I/O. Diagnostics and text parsers use UTF-8.
[[nodiscard]] inline std::string pathText(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return { utf8.begin(), utf8.end() };
}

} // namespace sokoban
