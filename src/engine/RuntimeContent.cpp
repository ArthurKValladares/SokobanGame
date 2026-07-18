#include "engine/RuntimeContent.hpp"

#include <SDL3/SDL_filesystem.h>

#include <fstream>
#include <stdexcept>
#include <string>

namespace sokoban {

std::filesystem::path runtimeContentRoot()
{
    const char* basePath = SDL_GetBasePath();
    if (basePath == nullptr || *basePath == '\0') {
        throw std::runtime_error("SDL_GetBasePath failed: executable directory is unavailable");
    }

    const std::filesystem::path root = std::filesystem::path(basePath) / "assets";
    const std::filesystem::path indexPath = root / "content.index";
    if (!std::filesystem::is_regular_file(indexPath)) {
        throw std::runtime_error(
            "runtime content is missing or was not staged: " + indexPath.string());
    }
    if (!std::filesystem::is_regular_file(root / "manifest.json")) {
        throw std::runtime_error("runtime asset manifest is missing: " + (root / "manifest.json").string());
    }

    std::ifstream index(indexPath, std::ios::binary);
    std::string formatLine;
    std::string versionLine;
    if (!std::getline(index, formatLine) || formatLine != "format 1") {
        throw std::runtime_error("unsupported or corrupt runtime content index: " + indexPath.string());
    }
    if (!std::getline(index, versionLine) ||
        versionLine != std::string("game-version ") + SOKOBAN_GAME_VERSION) {
        throw std::runtime_error(
            "runtime content version does not match game version "
            SOKOBAN_GAME_VERSION ": " + indexPath.string());
    }
    return root;
}

} // namespace sokoban
