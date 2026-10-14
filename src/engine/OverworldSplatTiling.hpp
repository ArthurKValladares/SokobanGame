#pragma once

#include "engine/OverworldMap.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sokoban {

class AssetManifest;

// Supplied definitions may be editor drafts. Planning never reloads or writes
// them, so callers can retain the editor's authority over unsaved documents.
struct OverworldSplatScreen {
    OverworldScreenId id = 0;
    OverworldSlot slot {};
    Level::Definition definition;
};

struct OverworldSplatTilingRequest {
    std::span<const OverworldSplatScreen> screens;
    OverworldScreenId activeScreen = 0;
    std::optional<Level::GroundSplat> selectedSplat;
    uint32_t screenWidth = 0;
    uint32_t screenHeight = 0;
    const AssetManifest& manifest;
    // Optional collision checks for unregistered files. Neither tree is changed.
    std::filesystem::path sourceAssetRoot;
    std::filesystem::path runtimeAssetRoot;
    // Masks referenced by other documents, including puzzle screens. They
    // count as users even when a mask's name appears owned by this screen.
    std::span<const std::string> externalMaskNames {};
};

struct OverworldSplatTilingPlan {
    struct Map {
        OverworldScreenId screen = 0;
        // Relative to the active screen, in the same tile coordinates as picking.
        Vec2 originTiles {};
        std::string textureName;
    };

    struct MaskChange {
        OverworldScreenId screen = 0;
        // Empty for a legacy document with no explicit splat definitions.
        std::optional<Level::GroundSplat> before;
        Level::GroundSplat after;
    };

    struct Copy {
        // Empty only when the legacy fallback is absent; create a blank mask.
        std::string sourceTextureName;
        std::string newTextureName;
        std::string relativePath;
    };

    bool valid = false;
    // The active map is always first. Every mask has its own name and path.
    std::vector<Map> maps;
    std::vector<MaskChange> maskChanges;
    std::vector<Copy> copies;
    std::string status;
};

// Matches ordered base/detail pairs, preferring the selected name and otherwise
// requiring an unambiguous pair. Incompatible or ambiguous neighbors are skipped.
// Shared masks, global fallbacks, and masks not owned by the screen's stable-ID
// prefix are copied before painting, preserving their paint and other users.
// The result describes all changes; this function performs no mutations.
[[nodiscard]] OverworldSplatTilingPlan planOverworldSplatTiling(
    const OverworldSplatTilingRequest& request);

} // namespace sokoban
