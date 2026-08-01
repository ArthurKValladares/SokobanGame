#include "engine/AnimationCatalogDebugUi.hpp"

#include "engine/AnimationCatalog.hpp"
#include "engine/AnimationPreviewDebugUi.hpp"
#include "engine/AssetManifest.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#endif

#include <exception>
#include <utility>

namespace sokoban {

void AnimationCatalogDebugUi::initialize(std::filesystem::path filePath)
{
    filePath_ = std::move(filePath);
    status_ = "Editing " + filePath_.string();
}

bool AnimationCatalogDebugUi::draw(
    AnimationCatalog& catalog,
    const AssetManifest& manifest,
    AnimationPreviewDebugUi& preview,
    VulkanRenderer& renderer)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    bool changed = false;
    if (ImGui::Button("Save Catalog")) {
        try {
            catalog.save(filePath_, manifest);
            dirty_ = false;
            status_ = "Saved " + filePath_.string();
        } catch (const std::exception& error) {
            status_ = "Save failed: " + std::string(error.what());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Catalog")) {
        try {
            catalog = AnimationCatalog::loadFromFile(filePath_, manifest);
            dirty_ = false;
            changed = true;
            status_ = "Reloaded " + filePath_.string();
        } catch (const std::exception& error) {
            status_ = "Reload failed: " + std::string(error.what());
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", dirty_ ? "Unsaved changes" : "Saved");
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }

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
                catalog.setGlobalSpeed(animation, speed);
                changed = true;
                dirty_ = true;
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
                        catalog.setUseAnimation(definition.use, candidate);
                        changed = true;
                        dirty_ = true;
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
                catalog.setUseSpeed(definition.use, speed);
                changed = true;
                dirty_ = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled(
                "Effective %.2fx", catalog.effectiveSpeed(definition.use));
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader(
            "Animation Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
        preview.draw(renderer);
    }
    return changed;
#else
    (void)catalog;
    (void)manifest;
    (void)preview;
    (void)renderer;
    return false;
#endif
}

} // namespace sokoban
