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

private:
    int timelineUseIndex_ = 6;
    int gateSourceUseIndex_ = 6;
    int gateEventIndex_ = 0;
    char newEventId_[64] = "event";
};

} // namespace sokoban
