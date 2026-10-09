#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/TileTypes.hpp"

#include <optional>
#include <string_view>

namespace sokoban {

struct ControlModelParts {
    RenderModel base;
    RenderModel moving;
};

// Combined models remain the editor/thumbnail pose and the fallback for
// manifests without a complete pair of rigid gameplay components.
[[nodiscard]] inline std::optional<ControlModelParts> controlModelParts(
    const AssetManifest& manifest, TileType tile)
{
    std::string_view baseName;
    std::string_view movingName;
    if (tileTypeIsLever(tile)) {
        baseName = "LeverBase";
        movingName = "LeverHandle";
    } else if (tileTypeIsButton(tile)) {
        baseName = "PulseButtonBase";
        movingName = "PulseButtonCap";
    } else if (tile == TileType::PressurePlate) {
        baseName = "PressurePlateBase";
        movingName = "PressurePlatePad";
    } else {
        return std::nullopt;
    }
    const auto base = manifest.findModelIdByName(baseName);
    const auto moving = manifest.findModelIdByName(movingName);
    if (!base || !moving) {
        return std::nullopt;
    }
    return ControlModelParts { *base, *moving };
}

} // namespace sokoban
