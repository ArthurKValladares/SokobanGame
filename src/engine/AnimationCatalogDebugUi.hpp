#pragma once

namespace sokoban {

class AnimationCatalogEditor;
class AnimationPreviewDebugUi;
class AssetManifest;
class VulkanRenderer;

class AnimationCatalogDebugUi {
public:
    // Returns true when the live catalog changed.
    bool draw(
        AnimationCatalogEditor& editor,
        const AssetManifest& manifest,
        AnimationPreviewDebugUi& preview,
        VulkanRenderer& renderer);
};

} // namespace sokoban
