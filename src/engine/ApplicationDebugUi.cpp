#include "engine/ApplicationDebugUi.hpp"

#include "engine/AudioConfig.hpp"
#include "engine/AudioSystem.hpp"

#include "engine/GameplayConfig.hpp"
#include "engine/Log.hpp"
#include "engine/Rules.hpp"
#include "engine/TaskSystem.hpp"
#include "engine/render/LightingConfig.hpp"
#include "engine/render/SceneConfig.hpp"
#include "engine/render/WaterConfig.hpp"

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

ApplicationDebugUi::Result ApplicationDebugUi::draw(
    const Context& context) const
{
    Result result;
#if SOKOBAN_ENABLE_DEBUG_UI
    const GameState& state = context.gameplaySession.state();
    const GameState::Player& primaryPlayer = state.players.front();
    PresentationSettings& settings = context.settings;
    if (context.inOverworld) {
        ImGui::Text("World: Overworld");
        ImGui::Text(
            "Selector targets: %d / %d solved",
            context.completedSelectorTargets,
            context.selectorTargetCount);
    } else {
        ImGui::Text(
            "World: Puzzle %d:%d",
            context.currentLevel,
            context.currentScreen);
    }
    ImGui::Text(
        "Player (%d, %d, %d)",
        primaryPlayer.cell.x,
        primaryPlayer.cell.y,
        primaryPlayer.cell.z);
    ImGui::Text("Player %s", primaryPlayer.dead ? "dead" : "alive");
    ImGui::Text("Player instances: %zu", state.players.size());
    ImGui::Text("Movables %zu", state.movables.size());
    ImGui::Text("Enemies %zu", state.enemies.size());
    ImGui::Text("History %zu", context.gameplaySession.historySize());

    // Show which admission rule is preventing concurrent actions.
    const ActionScheduler::AdmissionStats& admissions =
        context.gameplaySession.admissionStats();
    ImGui::Text(
        "Actions in flight %zu, admitted %zu",
        context.gameplaySession.inFlight().size(),
        admissions.admitted);
    ImGui::Text(
        "Refused: %zu by ownership, %zu by claim",
        admissions.refusedByOwnership,
        admissions.refusedByReservation);
    if (admissions.refusedByReservation == 0 &&
        admissions.refusedByOwnership + admissions.admitted != 0) {
        ImGui::TextDisabled(
            "No claim refusals yet this screen - ownership is carrying them.");
    }

    ImGui::Text(
        "Input %s, gamepads %zu%s%s",
        context.input.activeDevice() == ActiveInputDevice::Gamepad
            ? "gamepad"
            : "keyboard/mouse",
        context.input.connectedGamepadCount(),
        context.input.activeGamepadName().empty() ? "" : " - ",
        context.input.activeGamepadName().c_str());
    if (context.input.invalidBindingCount() != 0) {
        ImGui::Text("Invalid input bindings %zu", context.input.invalidBindingCount());
    }
    ImGui::Text(
        "End %s",
        rules::isEndUnlocked(context.level, state) ? "unlocked" : "locked");
    ImGui::BeginDisabled(context.inOverworld);
    result.solveCurrentScreen = ImGui::Button("Solve Current Screen");
    ImGui::EndDisabled();
    ImGui::Text(
        "Task workers %u, tasks run %llu",
        taskSystem().workerCount(),
        static_cast<unsigned long long>(taskSystem().executedTaskCount()));
    ImGui::Text(
        "Profile saves %llu writes / %llu requests (%llu coalesced)%s",
        static_cast<unsigned long long>(context.saveDiagnostics.completedWrites),
        static_cast<unsigned long long>(context.saveDiagnostics.requests),
        static_cast<unsigned long long>(context.saveDiagnostics.coalescedRequests),
        context.saveDiagnostics.writing
            ? " - writing"
            : (context.saveDiagnostics.pending ? " - pending" : ""));
    const log::Diagnostics logDiagnostics = log::diagnostics();
    ImGui::Text(
        "Log %llu written, %zu / %zu queued, %llu flushes%s",
        static_cast<unsigned long long>(
            logDiagnostics.writtenMessages),
        logDiagnostics.queuedMessages,
        logDiagnostics.queueCapacity,
        static_cast<unsigned long long>(logDiagnostics.flushes),
        logDiagnostics.writerActive ? " - writing" : "");
    if (logDiagnostics.droppedMessages != 0 ||
        logDiagnostics.fileSinkFailures != 0) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.65f, 0.30f, 1.0f),
            "Log dropped %llu, sink failures %llu",
            static_cast<unsigned long long>(
                logDiagnostics.droppedMessages),
            static_cast<unsigned long long>(
                logDiagnostics.fileSinkFailures));
    }
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
    ImGui::BeginDisabled(!context.renderer.wireframeSupported());
    if (ImGui::Checkbox("Wireframe", &wireframeEnabled)) {
        context.renderer.setWireframeEnabled(wireframeEnabled);
    }
    ImGui::EndDisabled();
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

    bool modelBackfaceCulling =
        context.renderer.modelBackfaceCullingEnabled();
    if (ImGui::Checkbox("Cull Model Back Faces", &modelBackfaceCulling)) {
        context.renderer.setModelBackfaceCullingEnabled(
            modelBackfaceCulling);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Skips the interior of closed glTF meshes. Turn this off if a "
            "model looks hollow or partly missing: that means its triangles "
            "are wound the other way. Tile quads are unaffected.");
    }

    bool opaqueFrontToBack =
        context.renderer.opaqueFrontToBackSortEnabled();
    if (ImGui::Checkbox("Sort Opaque Front To Back", &opaqueFrontToBack)) {
        context.renderer.setOpaqueFrontToBackSortEnabled(opaqueFrontToBack);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Draws opaque tile faces nearest first so the depth test can "
            "reject hidden fragments before shading. Turn this off to go "
            "back to the painter's order. Where two opaque surfaces are "
            "exactly coincident the depth test lets the last one drawn win, "
            "so a surface that flips between the two settings is a "
            "coincident-geometry problem, not a culling one.");
    }

    bool frustumCulling = context.renderer.frustumCullingEnabled();
    if (ImGui::Checkbox("Frustum Cull Main Scene", &frustumCulling)) {
        context.renderer.setFrustumCullingEnabled(frustumCulling);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Filters only main color/depth draw lists. Picking and shadow "
            "casters remain unchanged; model instances without retained "
            "mesh bounds fail open.");
    }

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
            config::minimumTileGridLineWidth,
            config::maximumTileGridLineWidth,
            "%.2f px");
    }

    if (ImGui::CollapsingHeader("Tile Geometry")) {
        float stepDurationSeconds =
            context.gameplaySession.stepDurationSeconds();
        ImGui::DragFloat(
            "Step Duration",
            &stepDurationSeconds,
            0.005f,
            config::minimumStepDurationSeconds,
            config::maximumStepDurationSeconds,
            "%.3f s");
        context.gameplaySession.setStepDurationSeconds(
            std::clamp(
                stepDurationSeconds,
                config::minimumStepDurationSeconds,
                config::maximumStepDurationSeconds));

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
            config::minimumSurfaceEntityHeight,
            config::maximumSurfaceEntityHeight,
            "%.3f");
        ImGui::DragFloat(
            "Surface Entity Width / Depth",
            &settings.geometry.surfaceEntityWidthDepth,
            0.01f,
            config::minimumSurfaceEntityWidthDepth,
            config::maximumSurfaceEntityWidthDepth,
            "%.2f");
        ImGui::TextDisabled("End and pressure plate geometry");

    }

    if (ImGui::CollapsingHeader("Water")) {
        auto& water = settings.water;
        if (ImGui::Button("Reset Water Defaults")) {
            water = {};
        }
        ImGui::TextDisabled(
            "Live renderer tuning; reset restores WaterConfig defaults.");

        if (ImGui::TreeNodeEx(
                "Optics", ImGuiTreeNodeFlags_DefaultOpen)) {
            float surfaceColor[4] {
                water.surfaceColor.x,
                water.surfaceColor.y,
                water.surfaceColor.z,
                water.surfaceColor.w,
            };
            if (ImGui::ColorEdit4("Surface Color", surfaceColor)) {
                water.surfaceColor = {
                    surfaceColor[0],
                    surfaceColor[1],
                    surfaceColor[2],
                    surfaceColor[3],
                };
            }
            ImGui::DragFloat(
                "Refraction Strength",
                &water.refractionStrength,
                0.0001f,
                config::minimumWaterRefractionStrength,
                config::maximumWaterRefractionStrength,
                "%.4f");
            ImGui::SliderFloat(
                "Underwater Caustics",
                &water.underwaterCausticStrength,
                0.0f,
                1.0f,
                "%.2f");
            ImGui::Checkbox(
                "Caustics Only (Hide Water)",
                &water.visualizeCausticsOnly);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx(
                "Ripples", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat(
                "Spatial Frequency",
                &water.rippleSpatialFrequency,
                0.02f,
                config::minimumWaterRippleSpatialFrequency,
                config::maximumWaterRippleSpatialFrequency,
                "%.2f");
            ImGui::DragFloat(
                "Animation Speed",
                &water.rippleSpeed,
                0.02f,
                config::minimumWaterRippleSpeed,
                config::maximumWaterRippleSpeed,
                "%.2f");
            ImGui::SliderFloat(
                "Primary Opacity",
                &water.primaryRippleOpacity,
                0.0f,
                1.0f,
                "%.2f");
            ImGui::SliderFloat(
                "Secondary Opacity",
                &water.secondaryRippleOpacity,
                0.0f,
                1.0f,
                "%.2f");
            ImGui::DragFloat(
                "Crest Half Width",
                &water.rippleCrestHalfWidth,
                0.001f,
                config::minimumWaterRippleWidth,
                config::maximumWaterRippleWidth,
                "%.3f");
            ImGui::DragFloat(
                "Halo Width",
                &water.rippleHaloWidth,
                0.001f,
                config::minimumWaterRippleWidth,
                config::maximumWaterRippleWidth,
                "%.3f");
            ImGui::SliderFloat(
                "Halo Strength",
                &water.rippleHaloStrength,
                0.0f,
                1.0f,
                "%.2f");
            ImGui::SliderFloat(
                "Crest Strength",
                &water.rippleCrestStrength,
                0.0f,
                1.0f,
                "%.2f");
            ImGui::DragFloat(
                "Secondary Width Scale",
                &water.secondaryRippleThicknessScale,
                0.01f,
                config::minimumWaterSecondaryRippleThicknessScale,
                config::maximumWaterSecondaryRippleThicknessScale,
                "%.2f");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Shoreline")) {
            ImGui::SliderFloat(
                "Primary Foam Opacity",
                &water.primaryShorelineOpacity,
                0.0f,
                1.0f,
                "%.2f");
            ImGui::SliderFloat(
                "Secondary Foam Opacity",
                &water.secondaryShorelineOpacity,
                0.0f,
                1.0f,
                "%.2f");
            ImGui::TreePop();
        }
    }

    if (ImGui::CollapsingHeader("Audio")) {
        PlayerProfile::AudioSettings audioSettings = context.audioSettings;
        bool settingsChanged = false;
        bool settingsCommitted = false;
        settingsChanged = ImGui::SliderFloat(
            "Master Volume",
            &audioSettings.masterVolume,
            config::minimumVolume,
            config::maximumVolume,
            "%.2f");
        settingsCommitted = ImGui::IsItemDeactivatedAfterEdit();
        settingsChanged = ImGui::SliderFloat(
            "Music Volume",
            &audioSettings.musicVolume,
            config::minimumVolume,
            config::maximumVolume,
            "%.2f") ||
            settingsChanged;
        settingsCommitted = ImGui::IsItemDeactivatedAfterEdit() || settingsCommitted;
        settingsChanged = ImGui::SliderFloat(
            "Sound Volume",
            &audioSettings.soundVolume,
            config::minimumVolume,
            config::maximumVolume,
            "%.2f") ||
            settingsChanged;
        settingsCommitted = ImGui::IsItemDeactivatedAfterEdit() || settingsCommitted;
        if ((settingsChanged || settingsCommitted) && context.updateAudioSettings) {
            context.updateAudioSettings(audioSettings, settingsCommitted);
        }
        float footstepInterval = context.audio.footstepIntervalSeconds();
        if (ImGui::DragFloat(
                "Footstep Interval",
                &footstepInterval,
                0.005f,
                config::minimumFootstepIntervalSeconds,
                config::maximumFootstepIntervalSeconds,
                "%.3f s")) {
            context.audio.setFootstepIntervalSeconds(footstepInterval);
        }
        ImGui::TextDisabled("Footsteps while walking; a looping stone drag while pushing.");
        if (!context.audio.available()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Audio unavailable (no device or missing files).");
        }
    }

    if (ImGui::CollapsingHeader("Output")) {
        ImGui::Text(
            "Exposure: %+.2f EV (player setting)",
            settings.outputTransform.exposureEv);
        int curve = static_cast<int>(settings.outputTransform.curve);
        if (ImGui::Combo(
                "Tonemap Curve",
                &curve,
                "Straight Clamp\0Khronos PBR Neutral\0")) {
            settings.outputTransform.curve =
                static_cast<TonemapCurve>(curve);
        }
        ImGui::TextDisabled(
            "Clamp is a Debug A/B reference; PBR Neutral is the default.");
    }

    if (ImGui::CollapsingHeader("Lighting")) {
        auto& lighting = settings.lighting;
        ImGui::DragFloat(
            "Sun Azimuth",
            &lighting.sunAzimuthDegrees,
            0.5f,
            config::minimumSunAzimuthDegrees,
            config::maximumSunAzimuthDegrees,
            "%.1f deg");
        ImGui::DragFloat(
            "Sun Tilt",
            &lighting.sunTiltDegrees,
            0.5f,
            config::minimumSunTiltDegrees,
            config::maximumSunTiltDegrees,
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
            config::maximumSunIntensity,
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
            config::maximumAmbientLightIntensity,
            "%.2f");
        ImGui::DragFloat(
            "Specular Strength",
            &lighting.specularStrength,
            0.01f,
            0.0f,
            config::maximumSpecularStrength,
            "%.2f");
        ImGui::DragFloat(
            "Model Shadow Receive",
            &lighting.modelShadowReceive,
            0.01f,
            0.0f,
            config::maximumModelShadowReceive,
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
            config::maximumAmbientOcclusionStrength,
            "%.2f");
        // Two views, not one. The occlusion buffer was always inspectable;
        // the ambient mask is the channel V7's first half added to the scene
        // target's alpha, and it is invisible in the finished image.
        using AmbientOcclusionDebug =
            RenderFrameData::Lighting::AmbientOcclusion::Debug;
        int debugView = static_cast<int>(lighting.ambientOcclusionDebug);
        if (ImGui::Combo(
                "SSAO Debug View",
                &debugView,
                "Off\0Occlusion\0Ambient Mask\0")) {
            lighting.ambientOcclusionDebug =
                static_cast<AmbientOcclusionDebug>(debugView);
        }
        ImGui::EndDisabled();

        ImGui::Checkbox("Shadows", &lighting.shadowsEnabled);
        ImGui::BeginDisabled(!lighting.shadowsEnabled);
        ImGui::DragFloat(
            "Shadow Opacity",
            &lighting.shadowOpacity,
            0.01f,
            0.0f,
            config::maximumShadowOpacity,
            "%.2f");
        ImGui::DragFloat(
            "Shadow Bias",
            &lighting.shadowBias,
            0.0005f,
            0.0f,
            config::maximumShadowBias,
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
        if (renderStats.cpuFrameTiming.available) {
            ImGui::Text(
                "CPU frame %.3f ms (avg %.3f, p95 %.3f, worst %.3f; %u samples)",
                renderStats.cpuFrameTiming.latestMilliseconds,
                renderStats.cpuFrameTiming.averageMilliseconds,
                renderStats.cpuFrameTiming.p95Milliseconds,
                renderStats.cpuFrameTiming.maximumMilliseconds,
                renderStats.cpuFrameTiming.samples);
        } else {
            ImGui::TextUnformatted("CPU frame timing pending");
        }
        if (renderStats.gpuFrameTiming.available) {
            ImGui::Text(
                "GPU frame %.3f ms (avg %.3f, p95 %.3f, worst %.3f; %u samples)",
                renderStats.gpuFrameTiming.latestMilliseconds,
                renderStats.gpuFrameTiming.averageMilliseconds,
                renderStats.gpuFrameTiming.p95Milliseconds,
                renderStats.gpuFrameTiming.maximumMilliseconds,
                renderStats.gpuFrameTiming.samples);
        } else if (!renderStats.gpuTimestampsSupported) {
            ImGui::TextUnformatted("GPU timestamps unavailable");
        } else {
            ImGui::TextUnformatted("GPU frame timing pending");
        }
        ImGui::Text(
            "Recorded frame %llu",
            static_cast<unsigned long long>(renderStats.frameIndex));
        ImGui::Text(
            "GPU %s (%s)",
            context.renderer.physicalDeviceName().data(),
            context.renderer.physicalDeviceTypeName());
        ImGui::Text(
            "Swapchain %u x %u, %u images",
            renderStats.swapchainWidth,
            renderStats.swapchainHeight,
            renderStats.swapchainImages);
        ImGui::Text(
            "Scene %u x %u (%u%%), UI native",
            renderStats.renderWidth,
            renderStats.renderHeight,
            renderStats.renderScalePercent);
        ImGui::Text(
            "SSAO %u x %u (half scene resolution)",
            renderStats.ssaoWidth,
            renderStats.ssaoHeight);
        const double megapixels =
            static_cast<double>(renderStats.swapchainWidth) *
            static_cast<double>(renderStats.swapchainHeight) / 1'000'000.0;
        ImGui::Text(
            "Present %s, %.2f output MP",
            context.renderer.presentModeName(),
            megapixels);
        ImGui::Text(
            "Scene workload %.1f M MSAA sample-pixels",
            static_cast<double>(renderStats.renderWidth) *
                static_cast<double>(renderStats.renderHeight) /
                1'000'000.0 * renderStats.activeSamples);
        ImGui::Text("Active samples %ux", renderStats.activeSamples);
        ImGui::Text(
            "Wireframe %s",
            renderStats.wireframeEnabled ? "on" : "off");
        ImGui::Text(
            "Wireframe line width %.1f",
            renderStats.wireframeLineWidth);
        ImGui::Text("Render tiles %u", renderStats.totalTiles);
        ImGui::Text(
            "Scene preparations %u (iso %u, shadow %u, models %u/%u ready, particles %u)",
            renderStats.scenePreparations,
            renderStats.preparedIsoFaces,
            renderStats.preparedShadowFaces,
            renderStats.preparedModels - renderStats.unavailableModels,
            renderStats.preparedModels,
            renderStats.preparedParticles);
        ImGui::Text(
            "Point-shadow faces %u/%u in range (%u culled; %u draw submissions avoided)",
            renderStats.pointShadowFacesInRange,
            renderStats.pointShadowFaceCandidates,
            renderStats.pointShadowFacesCulled,
            renderStats.pointShadowFacesCulled * 6U);
        ImGui::Text(
            "Point-shadow cube faces %u rendered, %u reused",
            renderStats.pointShadowCubeFacesRendered,
            renderStats.pointShadowCubeFacesReused);
        ImGui::Text(
            "Point-shadow models %u/%u in range (%u culled)",
            renderStats.pointShadowModelsInRange,
            renderStats.pointShadowModelCandidates,
            renderStats.pointShadowModelsCulled);
        ImGui::Text("Visible faces %u", renderStats.visibleFaces);
        ImGui::Text(
            "Persistent renderables %u (%u visible, %u culled; bounds %u reused, %u rebuilt)",
            renderStats.persistentRenderables,
            renderStats.visibleRenderables,
            renderStats.culledRenderables,
            renderStats.reusedRenderableBounds,
            renderStats.rebuiltRenderableBounds);
        ImGui::Text(
            "Main-scene frustum culling %s",
            renderStats.frustumCullingEnabled ? "on" : "off");
        ImGui::Text(
            "Parallel scene preparation %s",
            renderStats.parallelScenePreparationEnabled ? "on" : "off");
        ImGui::Text(
            "Point-shadow range/cache optimizations %s",
            renderStats.pointShadowOptimizationsEnabled ? "on" : "off");
        ImGui::Text(
            "Recorder scratch reuse %s (%u growths, %llu capacity bytes)",
            renderStats.recorderScratchReuseEnabled ? "on" : "off",
            renderStats.recorderScratchGrowths,
            static_cast<unsigned long long>(
                renderStats.recorderScratchCapacityBytes));
        const auto showPhase = [](const char* label,
                                   const RenderPhaseTiming& timing) {
            if (!timing.available) {
                ImGui::Text("%s timing pending", label);
                return;
            }
            ImGui::Text(
                "%s %.3f ms avg (p95 %.3f, max %.3f; %u samples)",
                label,
                timing.averageMilliseconds,
                timing.p95Milliseconds,
                timing.maximumMilliseconds,
                timing.samples);
        };
        showPhase("Asset scheduling", renderStats.assetSchedulingTiming);
        showPhase("Frame-fence wait", renderStats.frameFenceWaitTiming);
        showPhase("Asset maintenance", renderStats.assetMaintenanceTiming);
        showPhase("Image acquisition", renderStats.imageAcquisitionTiming);
        showPhase("Command recording", renderStats.commandRecordingTiming);
        showPhase("Submit/present", renderStats.submitPresentTiming);
        showPhase("  Recorder setup", renderStats.recorderSetupTiming);
        showPhase(
            "  Game command recording",
            renderStats.gameCommandRecordingTiming);
        showPhase(
            "    Shadow command recording",
            renderStats.shadowCommandRecordingTiming);
        showPhase(
            "    Scene command recording",
            renderStats.sceneCommandRecordingTiming);
        showPhase(
            "  SSAO command recording",
            renderStats.ssaoCommandRecordingTiming);
        showPhase(
            "  Preview command recording",
            renderStats.previewCommandRecordingTiming);
        showPhase(
            "  Output/UI command recording",
            renderStats.outputCommandRecordingTiming);
        showPhase(
            "Asset publication events",
            renderStats.assetPublicationEventTiming);
        showPhase("GPU shadows", renderStats.gpuShadowTiming);
        showPhase("GPU scene color/depth", renderStats.gpuSceneTiming);
        showPhase("  GPU scene raster/resolve", renderStats.gpuSceneRasterTiming);
        showPhase(
            "  GPU scene depth publish", renderStats.gpuSceneDepthPublishTiming);
        showPhase("  GPU scene translucency", renderStats.gpuSceneTranslucencyTiming);
        showPhase("GPU SSAO", renderStats.gpuSsaoTiming);
        showPhase("  GPU SSAO scene snapshot", renderStats.gpuSsaoSnapshotTiming);
        showPhase("  GPU SSAO occlusion", renderStats.gpuSsaoOcclusionTiming);
        showPhase("  GPU SSAO composite", renderStats.gpuSsaoCompositeTiming);
        showPhase("GPU output/UI", renderStats.gpuOutputTiming);
        ImGui::Text(
            "Asset publications %llu across %llu frames; texture uploads %llu/%llu complete (%u in flight)",
            static_cast<unsigned long long>(renderStats.assetPublications),
            static_cast<unsigned long long>(
                renderStats.assetPublicationFrames),
            static_cast<unsigned long long>(
                renderStats.textureUploadCompletions),
            static_cast<unsigned long long>(
                renderStats.textureUploadSubmissions),
            renderStats.textureUploadsInFlight);
        if (renderStats.scenePreparationTiming.available) {
            ImGui::Text(
                "Scene preparation %.3f ms avg (p95 %.3f, max %.3f; %u samples)",
                renderStats.scenePreparationTiming.averageMilliseconds,
                renderStats.scenePreparationTiming.p95Milliseconds,
                renderStats.scenePreparationTiming.maximumMilliseconds,
                renderStats.scenePreparationTiming.samples);
        }
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
            "GPU texture uploads %u in flight, %llu submitted, %llu retired",
            assetStats.uploadingTextures,
            static_cast<unsigned long long>(
                assetStats.textureUploadSubmissions),
            static_cast<unsigned long long>(
                assetStats.textureUploadCompletions));
        ImGui::Text(
            "Animations %u/%u loaded, %u pending",
            assetStats.loadedAnimations,
            assetStats.totalAnimations,
            assetStats.pendingAnimations);
        ImGui::Text(
            "Streaming %u requested, %u ready, %u queued, %u CPU jobs",
            assetStats.requestedAssets,
            assetStats.readyRequestedAssets,
            assetStats.queuedAssets,
            assetStats.activeCpuJobs);
        ImGui::Text(
            "Cancelled stale prefetches %llu",
            static_cast<unsigned long long>(
                assetStats.cancelledPrefetches));
        ImGui::Text(
            "Asset residency %.1f / %.1f MiB meshes (peak %.1f), %.1f / %.1f MiB textures (peak %.1f)",
            static_cast<double>(assetStats.modelResidencyBytes) / (1024.0 * 1024.0),
            static_cast<double>(assetStats.modelResidencyBudgetBytes) / (1024.0 * 1024.0),
            static_cast<double>(assetStats.modelResidencyPeakBytes) / (1024.0 * 1024.0),
            static_cast<double>(assetStats.textureResidencyBytes) / (1024.0 * 1024.0),
            static_cast<double>(assetStats.textureResidencyBudgetBytes) / (1024.0 * 1024.0),
            static_cast<double>(assetStats.textureResidencyPeakBytes) / (1024.0 * 1024.0));
        ImGui::Text(
            "Residency evictions %llu, capacity blocks %llu"
            " (%llu oversized, %llu no mip tail, %llu nothing evictable)%s",
            static_cast<unsigned long long>(assetStats.residencyEvictions),
            static_cast<unsigned long long>(assetStats.residencyBudgetBlocks),
            static_cast<unsigned long long>(
                assetStats.residencyOversizedBlocks),
            static_cast<unsigned long long>(assetStats.residencyMipPlanBlocks),
            static_cast<unsigned long long>(
                assetStats.residencyNoVictimBlocks),
            assetStats.residencyBudgetBlocked ? ", capacity blocked" : "");
        ImGui::Text(
            "Fence retirement %u meshes (%.1f MiB), %u textures (%.1f MiB)",
            assetStats.retiringModels,
            static_cast<double>(assetStats.retiringModelBytes) /
                (1024.0 * 1024.0),
            assetStats.retiringTextures,
            static_cast<double>(assetStats.retiringTextureBytes) /
                (1024.0 * 1024.0));
        ImGui::Text(
            "Texture mip residency %u/%u levels, %u reduced (%.1f MiB omitted)",
            assetStats.residentTextureMipLevels,
            assetStats.availableTextureMipLevels,
            assetStats.mipDegradedTextures,
            static_cast<double>(assetStats.mipOmittedBytes) /
                (1024.0 * 1024.0));
        if (assetStats.failedAssets > 0) {
            ImGui::Text("Asset load failures %u", assetStats.failedAssets);
        }
        ImGui::Text(
            "Pipeline rebuilds %llu",
            static_cast<unsigned long long>(renderStats.pipelineRebuilds));
        ImGui::Text(
            "Resource reconfigurations %llu, retired %u%s",
            static_cast<unsigned long long>(
                renderStats.renderResourceReconfigurations),
            renderStats.retiredRenderResourceSets,
            renderStats.rendererReconfigurationPending
                ? ", pending"
                : "");
        ImGui::Text(
            "Swapchain recreations %llu",
            static_cast<unsigned long long>(
                renderStats.swapchainRecreations));
        ImGui::Text(
            "Zero-extent recreation deferrals %llu",
            static_cast<unsigned long long>(
                renderStats.swapchainRecreationDeferrals));
        ImGui::Text(
            "Present-queue retirement waits %llu",
            static_cast<unsigned long long>(
                renderStats.presentQueueRetirementWaits));
    }
#else
    (void)context;
#endif
    return result;
}

} // namespace sokoban
