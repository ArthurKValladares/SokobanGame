#include "engine/LogDebugUi.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>

namespace sokoban {
namespace {

constexpr std::array<log::Level, 4> levels {
    log::Level::Debug,
    log::Level::Info,
    log::Level::Warning,
    log::Level::Error,
};

ImVec4 colorForLevel(log::Level level)
{
    switch (level) {
    case log::Level::Debug:
        return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    case log::Level::Info:
        return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    case log::Level::Warning:
        return { 1.0f, 0.72f, 0.25f, 1.0f };
    case log::Level::Error:
        return { 1.0f, 0.35f, 0.32f, 1.0f };
    }
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

std::array<char, 16> timestampFor(
    std::chrono::system_clock::time_point time)
{
    const std::time_t seconds =
        std::chrono::system_clock::to_time_t(time);
    std::tm local {};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            time.time_since_epoch()) %
        std::chrono::seconds(1);
    std::array<char, 16> result {};
    std::snprintf(
        result.data(),
        result.size(),
        "%02d:%02d:%02d.%03lld",
        local.tm_hour,
        local.tm_min,
        local.tm_sec,
        static_cast<long long>(milliseconds.count()));
    return result;
}

bool containsCaseInsensitive(
    std::string_view text,
    std::string_view needle)
{
    if (needle.empty()) {
        return true;
    }
    return std::search(
               text.begin(),
               text.end(),
               needle.begin(),
               needle.end(),
               [](char left, char right) {
                   return std::tolower(static_cast<unsigned char>(left)) ==
                       std::tolower(static_cast<unsigned char>(right));
               }) != text.end();
}

bool matchesSearch(const log::Entry& entry, std::string_view search)
{
    return containsCaseInsensitive(entry.message, search) ||
        containsCaseInsensitive(log::levelName(entry.level), search) ||
        containsCaseInsensitive(log::categoryName(entry.category), search);
}

std::string formattedEntry(const log::Entry& entry)
{
    const std::array<char, 16> time = timestampFor(entry.timestamp);
    return "[" + std::string(time.data()) + "] [" +
        std::string(log::levelName(entry.level)) + "] [" +
        std::string(log::categoryName(entry.category)) + "] " +
        entry.message + '\n';
}

bool drawSingleLineMessage(std::string_view message)
{
    const std::size_t newline = message.find_first_of("\r\n");
    const char* begin = message.data();
    const char* end = newline == std::string_view::npos
        ? begin + message.size()
        : begin + newline;
    ImGui::TextUnformatted(begin, end);
    bool hovered = ImGui::IsItemHovered();
    if (newline != std::string_view::npos) {
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextDisabled("[multiline]");
        hovered |= ImGui::IsItemHovered();
    }
    return hovered;
}

} // namespace

LogDebugUi::LogDebugUi()
{
    enabledLevels_.fill(true);
    enabledCategories_.fill(true);
}

void LogDebugUi::refreshEntries()
{
    if (paused_) {
        return;
    }
    const log::HistorySnapshot snapshot = log::historySnapshot(
        hasObservedRevision_
            ? std::optional<uint64_t> { observedRevision_ }
            : std::nullopt);
    if (!hasObservedRevision_ || snapshot.revision != observedRevision_) {
        entries_ = snapshot.entries;
        observedRevision_ = snapshot.revision;
        hasObservedRevision_ = true;
        scrollToBottom_ = autoScroll_;
    }
}

void LogDebugUi::draw()
{
    refreshEntries();

    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputTextWithHint(
        "##LogSearch",
        "Search messages, levels, or categories",
        search_.data(),
        search_.size());
    ImGui::SameLine();
    if (ImGui::Checkbox("Pause", &paused_) && !paused_) {
        hasObservedRevision_ = false;
        refreshEntries();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Auto-scroll", &autoScroll_) && autoScroll_) {
        scrollToBottom_ = true;
    }

    ImGui::TextUnformatted("Levels:");
    ImGui::SameLine();
    for (std::size_t index = 0; index < levels.size(); ++index) {
        if (index != 0) {
            ImGui::SameLine();
        }
        ImGui::PushStyleColor(
            ImGuiCol_Text, colorForLevel(levels[index]));
        ImGui::Checkbox(
            log::levelName(levels[index]).data(),
            &enabledLevels_[index]);
        ImGui::PopStyleColor();
    }

    const std::size_t enabledCategoryCount = static_cast<std::size_t>(
        std::count(
            enabledCategories_.begin(),
            enabledCategories_.end(),
            true));
    const std::string categoryPreview = enabledCategoryCount ==
            enabledCategories_.size()
        ? "All categories"
        : std::to_string(enabledCategoryCount) + " of " +
            std::to_string(enabledCategories_.size()) + " categories";
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("Category", categoryPreview.c_str())) {
        if (ImGui::Button("All")) {
            enabledCategories_.fill(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("None")) {
            enabledCategories_.fill(false);
        }
        ImGui::Separator();
        for (std::size_t index = 0;
            index < enabledCategories_.size();
            ++index) {
            const auto category = static_cast<log::Category>(index);
            ImGui::Checkbox(
                log::categoryName(category).data(),
                &enabledCategories_[index]);
        }
        ImGui::EndCombo();
    }

    const std::string_view search(search_.data());
    std::vector<std::size_t> visibleEntries;
    visibleEntries.reserve(entries_.size());
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        const log::Entry& entry = entries_[index];
        const std::size_t level = static_cast<std::size_t>(entry.level);
        const std::size_t category =
            static_cast<std::size_t>(entry.category);
        if (level < enabledLevels_.size() &&
            category < enabledCategories_.size() &&
            enabledLevels_[level] &&
            enabledCategories_[category] &&
            matchesSearch(entry, search)) {
            visibleEntries.push_back(index);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Copy visible")) {
        std::string text;
        for (std::size_t index : visibleEntries) {
            text += formattedEntry(entries_[index]);
        }
        ImGui::SetClipboardText(text.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        log::clearHistory();
        entries_.clear();
        hasObservedRevision_ = false;
        visibleEntries.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Flush sinks")) {
        log::flush();
    }

    const log::Diagnostics diagnostics = log::diagnostics();
    ImGui::Text(
        "%zu visible / %zu retained | Queue %zu/%zu | Written %llu | "
        "Filtered %llu",
        visibleEntries.size(),
        entries_.size(),
        diagnostics.queuedMessages,
        diagnostics.queueCapacity,
        static_cast<unsigned long long>(diagnostics.writtenMessages),
        static_cast<unsigned long long>(diagnostics.filteredMessages));
    if (diagnostics.droppedMessages != 0 ||
        diagnostics.fileSinkFailures != 0 ||
        diagnostics.fileRotationFailures != 0) {
        ImGui::TextColored(
            colorForLevel(log::Level::Warning),
            "Dropped %llu | Sink failures %llu | Rotation failures %llu",
            static_cast<unsigned long long>(diagnostics.droppedMessages),
            static_cast<unsigned long long>(diagnostics.fileSinkFailures),
            static_cast<unsigned long long>(
                diagnostics.fileRotationFailures));
    } else {
        ImGui::TextDisabled(
            "File sink: %s | Writer: %s | Flushes: %llu",
            diagnostics.fileSinkOpen ? "open" : "closed",
            diagnostics.writerActive
                ? "active"
                : diagnostics.writerRunning ? "idle" : "stopped",
            static_cast<unsigned long long>(diagnostics.flushes));
    }

    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable(
            "LogEntries", 4, tableFlags, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(
            "Time", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableSetupColumn(
            "Level", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn(
            "Category", ImGuiTableColumnFlags_WidthFixed, 78.0f);
        ImGui::TableSetupColumn(
            "Message", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visibleEntries.size()));
        while (clipper.Step()) {
            for (int visible = clipper.DisplayStart;
                visible < clipper.DisplayEnd;
                ++visible) {
                const log::Entry& entry = entries_[visibleEntries[
                    static_cast<std::size_t>(visible)]];
                const std::array<char, 16> time =
                    timestampFor(entry.timestamp);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(time.data());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(
                    colorForLevel(entry.level),
                    "%s",
                    log::levelName(entry.level).data());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(
                    log::categoryName(entry.category).data());
                ImGui::TableSetColumnIndex(3);
                const bool messageHovered =
                    drawSingleLineMessage(entry.message);
                if (messageHovered &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    const std::string text = formattedEntry(entry);
                    ImGui::SetClipboardText(text.c_str());
                }
                if (messageHovered) {
                    ImGui::SetTooltip(
                        "Double-click to copy\n%s", entry.message.c_str());
                }
            }
        }
        if (scrollToBottom_) {
            ImGui::SetScrollY(ImGui::GetScrollMaxY());
            scrollToBottom_ = false;
        }
        ImGui::EndTable();
    }
}

} // namespace sokoban
