#include "engine/SaveStore.hpp"

#include "engine/AtomicFile.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace sokoban {
namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (!stream.eof() && stream.fail()) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return contents.str();
}

std::string corruptSuffix()
{
    static std::atomic_uint64_t sequence = 0;
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return ".corrupt-" + std::to_string(timestamp) + "-" +
        std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

enum class ProfileFileState {
    Missing,
    Valid,
    Invalid,
};

ProfileFileState profileFileState(const std::filesystem::path& path)
{
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        throw std::system_error(error, "cannot inspect " + path.string());
    }
    if (!exists) {
        return ProfileFileState::Missing;
    }

    const bool regular = std::filesystem::is_regular_file(path, error);
    if (error) {
        throw std::system_error(error, "cannot inspect " + path.string());
    }
    if (!regular) {
        throw std::runtime_error("profile artifact is not a regular file: " +
            path.string());
    }

    const std::string contents = readFile(path);
    try {
        (void)decodePlayerProfile(contents);
        return ProfileFileState::Valid;
    } catch (const std::exception&) {
        return ProfileFileState::Invalid;
    }
}

void removeArtifact(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::remove(path, error);
    if (error) {
        throw std::system_error(error, "cannot remove stale save artifact " + path.string());
    }
}

} // namespace

SaveStore::SaveStore(
    std::filesystem::path root,
    std::string fileStem,
    ProfileSections sections)
    : root_(std::move(root))
    , primaryPath_(root_ / (fileStem + ".json"))
    , backupPath_(root_ / (fileStem + ".backup.json"))
    , sections_(sections)
{
}

std::filesystem::path SaveStore::preferencePath(
    std::string_view organization,
    std::string_view application)
{
    const std::string organizationText(organization);
    const std::string applicationText(application);
    char* path = SDL_GetPrefPath(organizationText.c_str(), applicationText.c_str());
    if (path == nullptr || *path == '\0') {
        SDL_free(path);
        throw std::runtime_error("SDL_GetPrefPath failed: preference directory is unavailable");
    }
    const std::filesystem::path result(path);
    SDL_free(path);
    return result;
}

SaveStore::LoadResult SaveStore::load()
{
    try {
        std::filesystem::create_directories(root_);
        const bool recoveredInterruptedWrite = recoverInterruptedWrites();

        if (std::filesystem::is_regular_file(primaryPath_)) {
            try {
                DecodedPlayerProfile decoded = decodePlayerProfile(readFile(primaryPath_));
                if (decoded.sourceFormat != currentPlayerProfileFormat) {
                    writePrimary(decoded.profile, true);
                    status_ = "Migrated player profile from format " +
                        std::to_string(decoded.sourceFormat) + ".";
                    return {
                        .profile = std::move(decoded.profile),
                        .disposition = LoadDisposition::Migrated,
                        .message = status_,
                    };
                }
                status_ = recoveredInterruptedWrite
                    ? "Recovered interrupted player profile write."
                    : "Loaded player profile.";
                return {
                    .profile = std::move(decoded.profile),
                    .disposition = recoveredInterruptedWrite
                        ? LoadDisposition::RecoveredInterruptedWrite
                        : LoadDisposition::Loaded,
                    .message = status_,
                };
            } catch (const std::exception&) {
                archiveCorruptFile(primaryPath_);
            }
        }

        if (std::filesystem::is_regular_file(backupPath_)) {
            try {
                DecodedPlayerProfile decoded = decodePlayerProfile(readFile(backupPath_));
                writePrimary(decoded.profile, false);
                status_ = "Recovered player profile from backup.";
                return {
                    .profile = std::move(decoded.profile),
                    .disposition = LoadDisposition::RecoveredBackup,
                    .message = status_,
                };
            } catch (const std::exception&) {
                archiveCorruptFile(backupPath_);
            }
        }

        const std::string primaryCorruptPrefix =
            primaryPath_.filename().string() + ".corrupt-";
        const std::string backupCorruptPrefix =
            backupPath_.filename().string() + ".corrupt-";
        const bool resetCorrupt = std::ranges::any_of(
            std::filesystem::directory_iterator(root_),
            [&](const std::filesystem::directory_entry& entry) {
                return entry.path().filename().string().starts_with(primaryCorruptPrefix) ||
                    entry.path().filename().string().starts_with(backupCorruptPrefix);
            });
        PlayerProfile profile;
        if (resetCorrupt) {
            // There was a save; archive-and-replace is recovery, so a valid
            // default file takes its place for diagnosis.
            writePrimary(profile, false);
            status_ = "Corrupt player saves were archived; defaults were restored.";
        } else {
            // A genuinely fresh start writes nothing: no file exists until
            // the player actually begins a game or changes a setting.
            status_ = "No player profile found; starting fresh.";
        }
        return {
            .profile = std::move(profile),
            .disposition = resetCorrupt
                ? LoadDisposition::ResetCorrupt
                : LoadDisposition::CreatedDefault,
            .message = status_,
        };
    } catch (const std::exception& error) {
        status_ = "Player profile storage unavailable: " + std::string(error.what());
        return {
            .profile = {},
            .disposition = LoadDisposition::StorageUnavailable,
            .message = status_,
        };
    }
}

bool SaveStore::recoverInterruptedWrites()
{
    const bool primaryRecovered = recoverInterruptedWrite(primaryPath_);
    const bool backupRecovered = recoverInterruptedWrite(backupPath_);
    return primaryRecovered || backupRecovered;
}

bool SaveStore::recoverInterruptedWrite(const std::filesystem::path& path)
{
    const std::filesystem::path temporary = path.string() + ".tmp";
    const std::filesystem::path displaced = path.string() + ".replace-old";

    const ProfileFileState live = profileFileState(path);
    const ProfileFileState temporaryState = profileFileState(temporary);
    const ProfileFileState displacedState = profileFileState(displaced);

    if (live == ProfileFileState::Valid) {
        // A committed profile is authoritative. Any artifacts are stale data
        // from an interrupted write that never reached the live path.
        removeArtifact(temporary);
        removeArtifact(displaced);
        return false;
    }

    const std::filesystem::path* recoverySource = nullptr;
    if (temporaryState == ProfileFileState::Valid) {
        // The temporary file is the newest candidate: it was produced for the
        // attempted replacement and was fully written before the old fallback
        // could have displaced the live file.
        recoverySource = &temporary;
    } else if (displacedState == ProfileFileState::Valid) {
        recoverySource = &displaced;
    }

    if (recoverySource == nullptr) {
        return false;
    }

    atomicFile::replace(path, *recoverySource);
    removeArtifact(temporary);
    removeArtifact(displaced);
    return true;
}

SaveStore::InspectionResult SaveStore::inspect() const
{
    try {
        const bool primaryExists = std::filesystem::exists(primaryPath_);
        const bool backupExists = std::filesystem::exists(backupPath_);
        if (!primaryExists && !backupExists) {
            return {
                .disposition = InspectionDisposition::Missing,
                .message = "No player profile found.",
            };
        }

        std::string primaryError;
        if (primaryExists) {
            if (!std::filesystem::is_regular_file(primaryPath_)) {
                return {
                    .disposition = InspectionDisposition::StorageUnavailable,
                    .message = "Player profile path is not a regular file.",
                };
            }
            try {
                DecodedPlayerProfile decoded =
                    decodePlayerProfile(readFile(primaryPath_));
                return {
                    .profile = std::move(decoded.profile),
                    .disposition = InspectionDisposition::PrimaryValid,
                    .message = "Player profile is ready.",
                };
            } catch (const std::exception& error) {
                primaryError = error.what();
            }
        }

        std::string backupError;
        if (backupExists) {
            if (!std::filesystem::is_regular_file(backupPath_)) {
                return {
                    .disposition = InspectionDisposition::StorageUnavailable,
                    .message = "Player profile backup path is not a regular file.",
                };
            }
            try {
                DecodedPlayerProfile decoded =
                    decodePlayerProfile(readFile(backupPath_));
                return {
                    .profile = std::move(decoded.profile),
                    .disposition = InspectionDisposition::BackupValid,
                    .message = "Player profile can be recovered from backup.",
                };
            } catch (const std::exception& error) {
                backupError = error.what();
            }
        }

        std::string message = "Player profile is corrupt";
        if (!primaryError.empty()) {
            message += ": " + primaryError;
        } else if (!backupError.empty()) {
            message += ": " + backupError;
        }
        return {
            .disposition = InspectionDisposition::Corrupt,
            .message = std::move(message),
        };
    } catch (const std::exception& error) {
        return {
            .disposition = InspectionDisposition::StorageUnavailable,
            .message = "Player profile storage unavailable: " +
                std::string(error.what()),
        };
    }
}

bool SaveStore::save(const PlayerProfile& profile)
{
    try {
        std::filesystem::create_directories(root_);
        writePrimary(profile, true);
        status_ = "Saved player profile.";
        return true;
    } catch (const std::exception& error) {
        status_ = "Player profile save failed: " + std::string(error.what());
        return false;
    }
}

void SaveStore::writePrimary(const PlayerProfile& profile, bool updateBackup)
{
    const std::string contents = profile.serialize(sections_);
    (void)decodePlayerProfile(contents);

    if (updateBackup && std::filesystem::is_regular_file(primaryPath_)) {
        const std::string previous = readFile(primaryPath_);
        (void)decodePlayerProfile(previous);
        atomicFile::write(backupPath_, previous);
    }
    atomicFile::write(primaryPath_, contents);
}

void SaveStore::archiveCorruptFile(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path)) {
        return;
    }
    const std::filesystem::path archive = path.string() + corruptSuffix();
    std::error_code error;
    std::filesystem::rename(path, archive, error);
    if (error) {
        throw std::runtime_error(
            "cannot archive corrupt save " + path.string() + ": " + error.message());
    }
}

} // namespace sokoban
