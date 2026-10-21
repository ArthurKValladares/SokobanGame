#pragma once

#include "engine/render/VulkanRenderConstants.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace sokoban {

// The Glass asset is a rounded unit box. Its transparent material is absent
// from opaque scene depth, so publish its actual moving/scaled waterline.
inline void writeWaterContacts(SceneFrameUniform& uniform,
    std::span<const RenderFrameData::Tile> tiles,
    std::span<const RenderFrameData::WaterSurface> surfaces)
{
    uniform.waterContacts.fill({});
    uniform.waterContactMeta = {};
    std::size_t count = 0;
    for (const auto& tile : tiles) {
        if (!tile.blurBehind || tile.pickOnly || tile.isEditorPreview || tile.modelTransform ||
            !std::isfinite(tile.position.x) || !std::isfinite(tile.position.y) ||
            !std::isfinite(tile.baseElevation) || !std::isfinite(tile.height) ||
            !std::isfinite(tile.size.x) || !std::isfinite(tile.size.y) ||
            !std::isfinite(tile.modelRotationOffsetRadians) ||
            tile.height <= 0.0f || tile.size.x <= 0.0f || tile.size.y <= 0.0f) continue;
        const float centerX = tile.position.x + tile.size.x * 0.5f;
        const float centerY = tile.position.y + tile.size.y * 0.5f;
        const float halfWidth = tile.size.x * 0.5f;
        const float halfDepth = tile.size.y * 0.5f;
        const float halfHeight = tile.height * 0.5f;
        const float radius = std::min(0.05f * std::min(tile.size.x, tile.size.y), halfHeight);
        const float angle = tile.modelRotationOffsetRadians;
        const float cosine = std::abs(std::cos(angle));
        const float sine = std::abs(std::sin(angle));
        for (const auto& water : surfaces) {
            if (water.isEditorPreview || !std::isfinite(water.elevation) ||
                !std::isfinite(water.position.x) || !std::isfinite(water.position.y) ||
                !std::isfinite(water.size.x) || !std::isfinite(water.size.y) ||
                water.size.x <= 0.0f || water.size.y <= 0.0f ||
                water.elevation <= tile.baseElevation ||
                water.elevation >= tile.baseElevation + tile.height) continue;
            const float verticalInset = std::max(std::abs(water.elevation -
                (tile.baseElevation + halfHeight)) - (halfHeight - radius), 0.0f);
            const float sectionRadius = std::sqrt(std::max(
                radius * radius - verticalInset * verticalInset, 0.0f));
            const float sectionWidth = halfWidth - radius + sectionRadius;
            const float sectionDepth = halfDepth - radius + sectionRadius;
            const float extentX = cosine * sectionWidth + sine * sectionDepth;
            const float extentY = sine * sectionWidth + cosine * sectionDepth;
            if (centerX + extentX < water.position.x ||
                centerX - extentX > water.position.x + water.size.x ||
                centerY + extentY < water.position.y ||
                centerY - extentY > water.position.y + water.size.y) continue;
            const auto contacts = std::span(uniform.waterContacts.data(), count);
            if (std::ranges::any_of(contacts, [&](const auto& contact) {
                    return contact.centerAndElevation.x == centerX &&
                        contact.centerAndElevation.y == centerY &&
                        contact.centerAndElevation.z == water.elevation;
                })) continue;
            if (count == uniform.waterContacts.size()) break;
            uniform.waterContacts[count++] = {
                .centerAndElevation = { centerX, centerY, water.elevation, 0.0f },
                .halfExtentAndRadius = { sectionWidth, sectionDepth, sectionRadius, angle },
            };
        }
        if (count == uniform.waterContacts.size()) break;
    }
    uniform.waterContactMeta.x = static_cast<float>(count);
}

} // namespace sokoban
