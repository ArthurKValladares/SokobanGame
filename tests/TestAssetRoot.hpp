#pragma once

#include <cstdlib>
#include <filesystem>
#include <optional>

[[nodiscard]] inline std::optional<std::filesystem::path>
configuredTestAssetRoot()
{
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "SOKOBAN_ASSETS") != 0 || value == nullptr) {
        return std::nullopt;
    }
    const std::filesystem::path result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv("SOKOBAN_ASSETS");
    return value == nullptr
        ? std::nullopt
        : std::optional<std::filesystem::path>(value);
#endif
}

[[nodiscard]] inline std::filesystem::path testAssetRoot()
{
    return configuredTestAssetRoot().value_or("assets");
}
