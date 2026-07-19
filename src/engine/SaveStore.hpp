#pragma once

#include "engine/PlayerProfile.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace sokoban {

class SaveStore {
public:
    enum class LoadDisposition {
        Loaded,
        // Nothing on disk; defaults returned without writing any file.
        CreatedDefault,
        Migrated,
        RecoveredBackup,
        ResetCorrupt,
        StorageUnavailable,
    };

    struct LoadResult {
        PlayerProfile profile;
        LoadDisposition disposition = LoadDisposition::Loaded;
        std::string message;
    };

    // fileStem names the slot's files inside root (e.g. "profile" ->
    // profile.json / profile.backup.json). Slot 1 keeps the historical
    // "profile" stem so existing saves stay valid.
    explicit SaveStore(std::filesystem::path root, std::string fileStem = "profile");

    // SDL must already be initialized. SDL owns the platform-specific choice
    // of roaming/local preference storage.
    [[nodiscard]] static std::filesystem::path preferencePath(
        std::string_view organization,
        std::string_view application);

    [[nodiscard]] LoadResult load();
    [[nodiscard]] bool save(const PlayerProfile& profile);

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }
    [[nodiscard]] const std::filesystem::path& primaryPath() const { return primaryPath_; }
    [[nodiscard]] const std::filesystem::path& backupPath() const { return backupPath_; }
    [[nodiscard]] const std::string& status() const { return status_; }

private:
    void writePrimary(const PlayerProfile& profile, bool updateBackup);
    void archiveCorruptFile(const std::filesystem::path& path);

    std::filesystem::path root_;
    std::filesystem::path primaryPath_;
    std::filesystem::path backupPath_;
    std::string status_;
};

} // namespace sokoban
