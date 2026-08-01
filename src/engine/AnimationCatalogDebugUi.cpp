#include "engine/AnimationCatalogDebugUi.hpp"

#include "engine/AnimationCatalog.hpp"
#include "engine/AnimationCatalogEditor.hpp"
#include "engine/AnimationPreviewDebugUi.hpp"
#include "engine/AssetManifest.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#endif

namespace sokoban {

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
