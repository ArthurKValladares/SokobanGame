#pragma once

#include "engine/OverworldMap.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>

namespace sokoban {

enum class MakerRecoveryKind : uint8_t { Puzzle, Overworld };

struct MakerRecoverySnapshot {
    std::string draftName;
    std::optional<std::string> draftId;
    std::optional<OverworldScreenId> activeScreen;
    std::variant<Level::Definition, OverworldDraftOverride> document;

    [[nodiscard]] MakerRecoveryKind kind() const
    {
        return std::holds_alternative<OverworldDraftOverride>(document) ? MakerRecoveryKind::Overworld
                                                                      : MakerRecoveryKind::Puzzle;
    }
};

// The working copies have their own slots and never replace named drafts.
class MakerRecoveryStore {
public:
    explicit MakerRecoveryStore(std::filesystem::path root);

    [[nodiscard]] std::optional<MakerRecoverySnapshot> load(MakerRecoveryKind kind) const;
    void save(const MakerRecoverySnapshot& snapshot) const;
    void discard(MakerRecoveryKind kind) const;

private:
    [[nodiscard]] std::filesystem::path slot(MakerRecoveryKind kind) const;

    std::filesystem::path root_;
};

} // namespace sokoban
