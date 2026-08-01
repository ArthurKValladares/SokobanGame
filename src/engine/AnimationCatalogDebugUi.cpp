#include "engine/AnimationCatalogDebugUi.hpp"

#include "engine/AnimationCatalog.hpp"
#include "engine/AnimationCatalogEditor.hpp"
#include "engine/AnimationPreviewDebugUi.hpp"
#include "engine/AssetManifest.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sokoban {
namespace {

RenderModel previewModelForUse(
    AnimationUse use,
    const AssetManifest& manifest)
{
    switch (use) {
    case AnimationUse::EnemyIdle:
    case AnimationUse::EnemyAttack:
    case AnimationUse::EditorEnemyIdle:
    case AnimationUse::ThumbnailEnemyIdle:
        return manifest.enemyModel();
    default:
        return manifest.playerModel();
    }
}

} // namespace

bool AnimationCatalogDebugUi::draw(
    AnimationCatalogEditor& editor,
    const AssetManifest& manifest,
    AnimationPreviewDebugUi& preview,
    VulkanRenderer& renderer)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    bool changed = false;
    if (ImGui::Button("Save Animation Catalog")) {
        (void)editor.save(manifest);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Catalog")) {
        if (editor.reload(manifest)) {
            preview.clearCatalogPreview(renderer);
            changed = true;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", editor.dirty() ? "Unsaved changes" : "Saved");
    if (!editor.status().empty()) {
        ImGui::TextWrapped("%s", editor.status().c_str());
    }

    const AnimationCatalog& catalog = editor.catalog();

    if (ImGui::CollapsingHeader(
            "Global Clip Speeds", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled(
            "Applied everywhere the clip is used. Effective speed is global x use.");
        for (std::size_t i = 0; i < manifest.animations().size(); ++i) {
            const RenderAnimation animation { static_cast<uint32_t>(i + 1) };
            float speed = catalog.globalSpeed(animation);
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::SliderFloat(
                    manifest.animations()[i].name.c_str(),
                    &speed,
                    0.1f,
                    4.0f,
                    "%.2fx")) {
                editor.setGlobalSpeed(animation, speed);
                changed = true;
            }
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader(
            "Animation Uses", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled(
            "Every semantic use declared by code must have exactly one catalog row.");
        for (const AnimationUseDefinition& definition :
             animationUseDefinitions()) {
            ImGui::PushID(static_cast<int>(definition.use));
            const RenderAnimation selected = catalog.animation(definition.use);
            const char* selectedName = manifest.animation(selected).name.c_str();
            ImGui::TextUnformatted(definition.label.data(),
                definition.label.data() + definition.label.size());
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::BeginCombo("Clip", selectedName)) {
                for (std::size_t i = 0; i < manifest.animations().size(); ++i) {
                    const RenderAnimation candidate {
                        static_cast<uint32_t>(i + 1),
                    };
                    const bool isSelected = candidate == selected;
                    if (ImGui::Selectable(
                            manifest.animations()[i].name.c_str(),
                            isSelected)) {
                        editor.setUseAnimation(definition.use, candidate);
                        preview.clearCatalogPreview(renderer);
                        changed = true;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            float speed = catalog.useSpeed(definition.use);
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::SliderFloat(
                    "Use Speed", &speed, 0.1f, 4.0f, "%.2fx")) {
                editor.setUseSpeed(definition.use, speed);
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled(
                "Effective %.2fx", catalog.effectiveSpeed(definition.use));
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    const auto definitions = animationUseDefinitions();
    timelineUseIndex_ = std::clamp(
        timelineUseIndex_, 0, static_cast<int>(definitions.size()) - 1);
    if (ImGui::CollapsingHeader(
            "Timeline Events", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* selectedUseLabel =
            definitions[static_cast<std::size_t>(timelineUseIndex_)]
                .label.data();
        if (ImGui::BeginCombo("Use", selectedUseLabel)) {
            for (int i = 0; i < static_cast<int>(definitions.size()); ++i) {
                const bool selected = i == timelineUseIndex_;
                if (ImGui::Selectable(definitions[i].label.data(), selected)) {
                    timelineUseIndex_ = i;
                    gateEventIndex_ = 0;
                    timelinePage_ = TimelinePage::EventList;
                    originalEventId_.clear();
                    preview.clearCatalogPreview(renderer);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        const AnimationUse selectedUse =
            definitions[static_cast<std::size_t>(timelineUseIndex_)].use;
        const RenderAnimation selectedAnimation =
            catalog.animation(selectedUse);
        const auto openEventEditor =
            [&](const AnimationCatalog::TimelineEvent* event) {
                timelinePage_ = TimelinePage::EventEditor;
                originalEventId_ = event != nullptr ? event->id : "";
                std::snprintf(
                    eventName_,
                    sizeof(eventName_),
                    "%s",
                    event != nullptr ? event->id.c_str() : "event");
                if (preview.previewCatalogAnimation(
                        previewModelForUse(selectedUse, manifest),
                        selectedAnimation,
                        manifest,
                        renderer)) {
                    const float sourceDuration =
                        preview.catalogDurationSeconds();
                    if (std::abs(
                            sourceDuration -
                            catalog.clipDuration(selectedAnimation)) >
                        0.0001f) {
                        editor.setClipDuration(
                            selectedAnimation, sourceDuration);
                        changed = true;
                    }
                    preview.setCatalogNormalizedTime(
                        event != nullptr ? event->normalizedTime : 0.0f);
                }
            };

        if (timelinePage_ == TimelinePage::EventList) {
            const auto selectedEvents = catalog.events(selectedUse);
            if (selectedEvents.empty()) {
                ImGui::TextDisabled("No events attached to this animation.");
            }
            for (std::size_t i = 0; i < selectedEvents.size(); ++i) {
                const AnimationCatalog::TimelineEvent event =
                    selectedEvents[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TextUnformatted(event.id.c_str());
                ImGui::SameLine(260.0f);
                if (ImGui::Button("Edit")) {
                    openEventEditor(&event);
                }
                ImGui::PopID();
            }
            if (ImGui::Button("Add New Event")) {
                openEventEditor(nullptr);
            }

            ImGui::SeparatorText("Start Gate");
            const auto& currentGate = catalog.startGate(selectedUse);
            if (currentGate) {
                ImGui::Text(
                    "Waiting for %s / %s",
                    animationUseId(currentGate->sourceUse).data(),
                    currentGate->eventId.c_str());
            } else {
                ImGui::TextDisabled("Starts immediately");
            }

            gateSourceUseIndex_ = std::clamp(
                gateSourceUseIndex_,
                0,
                static_cast<int>(definitions.size()) - 1);
            if (ImGui::BeginCombo(
                    "Source Use",
                    definitions[
                        static_cast<std::size_t>(gateSourceUseIndex_)]
                        .label.data())) {
                for (int i = 0; i < static_cast<int>(definitions.size()); ++i) {
                    if (catalog.events(definitions[i].use).empty()) {
                        continue;
                    }
                    const bool selected = i == gateSourceUseIndex_;
                    if (ImGui::Selectable(
                            definitions[i].label.data(), selected)) {
                        gateSourceUseIndex_ = i;
                        gateEventIndex_ = 0;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            const AnimationUse gateSourceUse =
                definitions[static_cast<std::size_t>(gateSourceUseIndex_)]
                    .use;
            const auto gateEvents = catalog.events(gateSourceUse);
            gateEventIndex_ = gateEvents.empty()
                ? 0
                : std::clamp(
                      gateEventIndex_,
                      0,
                      static_cast<int>(gateEvents.size()) - 1);
            const char* gateEventLabel = gateEvents.empty()
                ? "No events"
                : gateEvents[static_cast<std::size_t>(gateEventIndex_)]
                      .id.c_str();
            if (ImGui::BeginCombo("Source Event", gateEventLabel)) {
                for (int i = 0; i < static_cast<int>(gateEvents.size()); ++i) {
                    const bool selected = i == gateEventIndex_;
                    if (ImGui::Selectable(
                            gateEvents[i].id.c_str(), selected)) {
                        gateEventIndex_ = i;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::BeginDisabled(gateEvents.empty());
            if (ImGui::Button("Set Start Gate")) {
                editor.setStartGate(selectedUse, AnimationCatalog::EventGate {
                    .sourceUse = gateSourceUse,
                    .eventId = gateEvents[
                        static_cast<std::size_t>(gateEventIndex_)].id,
                });
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!currentGate.has_value());
            if (ImGui::Button("Clear Start Gate")) {
                editor.setStartGate(selectedUse, std::nullopt);
                changed = true;
            }
            ImGui::EndDisabled();
        } else {
            if (ImGui::Button("Back to Events")) {
                timelinePage_ = TimelinePage::EventList;
                originalEventId_.clear();
                preview.clearCatalogPreview(renderer);
            }
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%.3fs source clip",
                catalog.clipDuration(selectedAnimation));

            preview.drawCatalogPreview(catalog.events(selectedUse));

            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputText(
                "Event Name", eventName_, sizeof(eventName_));
            const auto selectedEvents = catalog.events(selectedUse);
            const bool duplicateName = std::ranges::any_of(
                selectedEvents,
                [&](const AnimationCatalog::TimelineEvent& event) {
                    return event.id == eventName_ &&
                        event.id != originalEventId_;
                });
            ImGui::SameLine();
            ImGui::BeginDisabled(
                eventName_[0] == '\0' || duplicateName ||
                preview.catalogDurationSeconds() <= 0.0f);
            if (ImGui::Button("Add at Cursor")) {
                if (editor.commitTimelineEvent(
                        selectedUse,
                        originalEventId_,
                        eventName_,
                        preview.catalogNormalizedTime())) {
                    changed = true;
                    timelinePage_ = TimelinePage::EventList;
                    originalEventId_.clear();
                    preview.clearCatalogPreview(renderer);
                }
            }
            ImGui::EndDisabled();
            if (duplicateName) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
                    "An event with this name already exists.");
            }

            if (!originalEventId_.empty()) {
                if (ImGui::Button("Delete Event")) {
                    editor.removeTimelineEvent(
                        selectedUse, originalEventId_);
                    changed = true;
                    timelinePage_ = TimelinePage::EventList;
                    originalEventId_.clear();
                    preview.clearCatalogPreview(renderer);
                }
            }
        }
    }

    if (ImGui::CollapsingHeader(
            "Animation Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
        preview.draw(renderer, manifest);
    }
    return changed;
#else
    (void)editor;
    (void)manifest;
    (void)preview;
    (void)renderer;
    return false;
#endif
}

} // namespace sokoban
