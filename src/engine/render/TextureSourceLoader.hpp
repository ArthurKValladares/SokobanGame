#pragma once

#include "engine/TextureSource.hpp"
#include "engine/render/CompressedTextureArtifact.hpp"
#include "engine/render/ImageData.hpp"

#include <filesystem>
#include <variant>

namespace sokoban {

[[nodiscard]] ImageData loadRgbaTextureSource(
    const std::filesystem::path& assetRoot,
    const TextureSource& source);

using PreparedTextureSource =
    std::variant<ImageData, CompressedTextureArtifact>;

// Uses a staged native artifact only when the selected device supports BC7.
// A missing artifact deliberately falls back to the original image source so
// editor-authored textures and packages made before A2 remain loadable.
[[nodiscard]] PreparedTextureSource loadPreparedTextureSource(
    const std::filesystem::path& assetRoot,
    const TextureSourceIdentity& identity,
    bool supportsBc7);

} // namespace sokoban
