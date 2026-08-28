#pragma once

#include "engine/TextureSource.hpp"
#include "engine/render/ImageData.hpp"

#include <filesystem>

namespace sokoban {

[[nodiscard]] ImageData loadRgbaTextureSource(
    const std::filesystem::path& assetRoot,
    const TextureSource& source);

} // namespace sokoban
