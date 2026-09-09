#pragma once

#include "engine/PlayerProfile.hpp"

#include <array>
#include <filesystem>
#include <optional>
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
        RecoveredInterruptedWrite,
        RecoveredBackup,
        // The profile decoded successfully, but migration or backup repair
        // could not be persisted. `profile` contains the usable data and the
        // original valid file remains available for a later retry.
        LoadedWithPersistenceError,
        // The file belongs to a profile format this build cannot decode. It is
        // preserved in place and `profile` contains defaults.
        UnsupportedFormat,
        ResetCorrupt,
        StorageUnavailable,
    };

    struct LoadResult {
        PlayerProfile profile;
        LoadDisposition disposition = LoadDisposition::Loaded;
        std::string message;
    };

    enum class InspectionDisposition {
        Missing,
        PrimaryValid,
        BackupValid,
        UnsupportedFormat,
        Corrupt,
        StorageUnavailable,
    };

    struct InspectionResult {
        std::optional<PlayerProfile> profile;
        InspectionDisposition disposition = InspectionDisposition::Missing;
        std::string message;
    };

    struct DeleteResult {
        bool succeeded = false;
        // A committed deletion can leave cleanup for a later load/save when
        // an artifact could not be removed. The deletion marker still keeps
        // every recovery candidate ineligible.
        bool cleanupPending = false;
        std::string message;
    };

    // fileStem names the slot's files inside root (e.g. "profile" ->
    // profile.json / profile.backup.json). Slot 1 keeps the historical
    // "profile" stem so existing saves stay valid. `sections` selects which
    // profile sections this store writes (slot stores write progress only,
    // the shared settings store settings only).
    explicit SaveStore(
        std::filesystem::path root,
        const std::string& fileStem = "profile",
        ProfileSections sections = ProfileSections::All);

    // SDL owns the platform-specific choice of roaming/local preference
    // storage. This query does not require the video subsystem to be running,
    // allowing diagnostics to start before a window is created.
    [[nodiscard]] static std::filesystem::path preferencePath(
        std::string_view organization,
        std::string_view application);

    [[nodiscard]] LoadResult load();
    // Examines primary and backup files without migrating, recovering,
    // archiving, replacing, or creating anything on disk.
    [[nodiscard]] InspectionResult inspect() const;
    [[nodiscard]] bool save(const PlayerProfile& profile);
    [[nodiscard]] DeleteResult deleteProfile();

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }
    [[nodiscard]] const std::filesystem::path& primaryPath() const { return primaryPath_; }
    [[nodiscard]] const std::filesystem::path& backupPath() const { return backupPath_; }
    [[nodiscard]] const std::filesystem::path& deletionMarkerPath() const
    {
        return deletionMarkerPath_;
    }
    [[nodiscard]] std::array<std::filesystem::path, 6>
    recoverableArtifactPaths() const;
    [[nodiscard]] const std::string& status() const { return status_; }

private:
    [[nodiscard]] bool recoverInterruptedWrites();
    [[nodiscard]] bool recoverInterruptedWrite(const std::filesystem::path& path);
    void writePrimary(const PlayerProfile& profile, bool updateBackup);
    void archiveCorruptFile(const std::filesystem::path& path);
    [[nodiscard]] bool deletionMarked() const;
    [[nodiscard]] std::string removeRecoverableArtifacts() const;

    std::filesystem::path root_;
    std::filesystem::path primaryPath_;
    std::filesystem::path backupPath_;
    std::filesystem::path deletionMarkerPath_;
    ProfileSections sections_ = ProfileSections::All;
    std::string status_;
};

} // namespace sokoban
