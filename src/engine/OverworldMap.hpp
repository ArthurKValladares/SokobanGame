#pragma once

#include "engine/Level.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sokoban {

using OverworldScreenId = uint32_t;

struct OverworldSlot {
    int x = 0;
    int y = 0;

    bool operator==(const OverworldSlot&) const = default;
};

struct OverworldPosition {
    OverworldScreenId screen = 0;
    GridPosition3 cell {};

    bool operator==(const OverworldPosition&) const = default;
};

struct OverworldScreenSpec {
    OverworldScreenId id = 0;
    std::filesystem::path file;
    OverworldSlot slot {};

    bool operator==(const OverworldScreenSpec&) const = default;
};

struct OverworldLayout {
    int format = 2;
    uint32_t screenWidth = 0;
    uint32_t screenHeight = 0;
    OverworldPosition start;
    std::vector<OverworldScreenSpec> screens;

    bool operator==(const OverworldLayout&) const = default;
};

// Strict, versioned layout I/O. The path names layout.json itself rather than
// its parent directory so project transactions can stage or inspect one file.
[[nodiscard]] OverworldLayout loadOverworldLayout(
    const std::filesystem::path& path);
void writeOverworldLayout(
    const std::filesystem::path& path,
    const OverworldLayout& layout);

enum class OverworldValidationMode {
    // Allows unassigned selectors and incomplete puzzle-screen coverage while
    // authors build the map. Every assigned target must still exist.
    Structural,
    // Shipping content: every selector is assigned, every puzzle screen is
    // covered, and one puzzle level may belong to only one overworld screen.
    Production,
};

struct OverworldScreenRuntime {
    OverworldScreenId id = 0;
    std::filesystem::path file;
    OverworldSlot slot {};
    GridPosition origin {};
    uint32_t depth = 0;
    Level::Definition definition;
};

struct OverworldSelectorRuntime {
    OverworldScreenId screen = 0;
    uint32_t localId = 0;
    uint32_t runtimeId = 0;
    GridPosition3 globalCell {};
    std::optional<LevelLocation> target;
};

struct OverworldDefinitionOverride {
    OverworldScreenId screen = 0;
    Level::Definition definition;
};

// An editor draft may replace the topology as well as any number of component
// definitions. A missing layout keeps the file-backed layout, which is useful
// when only one actively edited component is unsaved.
struct OverworldDraftOverride {
    std::optional<OverworldLayout> layout;
    std::vector<OverworldDefinitionOverride> definitions;
};

// Separately authored overworld chunks composed into one ordinary Level.
// Gameplay can therefore cross a walkable physical seam without resetting Rules,
// GameplaySession, presentation history, or the undo chain.
class OverworldMap {
public:
    [[nodiscard]] static OverworldMap load(
        const std::filesystem::path& overworldRoot,
        std::optional<OverworldDraftOverride> draftOverride =
            std::nullopt);

    [[nodiscard]] const OverworldLayout& layout() const { return layout_; }
    [[nodiscard]] const Level& level() const { return level_; }
    [[nodiscard]] uint64_t fingerprint() const { return fingerprint_; }
    [[nodiscard]] GridPosition normalizationOffset() const
    {
        return normalizationOffset_;
    }
    [[nodiscard]] OverworldScreenId startScreen() const
    {
        return layout_.start.screen;
    }
    [[nodiscard]] const std::vector<OverworldScreenRuntime>& screens() const
    {
        return screens_;
    }
    [[nodiscard]] const std::vector<OverworldSelectorRuntime>& selectors() const
    {
        return selectors_;
    }

    [[nodiscard]] const OverworldScreenRuntime* screen(
        OverworldScreenId id) const;
    [[nodiscard]] std::optional<OverworldScreenId> screenAt(
        GridPosition3 globalCell) const;
    [[nodiscard]] std::optional<GridPosition3> toGlobal(
        OverworldScreenId screenId,
        GridPosition3 localCell) const;
    [[nodiscard]] std::optional<GridPosition3> toLocal(
        OverworldScreenId screenId,
        GridPosition3 globalCell) const;
    [[nodiscard]] std::vector<OverworldScreenId> visibleNeighborhood(
        OverworldScreenId activeScreen) const;

    // Catalog-dependent selector checks live here so the content pipeline,
    // project store, and campaign setup can share exactly one ownership rule.
    void validatePuzzleSelectors(
        std::span<const int> puzzleScreenCounts,
        OverworldValidationMode mode) const;

private:
    OverworldLayout layout_;
    Level level_;
    GridPosition normalizationOffset_ {};
    uint64_t fingerprint_ = 0;
    std::vector<OverworldScreenRuntime> screens_;
    std::vector<OverworldSelectorRuntime> selectors_;
};

} // namespace sokoban
