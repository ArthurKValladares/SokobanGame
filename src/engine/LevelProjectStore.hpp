#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace sokoban {

class LevelProjectStore {
public:
    using Mutation = std::function<void(const std::filesystem::path& stagingRoot)>;

    struct ManifestTransaction {
        std::filesystem::path sourcePath;
        std::optional<std::filesystem::path> runtimePath;
        Mutation mutation;
    };

    struct Result {
        bool succeeded = false;
        bool originalsPreserved = true;
        std::string message;
    };

    // Applies a mutation to a private clone, validates every active level and
    // screen, then publishes the source, optional runtime mirror, manifests,
    // and index as one recoverable operation. The mutation should throw on failure.
    [[nodiscard]] static Result transact(
        const std::filesystem::path& projectRoot,
        const std::optional<std::filesystem::path>& runtimeRoot,
        const Mutation& mutation,
        const std::optional<ManifestTransaction>& manifest =
            std::nullopt);

    // Validates and publishes a fresh runtime mirror of the source project.
    [[nodiscard]] static Result publishRuntime(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& runtimeRoot);

    // Resolves an interrupted publication before content is read or staged.
    // Throws if the transaction cannot be recovered.
    static void recover(const std::filesystem::path& projectRoot);
};

} // namespace sokoban
