#include "engine/ApplicationDebugUi.hpp"

#include "engine/AudioSystem.hpp"

#include "engine/Config.hpp"
#include "engine/Rules.hpp"
#include "engine/TaskSystem.hpp"
#include "engine/TileTypes.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>

namespace sokoban {
namespace {

#if SOKOBAN_ENABLE_DEBUG_UI
void drawSunDirectionPreview(Vec3 direction, float tiltDegrees)
{
    constexpr ImVec2 previewSize { 240.0f, 116.0f };
    ImGui::InvisibleButton("sun_direction_preview", previewSize);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 background = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 sun =
        ImGui::GetColorU32(ImVec4 { 1.0f, 0.88f, 0.25f, 1.0f });

    drawList->AddRectFilled(min, max, background, 4.0f);
    drawList->AddRect(min, max, border, 4.0f);

    const float radius = 34.0f;
    const ImVec2 topCenter { min.x + 62.0f, min.y + 66.0f };
    const ImVec2 sideCenter { min.x + 178.0f, min.y + 66.0f };
    drawList->AddText(
        ImVec2 { topCenter.x - 16.0f, min.y + 10.0f }, text, "XY");
    drawList->AddCircle(topCenter, radius, border, 48, 1.5f);
    drawList->AddLine(
        ImVec2 { topCenter.x - radius, topCenter.y },
        ImVec2 { topCenter.x + radius, topCenter.y },
        muted);
    drawList->AddLine(
        ImVec2 { topCenter.x, topCenter.y - radius },
        ImVec2 { topCenter.x, topCenter.y + radius },
        muted);
    const ImVec2 topEnd {
        topCenter.x + direction.x * radius,
        topCenter.y + direction.y * radius,
    };
    drawList->AddLine(topCenter, topEnd, sun, 2.0f);
    drawList->AddCircleFilled(topEnd, 4.0f, sun);

    drawList->AddText(
        ImVec2 { sideCenter.x - 16.0f, min.y + 10.0f }, text, "Side");
    drawList->AddCircle(sideCenter, radius, border, 48, 1.5f);
    drawList->AddLine(
        ImVec2 { sideCenter.x - radius, sideCenter.y },
        ImVec2 { sideCenter.x + radius, sideCenter.y },
        muted);
    drawList->AddLine(
        ImVec2 { sideCenter.x, sideCenter.y - radius },
        ImVec2 { sideCenter.x, sideCenter.y + radius },
        muted);

    constexpr float pi = 3.14159265358979323846f;
    const float signedHorizontalLength =
        std::sin(tiltDegrees * pi / 180.0f);
    const ImVec2 sideEnd {
        sideCenter.x + signedHorizontalLength * radius,
        sideCenter.y - direction.z * radius,
    };
    drawList->AddLine(sideCenter, sideEnd, sun, 2.0f);
    drawList->AddCircleFilled(sideEnd, 4.0f, sun);
}
#endif

} // namespace

void ApplicationDebugUi::draw(const Context& context) const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const GameState& state = context.gameplaySession.state();
    PresentationSettings& settings = context.settings;
    ImGui::Text(
        "Level %d Screen %d", context.currentLevel, context.currentScreen);
    ImGui::Text(
        "Player (%d, %d, %d)",
        state.player.x,
        state.player.y,
        state.player.z);
    ImGui::Text("Player %s", state.playerDead ? "dead" : "alive");
    ImGui::Text("Movables %zu", state.movables.size());
    ImGui::Text("History %zu", context.gameplaySession.historySize());
    ImGui::Text(
        "End %s",
        rules::isEndUnlocked(context.level, state) ? "unlocked" : "locked");
    ImGui::Text(
        "Task workers %u, tasks run %llu",
        taskSystem().workerCount(),
        static_cast<unsigned long long>(taskSystem().executedTaskCount()));
    ImGui::Separator();

    constexpr const char* antiAliasingLabels[] {
        "None",
        "MSAA 2x",
        "MSAA 4x",
        "MSAA 8x",
    };
    int antiAliasingIndex =
        static_cast<int>(context.renderer.antiAliasingMode());
    if (ImGui::Combo(
            "Anti-aliasing",
            &antiAliasingIndex,
            antiAliasingLabels,
            static_cast<int>(std::size(antiAliasingLabels)))) {
        context.renderer.setAntiAliasingMode(
            static_cast<AntiAliasingMode>(antiAliasingIndex));
    }
    bool wireframeEnabled = context.renderer.wireframeEnabled();
    if (ImGui::Checkbox("Wireframe", &wireframeEnabled)) {
        context.renderer.setWireframeEnabled(wireframeEnabled);
    }
    ImGui::SameLine();
    float wireframeLineWidth = context.renderer.wireframeLineWidth();
    const auto lineWidthRange = context.renderer.wireframeLineWidthRange();
    ImGui::BeginDisabled(!context.renderer.wideLinesSupported());
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::SliderFloat(
            "Line Width",
            &wireframeLineWidth,
            lineWidthRange[0],
            lineWidthRange[1],
            "%.1f")) {
        context.renderer.setWireframeLineWidth(wireframeLineWidth);
    }
    ImGui::EndDisabled();

    if (ImGui::CollapsingHeader("Tile Grid")) {
        float gridColor[3] {
            settings.grid.color.x,
            settings.grid.color.y,
            settings.grid.color.z,
        };
        if (ImGui::ColorEdit3("Grid Color", gridColor)) {
            settings.grid.color.x = gridColor[0];
            settings.grid.color.y = gridColor[1];
            settings.grid.color.z = gridColor[2];
        }
        ImGui::DragFloat(
            "Grid Alpha",
            &settings.grid.color.w,
            0.01f,
            0.0f,
            1.0f,
            "%.2f");
        ImGui::DragFloat(
            "Grid Width",
            &settings.grid.lineWidth,
            0.05f,
            0.0f,
            12.0f,
            "%.2f px");
    }

    if (ImGui::CollapsingHeader("Tile Geometry")) {
        float stepDurationSeconds =
            context.gameplaySession.stepDurationSeconds();
        ImGui::DragFloat(
            "Step Duration",
            &stepDurationSeconds,
            0.005f,
            0.05f,
            1.0f,
            "%.3f s");
        context.gameplaySession.setStepDurationSeconds(
            std::clamp(stepDurationSeconds, 0.05f, 1.0f));

        if (ImGui::TreeNode("Step Rates (tiles/step)")) {
            rules::StepRates stepRates =
                context.gameplaySession.stepRates();
            ImGui::SliderInt("Player", &stepRates.playerMove, 0, 5);
            ImGui::SliderInt("Slide", &stepRates.slide, 0, 5);
            ImGui::SliderInt("Conveyor", &stepRates.conveyor, 0, 5);
            context.gameplaySession.setStepRates(stepRates);
            ImGui::TextDisabled("Movement rates by source; default 1.");
            ImGui::TreePop();
        }

        ImGui::DragFloat(
            "Surface Entity Height",
            &settings.geometry.surfaceEntityHeight,
            0.005f,
            0.01f,
            0.5f,
            "%.3f");
        ImGui::DragFloat(
            "Surface Entity Width / Depth",
            &settings.geometry.surfaceEntityWidthDepth,
            0.01f,
            0.1f,
            1.0f,
            "%.2f");
        ImGui::TextDisabled("End and pressure plate geometry");

        if (ImGui::TreeNode("Tile Scale")) {
            for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
                if (definition.type == TileType::Air) {
                    continue;
                }
                float scale = settings.tileScale(definition.type);
                if (ImGui::DragFloat(
                        definition.name.data(),
                        &scale,
                        0.01f,
                        config::minTileScale,
                        config::maxTileScale,
                        "%.2f")) {
                    settings.setTileScale(definition.type, scale);
                }
            }
            ImGui::TextDisabled(
                "Visual scale around each tile's bottom-center.");
            ImGui::TreePop();
        }
    }

    if (ImGui::CollapsingHeader("Audio")) {
        float volume = context.audio.masterVolume();
        if (ImGui::SliderFloat("Master Volume", &volume, 0.0f, 1.0f, "%.2f")) {
            context.audio.setMasterVolume(volume);
        }
        float footstepInterval = context.audio.footstepIntervalSeconds();
        if (ImGui::DragFloat("Footstep Interval", &footstepInterval, 0.005f, 0.05f, 1.0f, "%.3f s")) {
            context.audio.setFootstepIntervalSeconds(footstepInterval);
        }
        ImGui::TextDisabled("Footsteps play while the player walks or pushes.");
        if (!context.audio.available()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Audio unavailable (no device or missing files).");
        }
    }

    if (ImGui::CollapsingHeader("Lighting")) {
        auto& lighting = settings.lighting;
        ImGui::DragFloat(
            "Sun Azimuth",
            &lighting.sunAzimuthDegrees,
            0.5f,
            -180.0f,
            180.0f,
            "%.1f deg");
        ImGui::DragFloat(
            "Sun Tilt",
            &lighting.sunTiltDegrees,
            0.5f,
            -90.0f,
            90.0f,
            "%.1f deg");

        const Vec3 sunDirection = settings.sunDirection();
        ImGui::Text(
            "Unit Vector %.2f, %.2f, %.2f",
            sunDirection.x,
            sunDirection.y,
            sunDirection.z);
        drawSunDirectionPreview(sunDirection, lighting.sunTiltDegrees);

        float sunColor[3] {
            lighting.sunColor.x,
            lighting.sunColor.y,
            lighting.sunColor.z,
        };
        if (ImGui::ColorEdit3("Sun Color", sunColor)) {
            lighting.sunColor = {
                sunColor[0],
                sunColor[1],
                sunColor[2],
            };
        }
        ImGui::DragFloat(
            "Sun Intensity",
            &lighting.sunIntensity,
            0.02f,
            0.0f,
            4.0f,
            "%.2f");

        float ambientColor[3] {
            lighting.ambientColor.x,
            lighting.ambientColor.y,
            lighting.ambientColor.z,
        };
        if (ImGui::ColorEdit3("Ambient Color", ambientColor)) {
            lighting.ambientColor = {
                ambientColor[0],
                ambientColor[1],
                ambientColor[2],
            };
        }
        ImGui::DragFloat(
            "Ambient Intensity",
            &lighting.ambientIntensity,
            0.01f,
            0.0f,
            2.0f,
            "%.2f");
        ImGui::DragFloat(
            "Specular Strength",
            &lighting.specularStrength,
            0.01f,
            0.0f,
            1.0f,
            "%.2f");
        ImGui::DragFloat(
            "Specular Power",
            &lighting.specularPower,
            0.5f,
            1.0f,
            128.0f,
            "%.1f");
        ImGui::DragFloat(
            "Model Shadow Receive",
            &lighting.modelShadowReceive,
            0.01f,
            0.0f,
            1.0f,
            "%.2f");
        ImGui::TextDisabled(
            "Lower model shadow receive reduces harsh self-shadowing.");

        ImGui::Checkbox(
            "Ambient Occlusion",
            &lighting.ambientOcclusionEnabled);
        ImGui::BeginDisabled(!lighting.ambientOcclusionEnabled);
        ImGui::DragFloat(
            "AO Strength",
            &lighting.ambientOcclusionStrength,
            0.01f,
            0.0f,
            1.0f,
            "%.2f");
        ImGui::Checkbox(
            "Visualize SSAO",
            &lighting.ambientOcclusionVisualize);
        ImGui::EndDisabled();

        ImGui::Checkbox("Shadows", &lighting.shadowsEnabled);
        ImGui::BeginDisabled(!lighting.shadowsEnabled);
        ImGui::DragFloat(
            "Shadow Opacity",
            &lighting.shadowOpacity,
            0.01f,
            0.0f,
            0.85f,
            "%.2f");
        ImGui::DragFloat(
            "Shadow Bias",
            &lighting.shadowBias,
            0.0005f,
            0.0f,
            0.05f,
            "%.4f");
        ImGui::EndDisabled();
    }
    settings.normalize();

    if (ImGui::CollapsingHeader("Rendering Stats")) {
        const RenderStats renderStats = context.renderer.renderStats();
        const VulkanModelResources::LoadingStats assetStats =
            context.renderer.assetLoadingStats();
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text(
            "Frame %.3f ms (%.1f FPS)",
            io.DeltaTime * 1000.0f,
            io.Framerate);
        ImGui::Text(
            "Recorded frame %llu",
            static_cast<unsigned long long>(renderStats.frameIndex));
        ImGui::Text(
            "Swapchain %u x %u, %u images",
            renderStats.swapchainWidth,
            renderStats.swapchainHeight,
            renderStats.swapchainImages);
        ImGui::Text("Active samples %ux", renderStats.activeSamples);
        ImGui::Text(
            "Wireframe %s",
            renderStats.wireframeEnabled ? "on" : "off");
        ImGui::Text(
            "Wireframe line width %.1f",
            renderStats.wireframeLineWidth);
        ImGui::Text("Render tiles %u", renderStats.totalTiles);
        ImGui::Text("Visible faces %u", renderStats.visibleFaces);
        ImGui::Text("Draw calls %u", renderStats.drawCalls);
        ImGui::Text("Triangles %u", renderStats.triangles);
        ImGui::Text("Vertices %u", renderStats.vertices);
        ImGui::Text("Pipelines bound %u", renderStats.pipelineBinds);
        ImGui::Text("Render passes %u", renderStats.renderPasses);
        ImGui::Text("Image barriers %u", renderStats.imageBarriers);
        ImGui::Text(
            "Models %u/%u loaded, %u pending",
            assetStats.loadedModels,
            assetStats.totalModels,
            assetStats.pendingModels);
        ImGui::Text(
            "Textures %u/%u loaded, %u pending",
            assetStats.loadedTextures,
            assetStats.totalTextures,
            assetStats.pendingTextures);
        ImGui::Text(
            "Animations %u/%u loaded, %u pending",
            assetStats.loadedAnimations,
            assetStats.totalAnimations,
            assetStats.pendingAnimations);
        if (assetStats.failedAssets > 0) {
            ImGui::Text("Asset load failures %u", assetStats.failedAssets);
        }
        ImGui::Text(
            "Pipeline rebuilds %llu",
            static_cast<unsigned long long>(renderStats.pipelineRebuilds));
        ImGui::Text(
            "Swapchain recreations %llu",
            static_cast<unsigned long long>(
                renderStats.swapchainRecreations));
    }
#else
    (void)context;
#endif
}

} // namespace sokoban
