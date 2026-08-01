#include "engine/AnimationPreviewDebugUi.hpp"

#include "engine/AnimationPreviewScene.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/PresentationSettings.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace sokoban {

void AnimationPreviewDebugUi::initialize(std::filesystem::path assetRoot)
{
    assetRoot_ = std::move(assetRoot);
}

void AnimationPreviewDebugUi::update(float dt, VulkanRenderer& renderer)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    PreviewSession* active = catalog_.active ? &catalog_ : nullptr;
    if (browser_.active) {
        active = &browser_;
    }
    if (active != nullptr && active->clip && !active->model.isCube()) {
        advanceSession(*active, dt);
        renderer.setAnimationPreview(
            active->model, &*active->clip, active->time);
    } else {
        renderer.setAnimationPreview(cubeModel, nullptr, 0.0f);
    }
#else
    (void)dt;
    (void)renderer;
#endif
}

void AnimationPreviewDebugUi::draw(
    VulkanRenderer& renderer,
    const AssetManifest& manifest)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (!scanned_) {
        rescan(renderer);
    }

    const char* modelLabel = browser_.model.isCube()
        ? "Select model..."
        : manifest.model(browser_.model).name.c_str();
    if (ImGui::BeginCombo("Model", modelLabel)) {
        for (std::size_t i = 0; i < manifest.models().size(); ++i) {
            const AssetManifest::Model& definition = manifest.models()[i];
            if (definition.geometry != ModelGeometry::Skinned) {
                continue;
            }
            const RenderModel candidate { static_cast<uint32_t>(i + 1) };
            const bool selected = candidate == browser_.model;
            if (ImGui::Selectable(definition.name.c_str(), selected) &&
                !selected) {
                renderer.setAnimationPreview(cubeModel, nullptr, 0.0f);
                browser_.model = candidate;
                browser_.active = browser_.clip.has_value();
                catalog_.active = false;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Rescan Assets")) {
        rescan(renderer);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu glTF files", files_.size());

    const char* fileLabel = fileIndex_ >= 0
        ? fileLabels_[static_cast<std::size_t>(fileIndex_)].c_str()
        : "Select file...";
    if (ImGui::BeginCombo("File", fileLabel)) {
        for (int i = 0; i < static_cast<int>(files_.size()); ++i) {
            const bool selected = i == fileIndex_;
            if (ImGui::Selectable(
                    fileLabels_[static_cast<std::size_t>(i)].c_str(),
                    selected) &&
                i != fileIndex_) {
                renderer.setAnimationPreview(cubeModel, nullptr, 0.0f);
                fileIndex_ = i;
                clipNames_ = listGltfAnimationNames(
                    files_[static_cast<std::size_t>(i)]);
                clipIndex_ = -1;
                browser_.clip.reset();
                browser_.active = false;
                error_.clear();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (fileIndex_ < 0) {
        ImGui::TextDisabled(
            "Pick a glTF/GLB file to browse its animations.");
        return;
    }
    if (clipNames_.empty()) {
        ImGui::TextDisabled("No animations in this file.");
        return;
    }

    const char* clipLabel = clipIndex_ >= 0
        ? clipNames_[static_cast<std::size_t>(clipIndex_)].c_str()
        : "Select animation...";
    if (ImGui::BeginCombo("Animation", clipLabel)) {
        for (int i = 0; i < static_cast<int>(clipNames_.size()); ++i) {
            const bool selected = i == clipIndex_;
            if (ImGui::Selectable(
                    clipNames_[static_cast<std::size_t>(i)].c_str(),
                    selected) &&
                i != clipIndex_) {
                clipIndex_ = i;
                error_.clear();
                try {
                    browser_.clip = loadGltfAnimationClip(
                        files_[static_cast<std::size_t>(fileIndex_)],
                        static_cast<uint32_t>(i));
                    browser_.time = 0.0f;
                    browser_.playing = true;
                    browser_.active = !browser_.model.isCube();
                    catalog_.active = false;
                } catch (const std::exception& exception) {
                    renderer.setAnimationPreview(cubeModel, nullptr, 0.0f);
                    browser_.clip.reset();
                    browser_.active = false;
                    error_ = exception.what();
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (!error_.empty()) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
            "%s",
            error_.c_str());
    }
    if (!browser_.clip) {
        return;
    }

    ImGui::Text(
        "Duration %.2fs, %zu channels",
        browser_.clip->durationSeconds,
        browser_.clip->channels.size());
    if (ImGui::Checkbox("Show Preview Scene", &browser_.active) &&
        browser_.active) {
        catalog_.active = false;
    }
    ImGui::BeginDisabled(browser_.model.isCube());
    if (ImGui::Button("|<")) {
        browser_.time = 0.0f;
        browser_.playing = false;
        browser_.active = true;
        catalog_.active = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("< Frame")) {
        browser_.time = std::max(0.0f, browser_.time - 1.0f / 60.0f);
        browser_.playing = false;
        browser_.active = true;
        catalog_.active = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(browser_.playing ? "Pause" : "Play")) {
        browser_.playing = !browser_.playing;
        browser_.active = true;
        catalog_.active = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame >")) {
        browser_.time = std::min(
            browser_.clip->durationSeconds,
            browser_.time + 1.0f / 60.0f);
        browser_.playing = false;
        browser_.active = true;
        catalog_.active = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(">|")) {
        browser_.time = browser_.clip->durationSeconds;
        browser_.playing = false;
        browser_.active = true;
        catalog_.active = false;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &browser_.loop);
    ImGui::SliderFloat("Speed", &browser_.speed, 0.1f, 3.0f, "%.2fx");
    const float duration =
        std::max(browser_.clip->durationSeconds, 0.0001f);
    if (ImGui::SliderFloat(
            "Timeline", &browser_.time, 0.0f, duration, "%.3fs")) {
        browser_.playing = false;
        browser_.active = true;
        catalog_.active = false;
    }
    ImGui::EndDisabled();
#else
    (void)renderer;
    (void)manifest;
#endif
}

void AnimationPreviewDebugUi::drawCatalogPreview(
    std::span<const AnimationCatalog::TimelineEvent> markers)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::PushID("CatalogEventPreview");
    if (!catalogError_.empty()) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
            "%s",
            catalogError_.c_str());
    }
    if (!catalog_.clip) {
        ImGui::TextDisabled(
            "Preview this use to author events against its source clip.");
        ImGui::PopID();
        return;
    }

    ImGui::Text(
        "Duration %.2fs, %zu channels",
        catalog_.clip->durationSeconds,
        catalog_.clip->channels.size());
    if (ImGui::Checkbox("Show Event Preview", &catalog_.active) &&
        catalog_.active) {
        browser_.active = false;
    }
    ImGui::BeginDisabled(catalog_.model.isCube());
    if (ImGui::Button("|<")) {
        catalog_.time = 0.0f;
        catalog_.playing = false;
        catalog_.active = true;
        browser_.active = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("< Frame")) {
        catalog_.time = std::max(0.0f, catalog_.time - 1.0f / 60.0f);
        catalog_.playing = false;
        catalog_.active = true;
        browser_.active = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(catalog_.playing ? "Pause" : "Play")) {
        catalog_.playing = !catalog_.playing;
        catalog_.active = true;
        browser_.active = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame >")) {
        catalog_.time = std::min(
            catalog_.clip->durationSeconds,
            catalog_.time + 1.0f / 60.0f);
        catalog_.playing = false;
        catalog_.active = true;
        browser_.active = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(">|")) {
        catalog_.time = catalog_.clip->durationSeconds;
        catalog_.playing = false;
        catalog_.active = true;
        browser_.active = false;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &catalog_.loop);
    ImGui::SliderFloat(
        "Speed", &catalog_.speed, 0.1f, 3.0f, "%.2fx");
    const float duration =
        std::max(catalog_.clip->durationSeconds, 0.0001f);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat(
            "##EventTimeline",
            &catalog_.time,
            0.0f,
            duration,
            "")) {
        catalog_.playing = false;
        catalog_.active = true;
        browser_.active = false;
    }
    const ImVec2 timelineMin = ImGui::GetItemRectMin();
    const ImVec2 timelineMax = ImGui::GetItemRectMax();
    for (const AnimationCatalog::TimelineEvent& marker : markers) {
        const float x = timelineMin.x +
            (timelineMax.x - timelineMin.x) * marker.normalizedTime;
        ImGui::GetWindowDrawList()->AddLine(
            { x, timelineMin.y },
            { x, timelineMax.y },
            IM_COL32(255, 205, 70, 255),
            2.0f);
    }
    char timelineLabel[64] {};
    std::snprintf(
        timelineLabel,
        sizeof(timelineLabel),
        "%.3fs / %.1f%%",
        catalog_.time,
        catalogNormalizedTime() * 100.0f);
    const ImVec2 labelSize = ImGui::CalcTextSize(timelineLabel);
    ImGui::GetWindowDrawList()->AddText(
        {
            timelineMin.x +
                (timelineMax.x - timelineMin.x - labelSize.x) * 0.5f,
            timelineMin.y +
                (timelineMax.y - timelineMin.y - labelSize.y) * 0.5f,
        },
        IM_COL32(235, 240, 245, 255),
        timelineLabel);
    ImGui::EndDisabled();
    ImGui::PopID();
#else
    (void)markers;
#endif
}

bool AnimationPreviewDebugUi::previewCatalogAnimation(
    RenderModel model,
    RenderAnimation animation,
    const AssetManifest& manifest,
    VulkanRenderer& renderer)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (model.isCube() || animation.isNone() ||
        manifest.model(model).geometry != ModelGeometry::Skinned) {
        return false;
    }
    const AssetManifest::Animation& definition = manifest.animation(animation);
    const std::filesystem::path path =
        (assetRoot_ / definition.path).lexically_normal();
    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(path, errorCode) || errorCode) {
        catalogError_ = "Animation source file is unavailable.";
        return false;
    }
    try {
        PreviewSession replacement {
            .clip = loadGltfAnimationClip(
                path, animationIndexFromManifestClip(definition.clip)),
            .time = 0.0f,
            .speed = 1.0f,
            .model = model,
            .playing = false,
            .loop = false,
            .active = true,
        };
        catalog_ = std::move(replacement);
        browser_.active = false;
        catalogError_.clear();
        return true;
    } catch (const std::exception& exception) {
        renderer.setAnimationPreview(cubeModel, nullptr, 0.0f);
        catalog_ = {};
        catalogError_ = exception.what();
        return false;
    }
#else
    (void)model;
    (void)animation;
    (void)manifest;
    (void)renderer;
    return false;
#endif
}

void AnimationPreviewDebugUi::clearCatalogPreview(VulkanRenderer& renderer)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    catalog_ = {};
    catalogError_.clear();
    if (!browser_.active) {
        renderer.setAnimationPreview(cubeModel, nullptr, 0.0f);
    }
#else
    (void)renderer;
#endif
}

void AnimationPreviewDebugUi::setCatalogNormalizedTime(float normalizedTime)
{
    if (!catalog_.clip) {
        return;
    }
    catalog_.time = catalog_.clip->durationSeconds *
        std::clamp(normalizedTime, 0.0f, 1.0f);
    catalog_.playing = false;
    catalog_.active = !catalog_.model.isCube();
    if (catalog_.active) {
        browser_.active = false;
    }
}

float AnimationPreviewDebugUi::catalogNormalizedTime() const
{
    return catalog_.clip && catalog_.clip->durationSeconds > 0.0f
        ? std::clamp(
              catalog_.time / catalog_.clip->durationSeconds,
              0.0f,
              1.0f)
        : 0.0f;
}

float AnimationPreviewDebugUi::catalogDurationSeconds() const
{
    return catalog_.clip ? catalog_.clip->durationSeconds : 0.0f;
}

std::optional<RenderFrameData> AnimationPreviewDebugUi::previewFrame(
    const AssetManifest& manifest,
    const PresentationSettings& settings) const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const PreviewSession* active = catalog_.active ? &catalog_ : nullptr;
    if (browser_.active) {
        active = &browser_;
    }
    if (active != nullptr && active->clip && !active->model.isCube()) {
        return animationPreviewScene::build(
            active->model, manifest, settings);
    }
#else
    (void)manifest;
    (void)settings;
#endif
    return std::nullopt;
}

void AnimationPreviewDebugUi::rescan(VulkanRenderer& renderer)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    renderer.setAnimationPreview(cubeModel, nullptr, 0.0f);
    files_.clear();
    fileLabels_.clear();
    fileIndex_ = -1;
    clipNames_.clear();
    clipIndex_ = -1;
    browser_.clip.reset();
    error_.clear();
    browser_ = {};
    scanned_ = true;

    std::error_code errorCode;
    std::filesystem::recursive_directory_iterator it(assetRoot_, errorCode);
    const std::filesystem::recursive_directory_iterator end;
    for (; !errorCode && it != end; it.increment(errorCode)) {
        if (!it->is_regular_file(errorCode)) {
            continue;
        }
        const std::string extension = it->path().extension().string();
        if (extension == ".glb" || extension == ".gltf") {
            files_.push_back(it->path());
        }
    }
    std::ranges::sort(files_);
    fileLabels_.reserve(files_.size());
    for (const std::filesystem::path& path : files_) {
        fileLabels_.push_back(
            path.lexically_relative(assetRoot_).generic_string());
    }
#else
    (void)renderer;
#endif
}

void AnimationPreviewDebugUi::advanceSession(
    PreviewSession& session,
    float dt)
{
    if (!session.playing || !session.clip) {
        return;
    }
    session.time += dt * session.speed;
    const float duration = session.clip->durationSeconds;
    if (duration <= 0.0001f || session.time < duration) {
        return;
    }
    if (session.loop) {
        session.time = std::fmod(session.time, duration);
    } else {
        session.time = duration;
        session.playing = false;
    }
}

} // namespace sokoban
