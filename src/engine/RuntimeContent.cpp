#include "engine/RuntimeContent.hpp"

#include "engine/ContentPipeline.hpp"
#include "engine/LevelProjectStore.hpp"

#include <SDL3/SDL_filesystem.h>

#include <stdexcept>

namespace sokoban {

std::filesystem::path runtimeContentRoot(
    const std::filesystem::path& sourceLevelRoot)
{
    const char* basePath = SDL_GetBasePath();
    if (basePath == nullptr || *basePath == '\0') {
        throw std::runtime_error("SDL_GetBasePath failed: executable directory is unavailable");
    }

    const std::filesystem::path root = std::filesystem::path(basePath) / "assets";
    if (!sourceLevelRoot.empty()) {
        LevelProjectStore::recover(sourceLevelRoot);
    }
    validateContentPackage(root, SOKOBAN_GAME_VERSION);
    return root;
}

} // namespace sokoban
