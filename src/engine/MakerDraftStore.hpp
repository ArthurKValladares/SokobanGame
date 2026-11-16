#pragma once

#include "engine/Level.hpp"
#include "engine/OverworldMap.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

class MakerDraftStore {
public:
    static constexpr std::size_t maxNameBytes = 64;

    class NameExists : public std::runtime_error {
    public:
        NameExists() : std::runtime_error("A draft with this name already exists.") {}
    };

    struct DraftVersion {
        std::filesystem::file_time_type modified;
        std::uintmax_t bytes;

        bool operator==(const DraftVersion&) const = default;
    };

    struct SavedLevel {
        std::string id;
        std::string name;
        std::optional<std::string> legacyName;

        bool operator==(const SavedLevel&) const = default;
    };

    explicit MakerDraftStore(std::filesystem::path root);

    [[nodiscard]] std::vector<std::string> list() const;
    [[nodiscard]] std::vector<SavedLevel> listLevels() const;
    [[nodiscard]] std::optional<SavedLevel> findLevelByName(std::string_view name) const;
    [[nodiscard]] std::optional<SavedLevel> findLevelById(std::string_view id) const;
    // Legacy links resolve against the old name's original identity before
    // looking for a current display name. An explicit ID never falls back.
    [[nodiscard]] std::optional<SavedLevel> resolveLevel(std::optional<std::string_view> id,
        std::optional<std::string_view> legacyName) const;
    SavedLevel save(std::string_view name, const Level::Definition& definition,
        bool overwrite = false) const;
    SavedLevel saveById(std::string_view id, const Level::Definition& definition) const;
    SavedLevel renameLevel(std::string_view id, std::string_view name) const;
    [[nodiscard]] Level::Definition load(std::string_view name) const;
    [[nodiscard]] Level::Definition loadById(std::string_view id) const;
    [[nodiscard]] std::optional<DraftVersion> draftVersion(std::string_view name) const;
    [[nodiscard]] std::optional<DraftVersion> draftVersionById(std::string_view id) const;

    [[nodiscard]] std::vector<std::string> listOverworlds() const;
    void saveOverworld(std::string_view name, const OverworldDraftOverride& draft,
        bool overwrite = false) const;
    [[nodiscard]] OverworldDraftOverride loadOverworld(std::string_view name) const;

private:
    std::filesystem::path root_;
};

} // namespace sokoban
