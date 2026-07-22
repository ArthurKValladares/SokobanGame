#pragma once

#include "engine/AsyncSaveStore.hpp"
#include "engine/PlayerProfile.hpp"
#include "engine/SaveSlotState.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace sokoban {

// Headless owner of the save-slot lifecycle: the per-slot progress stores,
// the shared settings store, the active-slot marker file, slot summaries,
// switching, and deletion. The caller (Application) owns the live
// PlayerProfile and all gameplay consequences; this class owns every disk
// decision. Fresh installs write nothing: files appear only through explicit
// save requests (first checkpoint or a settings change).
class SaveSlotManager {
public:
    static constexpr int slotCount = 3;

    struct SlotSummary {
        SaveSlotState state = SaveSlotState::Empty;
        bool completed = false; // every level 0..levelCount-1 completed
        int currentLevel = 0; // 0-based
        int completedLevels = 0;
    };

    explicit SaveSlotManager(
        std::filesystem::path directory,
        std::chrono::milliseconds writeDelay = std::chrono::seconds(2));

    // Loads the marker's slot merged with the shared settings file. When the
    // settings file does not exist yet, a pre-split combined save's settings
    // are adopted and written; a genuinely fresh install writes nothing.
    [[nodiscard]] PlayerProfile loadActiveProfile();

    [[nodiscard]] int activeSlot() const { return activeSlot_; }
    [[nodiscard]] const std::filesystem::path& directory() const { return directory_; }

    // One summary per slot; the active slot summarizes `activeProfile`
    // (live, possibly unsaved). Non-active slots decode their files once and
    // are cached - only this process writes them, so the cache is refreshed
    // solely by switchTo/deleteSlot (which is why saving the active slot
    // needs no invalidation).
    [[nodiscard]] std::vector<SlotSummary> slotSummaries(
        const PlayerProfile& activeProfile,
        int levelCount) const;

    // Flushes pending writes, swaps stores and the marker to `slot`, and
    // returns its progress with `currentProfile`'s shared settings carried
    // over. Empty when the slot is invalid or already active. The caller
    // settles (checkpoints/saves) the outgoing profile first.
    [[nodiscard]] std::optional<PlayerProfile> switchTo(
        int slot,
        const PlayerProfile& currentProfile);

    // Removes the slot's save files (primary and backup). Deleting the
    // active slot drains pending writes first; the caller resets its live
    // profile's progress alongside.
    void deleteSlot(int slot);

    void saveProgress(const PlayerProfile& profile, bool immediate);
    // Persists only the settings sections into the shared settings file.
    void saveSettings(const PlayerProfile& profile, bool immediate);
    void flush();

    [[nodiscard]] std::string progressStatus() const;
    [[nodiscard]] AsyncSaveStore::Diagnostics progressDiagnostics() const;
    [[nodiscard]] std::string settingsStatus() const;
    [[nodiscard]] AsyncSaveStore::Diagnostics settingsDiagnostics() const;

private:
    [[nodiscard]] static SlotSummary summarize(
        const PlayerProfile& profile,
        int levelCount);
    [[nodiscard]] SlotSummary inspectSlotSummary(int slot, int levelCount) const;
    [[nodiscard]] int readActiveSlotMarker() const;
    void writeActiveSlotMarker(int slot) const;

    std::filesystem::path directory_;
    // Decoded summaries for non-active slots, keyed by slot; nullopt entries
    // are decoded on demand. Cleared on switchTo/deleteSlot.
    mutable std::vector<std::optional<SlotSummary>> summaryCache_ =
        std::vector<std::optional<SlotSummary>>(slotCount);
    mutable int summaryCacheLevelCount_ = -1;
    std::chrono::milliseconds writeDelay_;
    int activeSlot_ = 0; // 0-based
    // One worker serves both channels: settings (0, shared across slots) and
    // progress (repointed at the active slot's file on switch).
    std::unique_ptr<AsyncSaveStore> store_;
    static constexpr int kSettingsChannel = 0;
    int progressChannel_ = 1;
};

} // namespace sokoban
