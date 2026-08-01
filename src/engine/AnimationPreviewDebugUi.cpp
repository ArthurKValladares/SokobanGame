#include "engine/AnimationPreviewDebugUi.hpp"

#include "engine/AnimationPreviewScene.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/PresentationSettings.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#endif

#include <algorithm>
#include <cmath>
#include <utility>

namespace sokoban {

void AnimationPreviewDebugUi::initialize(std::filesystem::path assetRoot)
{
    assetRoot_ = std::move(assetRoot);
}

void AnimationPreviewDebugUi::update(float dt, VulkanRenderer& renderer)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (active_ && clip_ && !model_.isCube()) {
        if (playing_) {
            time_ += dt * speed_;
            const float duration = clip_->durationSeconds;
            if (duration > 0.0001f && time_ >= duration) {
                if (loop_) {
                    time_ = std::fmod(time_, duration);
                }
                else {
                    time_ = duration;
                    playing_ = false;
                }
            }
        }
        renderer.setAnimationPreview(model_, &*clip_, time_);
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

    const char* modelLabel = model_.isCube()
        ? "Select model..."
        : manifest.model(model_).name.c_str();
    if (ImGui::BeginCombo("Model", modelLabel)) {
        for (std::size_t i = 0; i < manifest.models().size(); ++i) {
            const AssetManifest::Model& definition = manifest.models()[i];
            if (definition.geometry != ModelGeometry::Skinned) {
                continue;
            }
            const RenderModel candidate { static_cast<uint32_t>(i + 1) };
            const bool selected = candidate == model_;
            if (ImGui::Selectable(definition.name.c_str(), selected) &&
                !selected) {
                renderer.setAnimationPreview(cubeModel, nullptr, 0.0f);
                model_ = candidate;
                active_ = clip_.has_value();
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
                clip_.reset();
                active_ = false;
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
                    clip_ = loadGltfAnimationClip(
                        files_[static_cast<std::size_t>(fileIndex_)],
                        static_cast<uint32_t>(i));
                    time_ = 0.0f;
                    playing_ = true;
                    active_ = !model_.isCube();
                } catch (const std::exception& exception) {
                    renderer.setAnimationPreview(cubeModel, nullptr, 0.0f);
                    clip_.reset();
                    active_ = false;
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
    if (!clip_) {
        return;
    }

    ImGui::Text(
        "Duration %.2fs, %zu channels",
        clip_->durationSeconds,
        clip_->channels.size());
    ImGui::Checkbox("Show Preview Scene", &active_);
    ImGui::BeginDisabled(model_.isCube());
    if (ImGui::Button("|<")) {
        time_ = 0.0f;
        playing_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("< Frame")) {
        time_ = std::max(0.0f, time_ - 1.0f / 60.0f);
        playing_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(playing_ ? "Pause" : "Play")) {
        playing_ = !playing_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame >")) {
        time_ = std::min(
            clip_->durationSeconds,
            time_ + 1.0f / 60.0f);
        playing_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(">|")) {
        time_ = clip_->durationSeconds;
        playing_ = false;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &loop_);
    ImGui::SliderFloat("Speed", &speed_, 0.1f, 3.0f, "%.2fx");
    const float duration = std::max(clip_->durationSeconds, 0.0001f);
    if (ImGui::SliderFloat(
            "Timeline", &time_, 0.0f, duration, "%.3fs")) {
        playing_ = false;
    }
    ImGui::EndDisabled();
#else
    (void)renderer;
    (void)manifest;
#endif
}

std::optional<RenderFrameData> AnimationPreviewDebugUi::previewFrame(
    const AssetManifest& manifest,
    const PresentationSettings& settings) const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (active_ && clip_ && !model_.isCube()) {
        return animationPreviewScene::build(model_, manifest, settings);
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
    clip_.reset();
    error_.clear();
    time_ = 0.0f;
    speed_ = 1.0f;
    playing_ = true;
    loop_ = true;
    active_ = false;
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

} // namespace sokoban
