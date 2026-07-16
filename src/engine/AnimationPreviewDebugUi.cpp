#include "engine/AnimationPreviewDebugUi.hpp"

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
    if (active_ && clip_) {
        if (playing_) {
            time_ += dt * speed_;
            if (clip_->durationSeconds > 0.0001f &&
                time_ > clip_->durationSeconds) {
                time_ = std::fmod(time_, clip_->durationSeconds);
            }
        }
        renderer.setAnimationPreview(&*clip_, time_);
    } else {
        renderer.setAnimationPreview(nullptr, 0.0f);
    }
#else
    (void)dt;
    (void)renderer;
#endif
}

void AnimationPreviewDebugUi::draw(VulkanRenderer& renderer)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (!scanned_) {
        rescan(renderer);
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
                renderer.setAnimationPreview(nullptr, 0.0f);
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
                    active_ = true;
                } catch (const std::exception& exception) {
                    renderer.setAnimationPreview(nullptr, 0.0f);
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
    ImGui::Checkbox("Preview On Player", &active_);
    if (ImGui::Button(playing_ ? "Pause" : "Play")) {
        playing_ = !playing_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        time_ = 0.0f;
    }
    ImGui::SliderFloat("Speed", &speed_, 0.1f, 3.0f, "%.2fx");
    const float duration = std::max(clip_->durationSeconds, 0.0001f);
    ImGui::SliderFloat("Time", &time_, 0.0f, duration, "%.2fs");
    ImGui::TextDisabled(
        "Plays on the player model, overriding gameplay animation.");
#else
    (void)renderer;
#endif
}

void AnimationPreviewDebugUi::rescan(VulkanRenderer& renderer)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    renderer.setAnimationPreview(nullptr, 0.0f);
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
