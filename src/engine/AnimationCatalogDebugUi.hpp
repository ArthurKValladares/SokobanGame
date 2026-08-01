#pragma once

#include <string>

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
    enum class TimelinePage {
        EventList,
        EventEditor,
    };

    int timelineUseIndex_ = 6;
    int gateSourceUseIndex_ = 6;
    int gateEventIndex_ = 0;
    TimelinePage timelinePage_ = TimelinePage::EventList;
    std::string originalEventId_;
    char eventName_[64] {};
};

} // namespace sokoban
