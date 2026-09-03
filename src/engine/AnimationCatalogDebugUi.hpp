#pragma once

// The timeline helpers below name the catalog's own types, so this header
// needs it rather than only forward declarations.
#include "engine/AnimationCatalog.hpp"
#include "engine/render/RenderTypes.hpp"

#include <span>
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
    // What the timeline section works on: the editor and its catalog, the
    // preview and the renderer it drives, and the animation the selected use
    // resolves to. Bundled because all three steps below want most of it.
    struct TimelineContext {
        AnimationCatalogEditor& editor;
        const AssetManifest& manifest;
        AnimationPreviewDebugUi& preview;
        VulkanRenderer& renderer;
        const AnimationCatalog& catalog;
        AnimationUse selectedUse;
        RenderAnimation selectedAnimation;
        bool& changed;
    };

    void openEventEditor(
        const AnimationCatalog::TimelineEvent* event,
        const TimelineContext& timeline);
    void drawTimelineEventList(
        const TimelineContext& timeline,
        std::span<const AnimationUseDefinition> definitions);
    void drawTimelineEventEditor(const TimelineContext& timeline);

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
