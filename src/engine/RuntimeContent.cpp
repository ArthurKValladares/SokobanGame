#include "engine/RuntimeContent.hpp"

#include "engine/ContentPipeline.hpp"
#include "engine/LevelProjectStore.hpp"

#include <SDL3/SDL_filesystem.h>

#include <stdexcept>
#include <string>
#include <string_view>

namespace sokoban {

std::filesystem::path runtimeContentRoot(
    const std::filesystem::path& sourceLevelRoot)
{
    const char* basePath = SDL_GetBasePath();
    if (basePath == nullptr || *basePath == '\0') {
        throw std::runtime_error("SDL_GetBasePath failed: executable directory is unavailable");
    }

    const std::string_view utf8(basePath);
    const std::filesystem::path root =
        std::filesystem::path(std::u8string(utf8.begin(), utf8.end())) / "assets";
    if (!sourceLevelRoot.empty()) {
        LevelProjectStore::recover(sourceLevelRoot);
    }
    validateContentPackage(root, SOKOBAN_GAME_VERSION);
    return root;
}

} // namespace sokoban
