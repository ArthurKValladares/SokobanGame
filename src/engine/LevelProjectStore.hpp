#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace sokoban {

class LevelProjectStore {
public:
    using Mutation = std::function<void(const std::filesystem::path& stagingRoot)>;

    struct Result {
        bool succeeded = false;
        bool originalsPreserved = true;
        std::string message;
    };

    // Applies a mutation to a private clone, validates every active level and
    // screen, prepares the optional runtime mirror, and commits both roots as
    // one rollback-capable operation. The mutation should throw on failure.
    [[nodiscard]] static Result transact(
        const std::filesystem::path& projectRoot,
        const std::optional<std::filesystem::path>& runtimeRoot,
        const Mutation& mutation);
};

} // namespace sokoban
