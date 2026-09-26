#pragma once

#include "engine/Level.hpp"
#include "engine/Solution.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Keeps `solutions/` in step with the screens under `levels/`.
//
// Each screen whose content has a recording gets exactly one file,
// `level<L>-screen<S>.solution`, holding the shortest recording known for that
// content. Recordings of content that is not on disk (an editor draft that
// has not been saved yet) wait in `solutions/drafts/<digest>.solution` and
// move into place once a screen with that content appears. Recordings whose
// screen changed stay where they are, so the replay test can point them out,
// unless their file name is needed for the current screen; then they move to
// `drafts/` instead of being overwritten, and come back if the edit is undone.
//
// Nothing here touches the game or GPU, so it can run on a worker thread.
namespace sokoban::solution {

struct StoreChange {
    enum class Kind : std::uint8_t {
        Saved,          // a new recording was written
        KeptExisting,   // an equal or shorter recording was already stored
        Moved,          // an existing recording moved into or out of place
        NotRecorded,    // the solve did not replay; `message` says why
    };
    Kind kind = Kind::Saved;
    std::filesystem::path file;
    std::size_t steps = 0;
    std::string message;
};

struct SolveToStore {
    // For the file's `recorded-for` line when no screen matches.
    std::string name;
    Level::Definition definition;
    std::vector<Input> inputs;
};

// Brings the directory in line with the rules above. With a solve, records
// it first and offers it as a candidate. Throws on I/O errors.
[[nodiscard]] std::vector<StoreChange> reconcileStore(
    const std::filesystem::path& levelsRoot,
    const std::filesystem::path& solutionsDir,
    const std::optional<SolveToStore>& solve = std::nullopt);

} // namespace sokoban::solution
