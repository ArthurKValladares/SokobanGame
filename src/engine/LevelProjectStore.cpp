#include "engine/LevelProjectStore.hpp"

#include "engine/ContentPipeline.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/AtomicFile.hpp"
#include "engine/FileLock.hpp"
#include "engine/Level.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/OverworldMap.hpp"
#include "engine/PathText.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace sokoban {
namespace {

std::filesystem::path workingPath(
    const std::filesystem::path& root,
    std::string_view suffix)
{
    return root.parent_path() /
        (root.filename().string() + std::string(suffix));
}

std::filesystem::path runtimeFileWorkingPath(
    const std::filesystem::path& file,
    std::string_view suffix)
{
    const std::filesystem::path packageRoot = file.parent_path();
    return packageRoot.parent_path() /
        (packageRoot.filename().string() + "." + file.filename().string() +
         std::string(suffix));
}

void removeTree(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::remove_all(path, error);
    if (error) {
        throw std::runtime_error(
            "cannot remove " + path.string() + ": " + error.message());
    }
}

void renamePath(
    const std::filesystem::path& source,
    const std::filesystem::path& destination)
{
    std::error_code error;
    std::filesystem::rename(source, destination, error);
#ifdef _WIN32
    // A short-lived handle without delete sharing can block a Windows rename.
    // Retry only before the destination has appeared, while holding the
    // publication lock.
    for (const unsigned delayMilliseconds : { 10U, 20U, 40U, 80U }) {
        if (error.category() != std::system_category() ||
            (error.value() != ERROR_ACCESS_DENIED &&
                error.value() != ERROR_SHARING_VIOLATION)) {
            break;
        }
        Sleep(delayMilliseconds);
        std::error_code statusError;
        if (std::filesystem::symlink_status(destination, statusError).type() !=
            std::filesystem::file_type::not_found) {
            break;
        }
        std::filesystem::rename(source, destination, error);
    }
#endif
    if (error) {
        throw std::runtime_error(
            "cannot rename " + source.string() + " to " +
            destination.string() + ": " + error.message());
    }
}

void recoverWorkingTree(
    const std::filesystem::path& root,
    const std::filesystem::path& stage,
    const std::filesystem::path& backup)
{
    const bool rootExists = std::filesystem::exists(root);
    const bool backupExists = std::filesystem::exists(backup);
    if (!rootExists && backupExists) {
        renamePath(backup, root);
    } else if (rootExists && backupExists) {
        throw std::runtime_error(
            "ambiguous editor backup from an older version: " + backup.string());
    }
    removeTree(stage);
}

void copyDirectoryContents(
    const std::filesystem::path& source,
    const std::filesystem::path& destination)
{
    std::filesystem::create_directories(destination);
    if (!std::filesystem::exists(source)) {
        return;
    }
    if (!std::filesystem::is_directory(source)) {
        throw std::runtime_error(
            "level project root is not a directory: " + source.string());
    }

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(source)) {
        std::error_code error;
        std::filesystem::copy(
            entry.path(),
            destination / entry.path().filename(),
            std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::copy_symlinks |
                std::filesystem::copy_options::overwrite_existing,
            error);
        if (error) {
            throw std::runtime_error(
                "cannot stage " + entry.path().string() + ": " +
                error.message());
        }
    }
}

void copyFile(
    const std::filesystem::path& source,
    const std::filesystem::path& destination)
{
    std::error_code error;
    std::filesystem::copy_file(
        source,
        destination,
        std::filesystem::copy_options::overwrite_existing,
        error);
    if (error) {
        throw std::runtime_error(
            "cannot stage " + source.string() + ": " + error.message());
    }
}

PuzzleCatalog validateProject(const std::filesystem::path& root)
{
    PuzzleCatalog catalog = loadPuzzleCatalog(root);
    for (const auto& level : catalog.levels) {
        for (const auto& path : level.screens) {
            const Level screen = Level::loadFromFile(path);
            if (!screen.selectors().empty()) {
                throw std::runtime_error(
                    "screen selectors are only allowed in overworld content: " +
                    pathText(path));
            }
        }
    }

    const std::filesystem::path overworldRoot = root / "overworld";
    const std::filesystem::path layoutPath = overworldRoot / "layout.json";
    if (std::filesystem::is_regular_file(layoutPath)) {
        const OverworldMap overworld = OverworldMap::load(overworldRoot);
        overworld.validatePuzzleSelectors(
            catalog.screenCounts(),
            OverworldValidationMode::Structural);
    } else {
        const std::filesystem::path overworldPath = root / "overworld.scr";
        if (!std::filesystem::is_regular_file(overworldPath)) {
            return catalog;
        }
        const Level overworld = Level::loadFromFile(overworldPath);
        for (uint32_t z = 0; z < overworld.depth(); ++z) {
            for (uint32_t y = 0; y < overworld.height(); ++y) {
                for (uint32_t x = 0; x < overworld.width(); ++x) {
                    if (overworld.authoredTileAt(x, y, z) == TileType::End) {
                        throw std::runtime_error(
                            "end tiles are not allowed in overworld.scr");
                    }
                }
            }
        }
        for (const Level::ScreenSelector& selector : overworld.selectors()) {
            if (!selector.target) {
                continue;
            }
            if (!catalog.findScreen(*selector.target)) {
                throw std::runtime_error(
                    "selector " + std::to_string(selector.id) +
                    " targets a missing screen in " +
                    pathText(overworldPath));
            }
        }
    }
    return catalog;
}

void prepareRuntimeMirror(
    const PuzzleCatalog& catalog,
    const std::filesystem::path& projectStage,
    const std::filesystem::path& runtimeStage)
{
    std::filesystem::create_directories(runtimeStage);
    for (const auto& level : catalog.levels) {
        std::error_code error;
        std::filesystem::copy(
            level.directory,
            runtimeStage / level.directory.filename(),
            std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::copy_symlinks |
                std::filesystem::copy_options::overwrite_existing,
            error);
        if (error) {
            throw std::runtime_error(
                "cannot prepare runtime level mirror: " + error.message());
        }
    }
    const std::filesystem::path overworldRoot = projectStage / "overworld";
    if (std::filesystem::is_directory(overworldRoot)) {
        const std::filesystem::path runtimeOverworld =
            runtimeStage / "overworld";
        std::filesystem::create_directories(runtimeOverworld);
        const OverworldLayout layout = loadOverworldLayout(
            overworldRoot / "layout.json");
        std::vector<std::filesystem::path> files {
            std::filesystem::path("layout.json"),
        };
        for (const OverworldScreenSpec& screen : layout.screens) {
            files.push_back(screen.file);
        }
        for (const std::filesystem::path& file : files) {
            std::error_code error;
            std::filesystem::copy_file(
                overworldRoot / file,
                runtimeOverworld / file,
                std::filesystem::copy_options::overwrite_existing,
                error);
            if (error) {
                throw std::runtime_error(
                    "cannot prepare runtime overworld mirror: " +
                    error.message());
            }
        }
    } else {
        const std::filesystem::path overworld = projectStage / "overworld.scr";
        if (std::filesystem::is_regular_file(overworld)) {
            std::error_code error;
            std::filesystem::copy_file(
                overworld,
                runtimeStage / "overworld.scr",
                std::filesystem::copy_options::overwrite_existing,
                error);
            if (error) {
                throw std::runtime_error(
                    "cannot prepare runtime overworld mirror: " +
                    error.message());
            }
        }
    }
}

using Json = nlohmann::json;
namespace fs = std::filesystem;

struct Publication {
    fs::path destination;
    fs::path stage;
    fs::path backup;
    bool outsidePackage = false;
    bool hadOriginal = false;
};

fs::path publicationLockPath(const fs::path& projectRoot)
{
    return workingPath(projectRoot, ".editor-transaction.lock");
}

fs::path normalizedDestination(const fs::path& path)
{
    if (path.empty()) {
        throw std::runtime_error("invalid level publication destination");
    }
    fs::path normalized = fs::absolute(path).lexically_normal();
    if (normalized.filename().empty()) {
        normalized = normalized.parent_path();
    }
    if (normalized.empty() || !normalized.has_filename() ||
        normalized.filename() == "." || normalized.filename() == "..") {
        throw std::runtime_error("invalid level publication destination");
    }
    return fs::weakly_canonical(normalized.parent_path()) / normalized.filename();
}

Publication publicationFor(const fs::path& path, bool outsidePackage = false)
{
    const fs::path destination = normalizedDestination(path);
    const auto working = outsidePackage ? runtimeFileWorkingPath : workingPath;
    return { destination, working(destination, ".editor-stage"),
        working(destination, ".editor-backup"), outsidePackage };
}

fs::path journalPath(const fs::path& projectRoot)
{
    return workingPath(projectRoot, ".editor-transaction.json");
}

bool within(const fs::path& parent, const fs::path& path)
{
    const fs::path relative = path.lexically_relative(parent);
    return !relative.empty() && *relative.begin() != "..";
}

void validatePublicationPaths(
    const fs::path& journal, const std::vector<Publication>& files)
{
    std::vector<fs::path> paths { journal };
    for (const Publication& file : files) {
        for (const fs::path* path : { &file.destination, &file.stage, &file.backup }) {
            if (fs::is_symlink(*path) ||
                std::ranges::any_of(paths, [&](const fs::path& existing) {
                    return within(existing, *path) || within(*path, existing);
                })) {
                throw std::runtime_error("unsafe level publication path: " + path->string());
            }
            paths.push_back(*path);
        }
    }
}

std::string pathText(const fs::path& path)
{
    const auto utf8 = path.generic_u8string();
    return { utf8.begin(), utf8.end() };
}

Json publicationJournal(
    const fs::path& projectRoot, const std::vector<Publication>& files)
{
    Json entries = Json::array();
    for (const Publication& file : files) {
        entries.push_back({
            { "destination", pathText(file.destination) },
            { "outsidePackage", file.outsidePackage },
            { "hadOriginal", file.hadOriginal },
        });
    }
    return { { "version", 1 }, { "project", pathText(projectRoot) },
        { "committed", false }, { "entries", std::move(entries) } };
}

// Every original is either still in place or in its backup. Moving an installed
// replacement back into staging before restoring its backup makes rollback
// repeatable if the editor stops while recovering.
void rollBack(const Publication& file)
{
    if (fs::exists(file.backup)) {
        if (!file.hadOriginal &&
            (!fs::is_directory(file.backup) || !fs::is_empty(file.backup))) {
            throw std::runtime_error("invalid editor backup marker: " + file.backup.string());
        }
        if (fs::exists(file.destination)) {
            renamePath(file.destination, file.stage);
        }
        if (file.hadOriginal) {
            renamePath(file.backup, file.destination);
        }
    } else if (file.hadOriginal && !fs::exists(file.destination)) {
        throw std::runtime_error("original unavailable: " + file.destination.string());
    }
}

enum class Recovery { None, RolledBack, Committed };

Recovery recoverJournal(const fs::path& projectRoot)
{
    const fs::path path = journalPath(projectRoot);
    if (!fs::exists(path)) {
        return Recovery::None;
    }
    if (fs::is_symlink(path) || !fs::is_regular_file(path)) {
        throw std::runtime_error("invalid editor publication journal: " + path.string());
    }
    std::ifstream stream(path, std::ios::binary);
    const Json journal = Json::parse(stream);
    stream.close();
    const Json& entries = journal.at("entries");
    if (journal.at("version") != 1 || journal.at("project") != pathText(projectRoot) ||
        !entries.is_array() || entries.empty() || entries.size() > 5) {
        throw std::runtime_error("invalid editor publication journal: " + path.string());
    }
    const bool committed = journal.at("committed").get<bool>();
    std::vector<Publication> files;
    for (const Json& entry : entries) {
        const auto text = entry.at("destination").get<std::string>();
        const fs::path destination(std::u8string(text.begin(), text.end()));
        if (!destination.is_absolute()) {
            throw std::runtime_error("editor publication journal contains a relative path");
        }
        Publication file = publicationFor(
            destination, entry.at("outsidePackage").get<bool>());
        if (file.destination != destination) {
            throw std::runtime_error("editor publication destination changed: " + text);
        }
        file.hadOriginal = entry.at("hadOriginal").get<bool>();
        files.push_back(std::move(file));
    }
    validatePublicationPaths(path, files);
    if (!committed) {
        for (auto file = files.rbegin(); file != files.rend(); ++file) {
            rollBack(*file);
        }
    }
    for (const Publication& file : files) {
        removeTree(file.stage);
        removeTree(file.backup);
        if (file.outsidePackage && file.destination.filename() == "content.index") {
            const fs::path temporary = file.destination.parent_path() / "content.index.tmp";
            if (fs::is_regular_file(temporary)) {
                fs::remove(temporary);
            }
        }
    }
    fs::remove(path);
    return committed ? Recovery::Committed : Recovery::RolledBack;
}

LevelProjectStore::Result publishProject(
    const fs::path& requestedProject,
    const std::optional<fs::path>& runtimeRoot,
    const LevelProjectStore::Mutation* mutation,
    const std::optional<LevelProjectStore::ManifestTransaction>& manifest)
{
    fs::path projectRoot;
    std::optional<FileLock> lock;
    std::vector<Publication> files;
    bool originalsKnown = false;
    bool stagesOwned = false;
    bool committed = false;
    try {
        projectRoot = normalizedDestination(requestedProject);
        lock.emplace(publicationLockPath(projectRoot), "editor publication");
        (void)recoverJournal(projectRoot);
        originalsKnown = true;
        if (manifest && manifest->runtimePath && !runtimeRoot) {
            throw std::runtime_error("runtime manifest requires a runtime level mirror");
        }
        if (runtimeRoot) {
            const fs::path runtime = normalizedDestination(*runtimeRoot);
            if (within(projectRoot, runtime) || within(runtime, projectRoot)) {
                throw std::runtime_error("runtime mirror overlaps the source level tree");
            }
        }

        const auto add = [&](const fs::path& path, bool outsidePackage = false) {
            files.push_back(publicationFor(path, outsidePackage));
            return files.back().stage;
        };
        const fs::path projectStage = mutation ? add(projectRoot) : projectRoot;
        const fs::path sourceManifestStage = manifest ? add(manifest->sourcePath) : fs::path {};
        const fs::path runtimeStage = runtimeRoot
            ? add(*runtimeRoot, fs::is_regular_file(
                  runtimeRoot->parent_path() / "content.index")) : fs::path {};
        const fs::path runtimeManifestStage = manifest && manifest->runtimePath
            ? add(*manifest->runtimePath, true) : fs::path {};
        const fs::path indexPath = runtimeRoot
            ? runtimeRoot->parent_path() / "content.index" : fs::path {};
        const fs::path indexStage = runtimeRoot && fs::exists(indexPath)
            ? add(indexPath, true) : fs::path {};

        validatePublicationPaths(journalPath(projectRoot), files);
        originalsKnown = false;
        for (const Publication& file : files) {
            recoverWorkingTree(file.destination, file.stage, file.backup);
        }
        originalsKnown = true;
        stagesOwned = true;
        if (mutation) {
            copyDirectoryContents(projectRoot, projectStage);
            (*mutation)(projectStage);
        }
        const auto levels = validateProject(projectStage);
        if (runtimeRoot) {
            prepareRuntimeMirror(levels, projectStage, runtimeStage);
            if (!mutation) {
                // Validate the actual copy, including multi-file overworld edits.
                const auto mirrored = validateProject(runtimeStage);
                const bool indexed = fs::is_regular_file(
                    runtimeRoot->parent_path() / "content.index");
                if (indexed && (mirrored.levels.empty() ||
                    (!fs::is_regular_file(runtimeStage / "overworld/layout.json") &&
                     !fs::is_regular_file(runtimeStage / "overworld.scr")))) {
                    throw std::runtime_error(
                        "runtime level mirror requires a playable level and an overworld");
                }
            }
        }
        if (manifest) {
            copyFile(manifest->sourcePath, sourceManifestStage);
            manifest->mutation(sourceManifestStage);
            (void)AssetManifest::loadFromFile(sourceManifestStage);
            if (manifest->runtimePath) {
                copyFile(sourceManifestStage, runtimeManifestStage);
            }
        }
        if (!indexStage.empty()) {
            copyFile(indexPath, indexStage);
        } else if (runtimeRoot && fs::exists(indexPath)) {
            throw std::runtime_error("runtime content index appeared while staging");
        }
        for (Publication& file : files) {
            file.hadOriginal = fs::exists(file.destination);
        }

        Json journal = publicationJournal(projectRoot, files);
        const fs::path journalFile = journalPath(projectRoot);
        atomicFile::write(journalFile, journal.dump());
        for (const Publication& file : files) {
            if (file.hadOriginal) {
                renamePath(file.destination, file.backup);
            } else {
                // Record ownership before installing a previously absent path.
                if (!fs::create_directory(file.backup)) {
                    throw std::runtime_error("editor backup already exists: " + file.backup.string());
                }
            }
            renamePath(file.stage, file.destination);
        }
        if (runtimeRoot) {
            (void)refreshContentPackageIndex(runtimeRoot->parent_path());
        }
        journal["committed"] = true;
        atomicFile::write(journalFile, journal.dump());
        committed = true;
        (void)recoverJournal(projectRoot);
        return { .succeeded = true };
    } catch (const std::exception& error) {
        std::string message = error.what();
        if (committed) {
            return { .succeeded = true, .message = std::move(message) };
        }
        if (!stagesOwned) {
            return { .originalsPreserved = originalsKnown,
                .message = std::move(message) };
        }
        try {
            const Recovery recovery = recoverJournal(projectRoot);
            if (recovery == Recovery::Committed) {
                return { .succeeded = true, .message = std::move(message) };
            }
            if (recovery == Recovery::None) {
                for (const Publication& file : files) {
                    removeTree(file.stage);
                }
            }
        } catch (const std::exception& recoveryError) {
            message += "; recovery incomplete: ";
            message += recoveryError.what();
            return { .originalsPreserved = false, .message = std::move(message) };
        }
        return { .message = std::move(message) };
    }
}

} // namespace

LevelProjectStore::Result LevelProjectStore::transact(
    const std::filesystem::path& projectRoot,
    const std::optional<std::filesystem::path>& runtimeRoot,
    const Mutation& mutation,
    const std::optional<ManifestTransaction>& manifest)
{
    return publishProject(projectRoot, runtimeRoot, &mutation, manifest);
}

LevelProjectStore::Result LevelProjectStore::publishRuntime(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& runtimeRoot)
{
    return publishProject(projectRoot, runtimeRoot, nullptr, std::nullopt);
}

void LevelProjectStore::recover(const std::filesystem::path& projectRoot)
{
    const fs::path project = normalizedDestination(projectRoot);
    if (fs::exists(journalPath(project))) {
        const FileLock lock(publicationLockPath(project), "editor publication");
        (void)recoverJournal(project);
    }
}

} // namespace sokoban
