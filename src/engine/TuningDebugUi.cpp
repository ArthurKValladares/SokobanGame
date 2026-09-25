#include "engine/TuningDebugUi.hpp"

#include "engine/Log.hpp"
#include "engine/Tuning.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>
#include <vector>

namespace sokoban {
namespace {

// "fogOfWarNoiseScale" in a section with prefix "fogOfWar" reads as
// "Noise Scale".
std::string displayName(const tuning::Entry& entry)
{
    std::string_view name = entry.name;
    if (name.starts_with(entry.section->namePrefix) &&
        name.size() > entry.section->namePrefix.size()) {
        name.remove_prefix(entry.section->namePrefix.size());
    }
    std::string label;
    for (std::size_t index = 0; index < name.size(); ++index) {
        const char character = name[index];
        if (index > 0 && std::isupper(static_cast<unsigned char>(character))) {
            label += ' ';
        }
        label += index == 0
            ? static_cast<char>(
                  std::toupper(static_cast<unsigned char>(character)))
            : character;
    }
    return label;
}

void drawEntry(tuning::Entry& entry)
{
    ImGui::PushID(entry.name.data(), entry.name.data() + entry.name.size());
    const std::string label = displayName(entry);
    switch (entry.kind) {
    case tuning::Kind::Float: {
        const float speed = std::max((entry.maximum - entry.minimum) / 500.0f,
            0.0001f);
        ImGui::DragFloat(
            label.c_str(),
            static_cast<float*>(entry.value),
            speed,
            entry.minimum,
            entry.maximum,
            "%.4g",
            ImGuiSliderFlags_AlwaysClamp);
        break;
    }
    case tuning::Kind::UInt: {
        int value = static_cast<int>(*static_cast<uint32_t*>(entry.value));
        if (ImGui::SliderInt(
                label.c_str(),
                &value,
                static_cast<int>(entry.minimum),
                static_cast<int>(entry.maximum),
                "%d",
                ImGuiSliderFlags_AlwaysClamp)) {
            *static_cast<uint32_t*>(entry.value) =
                static_cast<uint32_t>(std::max(value, 0));
        }
        break;
    }
    case tuning::Kind::Color3:
        ImGui::ColorEdit3(label.c_str(), &static_cast<Vec3*>(entry.value)->x,
            ImGuiColorEditFlags_Float);
        break;
    }
    if (tuning::modified(entry)) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Revert")) {
            tuning::revert(entry);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Back to the value in the header.");
        }
    }
    ImGui::PopID();
}

} // namespace

TuningDebugUi::TuningDebugUi(std::filesystem::path sourceRoot)
    : sourceRoot_(std::move(sourceRoot))
{
}

void TuningDebugUi::draw()
{
    ImGui::TextWrapped(
        "Values apply immediately. Save writes a section's edited values "
        "into its header, where they become the defaults of the next "
        "build.");
    if (!status_.empty()) {
        if (statusIsError_) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", status_.c_str());
        } else {
            ImGui::TextWrapped("%s", status_.c_str());
        }
    }

    std::vector<const tuning::Section*> sections;
    for (const tuning::Entry& entry : tuning::entries()) {
        if (std::ranges::find(sections, entry.section) == sections.end()) {
            sections.push_back(entry.section);
        }
    }
    for (const tuning::Section* section : sections) {
        ImGui::PushID(section);
        std::size_t modifiedCount = 0;
        for (const tuning::Entry& entry : tuning::entries()) {
            modifiedCount += entry.section == section && tuning::modified(entry)
                ? 1U
                : 0U;
        }
        const std::string header = std::string(section->title) +
            (modifiedCount == 0
                    ? std::string()
                    : " (" + std::to_string(modifiedCount) + " unsaved)") +
            "###section";
        if (ImGui::CollapsingHeader(
                header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginDisabled(modifiedCount == 0);
            if (ImGui::Button("Save to header")) {
                const tuning::SaveResult result =
                    tuning::saveToSource(sourceRoot_, *section);
                statusIsError_ = !result.errors.empty();
                if (statusIsError_) {
                    status_ = "Save failed: " + result.errors.front();
                    log::error(log::Category::Editor) << status_;
                } else {
                    status_ = "Saved " + std::to_string(modifiedCount) +
                        " value(s) to " + std::string(section->sourceFile) +
                        ".";
                    log::info(log::Category::Editor) << status_;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert all")) {
                for (tuning::Entry& entry : tuning::entries()) {
                    if (entry.section == section) {
                        tuning::revert(entry);
                    }
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", std::string(section->sourceFile).c_str());
            for (tuning::Entry& entry : tuning::entries()) {
                if (entry.section == section) {
                    drawEntry(entry);
                }
            }
        }
        ImGui::PopID();
    }
    if (sections.empty()) {
        ImGui::TextDisabled("No tunable values are registered.");
    }
}

} // namespace sokoban
