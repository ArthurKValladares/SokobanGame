#include "engine/OverworldSplatTiling.hpp"

#include "engine/AssetManifest.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace sokoban {
namespace {

std::string pathIdentity(const std::string& path)
{
    std::string portable = path;
    std::replace(portable.begin(), portable.end(), '\\', '/');
    std::string identity =
        std::filesystem::path(portable).lexically_normal().generic_string();
#ifdef _WIN32
    std::ranges::transform(identity, identity.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
#endif
    return identity;
}

const AssetManifest::Texture* textureByName(
    const AssetManifest& manifest, const std::string& name)
{
    const RenderTexture texture = manifest.findTextureIdByName(name);
    return texture.isNone() ? nullptr : &manifest.textures()[texture.index()];
}

std::string legacyMaskName(
    const AssetManifest& manifest, OverworldScreenId screen)
{
    std::string name = groundSplatMapTextureNameForOverworldScreen(screen);
    if (textureByName(manifest, name)) return name;
    return textureByName(manifest, std::string(groundSplatMapTextureName))
        ? std::string(groundSplatMapTextureName) : std::string {};
}

bool sameMaterials(
    const Level::GroundSplat& left, const Level::GroundSplat& right)
{
    return left.base == right.base && left.detail == right.detail;
}

bool sameSelection(
    const Level::GroundSplat& left, const Level::GroundSplat& right)
{
    return left.name == right.name && sameMaterials(left, right) &&
        left.mask == right.mask;
}

bool screenOwnsMask(OverworldScreenId screen, const std::string& mask)
{
    const std::string prefix = groundSplatMapTextureNameForOverworldScreen(screen);
    return mask == prefix || mask.starts_with(prefix + "_");
}

} // namespace

OverworldSplatTilingPlan planOverworldSplatTiling(
    const OverworldSplatTilingRequest& request)
{
    OverworldSplatTilingPlan result;
    const auto fail = [&](std::string status) {
        OverworldSplatTilingPlan failure;
        failure.status = std::move(status);
        return failure;
    };
    if (request.screenWidth == 0 || request.screenHeight == 0) {
        return fail("This overworld has no board to paint.");
    }
    const auto active = std::ranges::find(
        request.screens, request.activeScreen, &OverworldSplatScreen::id);
    if (active == request.screens.end()) {
        return fail("The edited screen is absent from the overworld layout.");
    }

    std::set<OverworldScreenId> screenIds;
    std::set<std::pair<int, int>> slots;
    for (const auto& screen : request.screens) {
        if (!screenIds.insert(screen.id).second ||
            !slots.emplace(screen.slot.x, screen.slot.y).second) {
            return fail("Overworld paint needs distinct screen IDs and slots.");
        }
    }

    Level::GroundSplat selected;
    if (request.selectedSplat) {
        const auto found = std::ranges::find_if(active->definition.groundSplats,
            [&](const Level::GroundSplat& splat) {
                return sameSelection(splat, *request.selectedSplat);
            });
        if (found == active->definition.groundSplats.end()) {
            return fail("The selected splat map no longer belongs to this screen.");
        }
        selected = *found;
    } else if (!active->definition.groundSplats.empty()) {
        selected = active->definition.groundSplats.front();
    } else {
        selected = {
            .name = "Ground",
            .base = std::string(groundSplatBaseTextureName),
            .detail = std::string(groundSplatDetailTextureName),
            .mask = legacyMaskName(request.manifest, active->id),
        };
    }
    if (selected.base.empty() || selected.detail.empty() ||
        (!active->definition.groundSplats.empty() &&
            !textureByName(request.manifest, selected.mask))) {
        return fail("The selected splat map has no registered blend mask.");
    }

    // Count all uses, including definitions with a different material pair.
    // A second manifest name for the same source file is still the same mask.
    std::map<std::string, std::size_t> nameUses;
    std::map<std::string, std::size_t> pathUses;
    const auto countUse = [&](const std::string& name) {
        if (name.empty()) return;
        ++nameUses[name];
        if (const auto* texture = textureByName(request.manifest, name)) {
            ++pathUses[pathIdentity(texture->path)];
        }
    };
    for (const auto& screen : request.screens) {
        if (screen.definition.groundSplats.empty()) {
            countUse(legacyMaskName(request.manifest, screen.id));
        } else {
            for (const auto& splat : screen.definition.groundSplats) {
                countUse(splat.mask);
            }
        }
    }
    for (const auto& name : request.externalMaskNames) {
        countUse(name);
    }

    std::set<std::string> allocatedNames;
    std::set<std::string> allocatedPaths;
    for (const auto& texture : request.manifest.textures()) {
        allocatedNames.insert(texture.name);
        allocatedPaths.insert(pathIdentity(texture.path));
    }
    const auto allocateCopy = [&](OverworldScreenId screen,
                                  const std::string& source) {
        const std::string prefix = groundSplatMapTextureNameForOverworldScreen(screen);
        for (uint64_t suffix = 0; ; ++suffix) {
            const std::string name = suffix == 0 ? prefix
                : prefix + "_Tiled_" + std::to_string(suffix);
            const std::string path = suffix == 0
                ? groundSplatMapAssetPathForOverworldScreen(screen)
                : "custom/textures/" + name + ".png";
            if (allocatedNames.contains(name) ||
                allocatedPaths.contains(pathIdentity(path)) ||
                (!request.sourceAssetRoot.empty() &&
                    std::filesystem::exists(request.sourceAssetRoot / path)) ||
                (!request.runtimeAssetRoot.empty() &&
                    std::filesystem::exists(request.runtimeAssetRoot / path))) {
                if (suffix == std::numeric_limits<uint64_t>::max()) {
                    throw std::runtime_error("No unused overworld mask name remains.");
                }
                continue;
            }
            allocatedNames.insert(name);
            allocatedPaths.insert(pathIdentity(path));
            result.copies.push_back({ source, name, path });
            return name;
        }
    };

    const auto* fallback = textureByName(
        request.manifest, std::string(groundSplatMapTextureName));
    const std::string fallbackPath = fallback ? pathIdentity(fallback->path) : std::string {};
    std::size_t skipped = 0;
    const auto appendMap = [&](const OverworldSplatScreen& screen, bool isActive) {
        std::optional<Level::GroundSplat> before;
        Level::GroundSplat after;
        if (screen.definition.groundSplats.empty()) {
            if (selected.base != groundSplatBaseTextureName ||
                selected.detail != groundSplatDetailTextureName) {
                ++skipped;
                return;
            }
            after = selected;
            after.mask = legacyMaskName(request.manifest, screen.id);
        } else if (isActive) {
            before = selected;
            after = selected;
        } else {
            const auto& splats = screen.definition.groundSplats;
            const auto named = std::ranges::find_if(splats,
                [&](const Level::GroundSplat& splat) {
                    return splat.name == selected.name && sameMaterials(splat, selected);
                });
            if (named != splats.end()) {
                before = *named;
            } else {
                for (const auto& splat : splats) {
                    if (!sameMaterials(splat, selected)) continue;
                    if (before) {
                        ++skipped;
                        return;
                    }
                    before = splat;
                }
            }
            if (!before || !textureByName(request.manifest, before->mask)) {
                ++skipped;
                return;
            }
            after = *before;
        }

        const auto* texture = textureByName(request.manifest, after.mask);
        const std::string path = texture ? pathIdentity(texture->path) : std::string {};
        if (!texture || !screenOwnsMask(screen.id, after.mask) ||
            nameUses[after.mask] > 1 || pathUses[path] > 1 ||
            (!fallbackPath.empty() && path == fallbackPath)) {
            after.mask = allocateCopy(screen.id, after.mask);
        }
        if (!before || *before != after) {
            result.maskChanges.push_back({ screen.id, before, after });
        }
        const int64_t deltaX = static_cast<int64_t>(screen.slot.x) - active->slot.x;
        const int64_t deltaY = static_cast<int64_t>(screen.slot.y) - active->slot.y;
        result.maps.push_back({
            .screen = screen.id,
            .originTiles = {
                static_cast<float>(deltaX * request.screenWidth),
                static_cast<float>(deltaY * request.screenHeight),
            },
            .textureName = after.mask,
        });
    };

    try {
        appendMap(*active, true);
        for (const auto& screen : request.screens) {
            if (screen.id != active->id) appendMap(screen, false);
        }
    } catch (const std::exception& error) {
        return fail("Could not prepare tiled splat maps: " + std::string(error.what()));
    }
    result.valid = true;
    result.status = "Painting " + std::to_string(result.maps.size()) +
        " tiled overworld splat map(s).";
    if (skipped != 0) {
        result.status += " Skipped " + std::to_string(skipped) +
            " screen(s) with incompatible or ambiguous splat maps.";
    }
    return result;
}

} // namespace sokoban
