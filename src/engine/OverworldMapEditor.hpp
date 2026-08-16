#pragma once

#include "engine/LevelProjectStore.hpp"
#include "engine/OverworldMap.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sokoban {

// Headless project-level editor for the composed overworld. It deliberately
// owns topology rather than tile editing; LevelEditor remains the authority
// for one component definition at a time.
class OverworldMapEditor {
public:
    struct ScreenSummary {
        OverworldScreenId id = 0;
        OverworldSlot slot {};
        std::filesystem::path path;
        std::size_t selectorCount = 0;
        bool selected = false;
    };

    void initialize(
        const std::filesystem::path& projectLevelRoot,
        std::optional<std::filesystem::path> runtimeLevelRoot = std::nullopt);
    [[nodiscard]] bool reload();

    [[nodiscard]] bool loaded() const { return loaded_; }
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
    [[nodiscard]] const OverworldLayout& layout() const { return state_.layout; }
    [[nodiscard]] std::optional<OverworldScreenId> selectedScreen() const
    {
        return state_.selected;
    }
    [[nodiscard]] const std::string& status() const { return status_; }
    [[nodiscard]] const std::filesystem::path& projectLevelRoot() const
    {
        return projectLevelRoot_;
    }

    [[nodiscard]] std::vector<ScreenSummary> screens() const;
    [[nodiscard]] std::vector<OverworldScreenId> deletedScreens() const;
    [[nodiscard]] const OverworldScreenSpec* screen(
        OverworldScreenId id) const;
    [[nodiscard]] const Level::Definition* definition(
        OverworldScreenId id) const;
    [[nodiscard]] std::filesystem::path screenPath(
        OverworldScreenId id) const;
    [[nodiscard]] OverworldDraftOverride draftOverride(
        std::optional<OverworldDefinitionOverride> activeDefinition =
            std::nullopt) const;

    [[nodiscard]] bool addAdjacentScreen(
        OverworldScreenId source,
        OverworldSlot slot);

    [[nodiscard]] bool selectScreen(OverworldScreenId id);
    [[nodiscard]] bool addScreen(OverworldSlot slot);
    [[nodiscard]] bool moveScreen(OverworldScreenId id, OverworldSlot slot);
    [[nodiscard]] bool deleteScreen(OverworldScreenId id);
    [[nodiscard]] bool restoreDeletedScreen(
        OverworldScreenId id,
        OverworldSlot slot);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] bool save();

private:
    struct DraftScreen {
        OverworldScreenSpec spec;
        Level::Definition definition;

        bool operator==(const DraftScreen&) const = default;
    };

    struct State {
        OverworldLayout layout;
        std::vector<DraftScreen> screens;
        std::optional<OverworldScreenId> selected;
        std::vector<OverworldScreenId> retiredIds;
        std::vector<OverworldScreenId> restoredIds;

        bool operator==(const State&) const = default;
    };

    [[nodiscard]] DraftScreen* draftScreen(OverworldScreenId id);
    [[nodiscard]] const DraftScreen* draftScreen(OverworldScreenId id) const;
    [[nodiscard]] bool slotAvailable(
        OverworldSlot slot,
        std::optional<OverworldScreenId> ignore = std::nullopt) const;
    [[nodiscard]] OverworldScreenId nextScreenId() const;
    [[nodiscard]] Level::Definition defaultDefinition() const;
    void record(State before, std::string status);

    std::filesystem::path projectLevelRoot_;
    std::optional<std::filesystem::path> runtimeLevelRoot_;
    State state_;
    State savedState_;
    std::vector<State> undo_;
    std::vector<State> redo_;
    std::string status_;
    bool loaded_ = false;
};

} // namespace sokoban
