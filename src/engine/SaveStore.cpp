#include "engine/SaveStore.hpp"

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

void replaceFile(
    const std::filesystem::path& destination,
    const std::filesystem::path& temporary)
{
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (!error) {
        return;
    }

    const std::filesystem::path displaced = destination.string() + ".replace-old";
    std::filesystem::remove(displaced, error);
    error.clear();
    std::filesystem::rename(destination, displaced, error);
    if (error) {
        throw std::runtime_error(
            "cannot prepare replacement for " + destination.string() + ": " + error.message());
    }

    error.clear();
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code rollbackError;
        std::filesystem::rename(displaced, destination, rollbackError);
        throw std::runtime_error(
            "cannot replace " + destination.string() + ": " + error.message());
    }
    std::filesystem::remove(displaced, error);
}

void writeAtomically(const std::filesystem::path& destination, std::string_view contents)
{
    const std::filesystem::path temporary = destination.string() + ".tmp";
    std::error_code cleanupError;
    std::filesystem::remove(temporary, cleanupError);

    try {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot open " + temporary.string());
        }
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        stream.close();
        if (!stream) {
            throw std::runtime_error("cannot write " + temporary.string());
        }
        replaceFile(destination, temporary);
    } catch (...) {
        std::filesystem::remove(temporary, cleanupError);
        throw;
    }
}

std::string corruptSuffix()
{
    static std::atomic_uint64_t sequence = 0;
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return ".corrupt-" + std::to_string(timestamp) + "-" +
        std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
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
                status_ = "Loaded player profile.";
                return {
                    .profile = std::move(decoded.profile),
                    .disposition = LoadDisposition::Loaded,
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
        writeAtomically(backupPath_, previous);
    }
    writeAtomically(primaryPath_, contents);
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
