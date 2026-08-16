#include "engine/ContentPipeline.hpp"
#include "engine/render/SelectorRenderConfig.hpp"

#include "engine/AnimationCatalog.hpp"
#include "engine/render/GltfMesh.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/Level.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/OverworldMap.hpp"
#include "engine/TileThumbnailBake.hpp"
#include "engine/TileTypes.hpp"
#include "engine/ui/UiConfig.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace sokoban {
namespace {

constexpr std::array<std::string_view, 11> requiredShaders {
    "triangle.vert.glsl.spv",
    "triangle.frag.glsl.spv",
    "water.frag.glsl.spv",
    "mirror_energy.frag.glsl.spv",
    "ground_splat.frag.glsl.spv",
    "shadow.vert.glsl.spv",
    "model.vert.glsl.spv",
    "model_shadow.vert.glsl.spv",
    "fullscreen.vert.glsl.spv",
    "ssao.frag.glsl.spv",
    "ssao_composite.frag.glsl.spv",
};

std::filesystem::path normalizedRelativePath(
    const std::filesystem::path& path,
    std::string_view label)
{
    const std::filesystem::path normalized = path.lexically_normal();
    if (normalized.empty() || normalized.is_absolute() || normalized.has_root_path()) {
        throw std::runtime_error(std::string(label) + " must be a non-empty relative path: " + path.string());
    }
    for (const auto& component : normalized) {
        if (component == "..") {
            throw std::runtime_error(std::string(label) + " escapes its content root: " + path.string());
        }
    }
    return normalized;
}

std::filesystem::path canonicalRoot(const std::filesystem::path& root, std::string_view label)
{
    std::error_code error;
    const std::filesystem::path result = std::filesystem::weakly_canonical(root, error);
    if (error || !std::filesystem::is_directory(result)) {
        throw std::runtime_error(std::string(label) + " directory is unavailable: " + root.string());
    }
    return result;
}

bool isWithin(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    const std::filesystem::path relative = candidate.lexically_relative(root);
    if (relative.empty() && candidate != root) {
        return false;
    }
    return relative.empty() || *relative.begin() != "..";
}

std::filesystem::path sourceFile(
    const std::filesystem::path& root,
    const std::filesystem::path& relative,
    std::string_view label)
{
    const std::filesystem::path safeRelative = normalizedRelativePath(relative, label);
    std::error_code error;
    const std::filesystem::path candidate = std::filesystem::weakly_canonical(root / safeRelative, error);
    if (error || !isWithin(root, candidate)) {
        throw std::runtime_error(std::string(label) + " escapes its content root: " + relative.string());
    }
    if (!std::filesystem::is_regular_file(candidate)) {
        throw std::runtime_error(std::string(label) + " is missing: " + candidate.string());
    }
    return candidate;
}

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot read content file: " + path.string());
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool isNoticeFile(const std::filesystem::path& path)
{
    const std::string name = lowercase(path.filename().string());
    return name == "license" || name == "license.txt" || name == "ofl.txt" || name == "copying" ||
        name == "copying.txt" || name == "readme" || name == "readme.txt" ||
        name == "readme.html" || name == "copyright" || name == "copyright.txt";
}

std::vector<std::filesystem::path> gltfDependencies(const std::filesystem::path& gltf)
{
    if (lowercase(gltf.extension().string()) != ".gltf") {
        return {};
    }

    const std::string json = readTextFile(gltf);
    const std::regex uriPattern(R"uri("uri"\s*:\s*"([^"]+)")uri");
    std::vector<std::filesystem::path> dependencies;
    for (std::sregex_iterator it(json.begin(), json.end(), uriPattern), end; it != end; ++it) {
        const std::string uri = (*it)[1].str();
        if (uri.starts_with("data:")) {
            continue;
        }
        if (uri.find('\\') != std::string::npos || uri.find('%') != std::string::npos ||
            uri.find("://") != std::string::npos) {
            throw std::runtime_error("unsupported external glTF URI in " + gltf.string() + ": " + uri);
        }
        dependencies.emplace_back(uri);
    }
    return dependencies;
}

class InventoryBuilder {
public:
    explicit InventoryBuilder(ContentSourceRoots roots)
        : roots_ {
              canonicalRoot(roots.assets, "asset source"),
              canonicalRoot(roots.levels, "level source"),
              canonicalRoot(roots.shaders, "shader source"),
          }
    {
    }

    ContentInventory build()
    {
        addManifestAssets();
        addTileThumbnails();
        addLevels();
        addShaders();

        ContentInventory inventory;
        inventory.files.reserve(files_.size());
        for (const auto& [key, file] : files_) {
            (void)key;
            const std::uintmax_t size = std::filesystem::file_size(file.source);
            inventory.totalBytes += size;
            inventory.files.push_back({ file.source, file.destination, size });
        }
        return inventory;
    }

private:
    void addFile(
        const std::filesystem::path& sourceRoot,
        const std::filesystem::path& sourceRelative,
        const std::filesystem::path& destination,
        std::string_view label,
        bool includeNotices = true)
    {
        const std::filesystem::path safeDestination = normalizedRelativePath(destination, "content destination");
        const std::filesystem::path source = sourceFile(sourceRoot, sourceRelative, label);
        std::string destinationKey = safeDestination.generic_string();
#ifdef _WIN32
        destinationKey = lowercase(std::move(destinationKey));
#endif
        const auto [it, inserted] = files_.emplace(
            destinationKey,
            PendingFile { source, safeDestination });
        if (!inserted && it->second.source != source) {
            throw std::runtime_error(
                "multiple content files map to " + safeDestination.generic_string());
        }
        if (includeNotices) {
            addNotices(sourceRoot, source.parent_path());
        }
    }

    void addNotices(const std::filesystem::path& root, std::filesystem::path directory)
    {
        while (isWithin(root, directory) && directory != root) {
            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                if (entry.is_regular_file() && isNoticeFile(entry.path())) {
                    const std::filesystem::path relative = entry.path().lexically_relative(root);
                    addFile(root, relative, relative, "content notice", false);
                }
            }
            directory = directory.parent_path();
        }
    }

    void addAssetPath(const std::filesystem::path& relative, std::string_view label)
    {
        addFile(roots_.assets, relative, relative, label);
        const std::filesystem::path absolute = sourceFile(roots_.assets, relative, label);
        for (const auto& dependency : gltfDependencies(absolute)) {
            const std::filesystem::path dependencyRelative = relative.parent_path() / dependency;
            addFile(roots_.assets, dependencyRelative, dependencyRelative, "glTF dependency");
        }
    }

    void addManifestAssets()
    {
        addFile(roots_.assets, "manifest.json", "manifest.json", "asset manifest");
        addFile(
            roots_.assets,
            "animation_catalog.json",
            "animation_catalog.json",
            "animation catalog");
        addAssetPath(std::filesystem::path(config::uiFontPath), "UI font");
        addAssetPath(
            std::filesystem::path(config::titleBackgroundPath),
            "title background");
        manifest_ = AssetManifest::loadFromFile(
            roots_.assets / "manifest.json");
        const AssetManifest& manifest = *manifest_;
        // Validation is part of staging: missing code-owned use IDs, stale
        // IDs, or clip names that no longer exist fail the build instead of
        // reaching a shipped runtime.
        const AnimationCatalog animationCatalog =
            AnimationCatalog::loadFromFile(
            roots_.assets / "animation_catalog.json", manifest);

        for (std::size_t i = 0; i < manifest.animations().size(); ++i) {
            const RenderAnimation animation { static_cast<uint32_t>(i + 1) };
            const AssetManifest::Animation& definition =
                manifest.animations()[i];
            const GltfAnimationClip clip = loadGltfAnimationClip(
                sourceFile(
                    roots_.assets,
                    definition.path,
                    "animation '" + definition.name + "'"),
                animationIndexFromManifestClip(definition.clip));
            if (std::abs(
                    clip.durationSeconds -
                    animationCatalog.clipDuration(animation)) > 0.0001f) {
                throw std::runtime_error(
                    "animation catalog duration for '" + definition.name +
                    "' is stale: catalog=" +
                    std::to_string(
                        animationCatalog.clipDuration(animation)) +
                    ", source=" + std::to_string(clip.durationSeconds));
            }
        }

        for (const auto& texture : manifest.textures()) {
            addAssetPath(texture.path, "texture '" + texture.name + "'");
        }
        for (const auto& model : manifest.models()) {
            addAssetPath(model.path, "model '" + model.name + "'");
            for (const auto& attachment : model.attachments) {
                addAssetPath(
                    attachment.path,
                    "attachment on model '" + model.name + "'");
            }
        }
        for (const auto& animation : manifest.animations()) {
            addAssetPath(animation.path, "animation '" + animation.name + "'");
        }
        for (const auto& soundSet : manifest.soundSets()) {
            for (const auto& file : soundSet.files) {
                addAssetPath(file, "sound set '" + soundSet.name + "'");
            }
        }
        for (const auto& music : manifest.musicTracks()) {
            addAssetPath(music.file, "music for level " + std::to_string(music.level));
        }
    }

    // Baked tile palette thumbnails.
    //
    // Nothing in the manifest names these - they are pictures for the editor,
    // not assets the game loads - so without this they were silently dropped:
    // staging wipes the output root and copies only what it was told about. A
    // bake writes into both the source tree and the staged root, so the palette
    // looked right until the next launch re-staged and removed them, and the
    // editor fell back to coloured squares even though the files were sitting
    // in assets/custom/thumbnails.
    //
    // Missing files are skipped rather than fatal, unlike manifest assets:
    // before the first bake there is nothing to copy, and a thumbnail that is
    // not there costs the palette a picture rather than breaking the game.
    // Notices are not collected for the same reason - these are files the bake
    // generates, not third-party content that arrives with a licence.
    void addTileThumbnails()
    {
        for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
            if (!tileThumbnails::shouldBake(definition.type)) {
                continue;
            }
            const std::filesystem::path relative =
                tileThumbnails::assetPathFor(definition.type);
            std::error_code error;
            if (!std::filesystem::is_regular_file(
                    roots_.assets / relative, error)) {
                continue;
            }
            addFile(roots_.assets, relative, relative, "tile thumbnail", false);
        }
    }

    void addLevels()
    {
        const std::regex levelPattern(R"(^level([0-9]+)$)");
        const std::regex screenPattern(R"(^screen([0-9]+)\.scr$)");
        std::map<int, std::set<int>> screens;

        if (!manifest_) {
            throw std::logic_error(
                "content manifest was not loaded before levels");
        }

        const auto validateDecorations = [&](
            const Level& level,
            const std::filesystem::path& path) {
            for (const Level::Decoration& decoration :
                 level.decorations()) {
                try {
                    (void)manifest_->modelIdByName(decoration.model);
                } catch (const std::exception&) {
                    throw std::runtime_error(
                        "level decoration references unknown manifest "
                        "model '" + decoration.model + "': " +
                        path.string());
                }
            }
        };

        const std::filesystem::path legacyOverworldPath =
            roots_.levels / "overworld.scr";
        const std::filesystem::path overworldRoot =
            roots_.levels / "overworld";
        const std::filesystem::path layoutPath =
            overworldRoot / "layout.json";
        const bool composedOverworld =
            std::filesystem::is_regular_file(layoutPath);
        std::optional<OverworldMap> overworldMap;
        std::optional<Level> legacyOverworld;
        const Level* overworld = nullptr;
        const std::filesystem::path* overworldDiagnosticPath = nullptr;
        if (composedOverworld) {
            overworldMap = OverworldMap::load(overworldRoot);
            overworld = &overworldMap->level();
            overworldDiagnosticPath = &layoutPath;
            addFile(
                overworldRoot,
                "layout.json",
                std::filesystem::path("levels") / "overworld" /
                    "layout.json",
                "overworld layout");
            for (const OverworldScreenRuntime& screen :
                 overworldMap->screens()) {
                addFile(
                    overworldRoot,
                    screen.file,
                    std::filesystem::path("levels") / "overworld" /
                        screen.file,
                    "overworld component screen");
            }
        } else {
            if (!std::filesystem::is_regular_file(legacyOverworldPath)) {
                throw std::runtime_error(
                    "overworld screen or layout is missing: " +
                    legacyOverworldPath.string());
            }
            legacyOverworld = Level::loadFromFile(legacyOverworldPath);
            overworld = &*legacyOverworld;
            overworldDiagnosticPath = &legacyOverworldPath;
            addFile(
                roots_.levels,
                "overworld.scr",
                std::filesystem::path("levels") / "overworld.scr",
                "overworld screen");
        }

        validateDecorations(*overworld, *overworldDiagnosticPath);
        if (overworld->selectors().empty()) {
            throw std::runtime_error(
                "overworld must contain at least one screen selector: " +
                overworldDiagnosticPath->string());
        }
        for (uint32_t z = 0; z < overworld->depth(); ++z) {
            for (uint32_t y = 0; y < overworld->height(); ++y) {
                for (uint32_t x = 0; x < overworld->width(); ++x) {
                    if (overworld->authoredTileAt(x, y, z) == TileType::End) {
                        throw std::runtime_error(
                            "end tiles are not allowed in the overworld: " +
                            overworldDiagnosticPath->string());
                    }
                }
            }
        }
        try {
            for (std::string_view modelName : selectorRender::modelNames) {
                (void)manifest_->modelIdByName(modelName);
            }
        } catch (const std::exception&) {
            throw std::runtime_error(
                "overworld selectors require all flag A/B playable, solved, "
                "and unavailable manifest models");
        }
        for (const auto& levelDirectory : std::filesystem::directory_iterator(roots_.levels)) {
            if (!levelDirectory.is_directory() ||
                levelDirectory.path().filename() == "Deleted" ||
                (composedOverworld &&
                    levelDirectory.path().filename() == "overworld")) {
                continue;
            }
            std::smatch levelMatch;
            const std::string levelName = levelDirectory.path().filename().string();
            if (!std::regex_match(levelName, levelMatch, levelPattern)) {
                throw std::runtime_error("unexpected level directory: " + levelDirectory.path().string());
            }
            const int levelIndex = std::stoi(levelMatch[1].str());
            auto& levelScreens = screens[levelIndex];
            for (const auto& screenFile : std::filesystem::directory_iterator(levelDirectory.path())) {
                if (!screenFile.is_regular_file()) {
                    throw std::runtime_error("unexpected entry in level directory: " + screenFile.path().string());
                }
                std::smatch screenMatch;
                const std::string screenName = screenFile.path().filename().string();
                if (screenName == levelMetadataFilename) {
                    continue;
                }
                if (!std::regex_match(screenName, screenMatch, screenPattern)) {
                    throw std::runtime_error("unexpected level file: " + screenFile.path().string());
                }
                const int screenIndex = std::stoi(screenMatch[1].str());
                levelScreens.insert(screenIndex);
                const Level level = Level::loadFromFile(screenFile.path());
                if (!level.selectors().empty()) {
                    throw std::runtime_error(
                        "screen selectors are only allowed in overworld.scr: " +
                        screenFile.path().string());
                }
                validateDecorations(level, screenFile.path());
                const std::filesystem::path relative = screenFile.path().lexically_relative(roots_.levels);
                addFile(roots_.levels, relative, std::filesystem::path("levels") / relative, "level screen");
            }

            const std::filesystem::path metadataPath =
                levelDirectory.path() / levelMetadataFilename;
            (void)loadLevelMetadata(
                levelDirectory.path(),
                levelScreens.size());
            if (std::filesystem::exists(metadataPath)) {
                const std::filesystem::path relative =
                    metadataPath.lexically_relative(roots_.levels);
                addFile(
                    roots_.levels,
                    relative,
                    std::filesystem::path("levels") / relative,
                    "level metadata");
            }
        }

        if (screens.empty()) {
            throw std::runtime_error("no playable levels were found in " + roots_.levels.string());
        }
        int expectedLevel = 0;
        for (const auto& [level, levelScreens] : screens) {
            if (level != expectedLevel++) {
                throw std::runtime_error("level indices must be contiguous starting at level0");
            }
            if (levelScreens.empty()) {
                throw std::runtime_error(
                    "level" + std::to_string(level) + " contains no playable screens");
            }
            int expectedScreen = 0;
            for (const int screen : levelScreens) {
                if (screen != expectedScreen++) {
                    throw std::runtime_error(
                        "screen indices in level" + std::to_string(level) +
                        " must be contiguous starting at screen0");
                }
            }
        }

        if (overworldMap) {
            std::vector<int> screenCounts;
            screenCounts.reserve(screens.size());
            for (const auto& [level, levelScreens] : screens) {
                (void)level;
                screenCounts.push_back(
                    static_cast<int>(levelScreens.size()));
            }
            overworldMap->validatePuzzleSelectors(
                screenCounts, OverworldValidationMode::Production);
            return;
        }

        std::set<std::pair<int, int>> selectorTargets;
        for (const Level::ScreenSelector& selector : overworld->selectors()) {
            if (!selector.target) {
                throw std::runtime_error(
                    "overworld selector " + std::to_string(selector.id) +
                    " is unassigned: " + legacyOverworldPath.string());
            }
            const auto level = screens.find(selector.target->level);
            if (level == screens.end() ||
                !level->second.contains(selector.target->screen)) {
                throw std::runtime_error(
                    "overworld selector " + std::to_string(selector.id) +
                    " targets missing level " +
                    std::to_string(selector.target->level) + " screen " +
                    std::to_string(selector.target->screen) + ": " +
                    legacyOverworldPath.string());
            }
            selectorTargets.emplace(
                selector.target->level,
                selector.target->screen);
        }
        for (const auto& [level, levelScreens] : screens) {
            for (int screen : levelScreens) {
                if (!selectorTargets.contains({ level, screen })) {
                    throw std::runtime_error(
                        "overworld has no selector for level " +
                        std::to_string(level) + " screen " +
                        std::to_string(screen) + ": " +
                        legacyOverworldPath.string());
                }
            }
        }
    }

    void addShaders()
    {
        for (const std::string_view shader : requiredShaders) {
            addFile(
                roots_.shaders,
                std::filesystem::path(shader),
                std::filesystem::path("shaders") / shader,
                "compiled shader",
                false);
        }
    }

    struct PendingFile {
        std::filesystem::path source;
        std::filesystem::path destination;
    };

    ContentSourceRoots roots_;
    std::optional<AssetManifest> manifest_;
    std::map<std::string, PendingFile> files_;
};

void ensureSafeOutputRoot(
    const ContentSourceRoots& roots,
    const std::filesystem::path& outputRoot)
{
    if (outputRoot.empty() || outputRoot == outputRoot.root_path()) {
        throw std::runtime_error("refusing to stage content into an unsafe output path");
    }
    auto resolvePotentialPath = [](const std::filesystem::path& path) {
        std::filesystem::path existing =
            std::filesystem::absolute(path).lexically_normal();
        std::vector<std::filesystem::path> missing;
        std::error_code error;
        while (!std::filesystem::exists(existing, error)) {
            if (error) {
                throw std::runtime_error(
                    "cannot inspect content output path: " +
                    path.string() + ": " + error.message());
            }
            const std::filesystem::path parent = existing.parent_path();
            if (parent.empty() || parent == existing) {
                throw std::runtime_error(
                    "cannot resolve content output path: " + path.string());
            }
            missing.push_back(existing.filename());
            existing = parent;
        }
        std::filesystem::path resolved =
            std::filesystem::canonical(existing, error);
        if (error) {
            throw std::runtime_error(
                "cannot resolve content output path: " + path.string() +
                ": " + error.message());
        }
        for (auto component = missing.rbegin();
             component != missing.rend();
             ++component) {
            resolved /= *component;
        }
        return resolved.lexically_normal();
    };
    const std::filesystem::path output = resolvePotentialPath(outputRoot);
    for (const auto& source : { roots.assets, roots.levels, roots.shaders }) {
        const std::filesystem::path canonicalSource =
            std::filesystem::canonical(source);
        if (output == canonicalSource ||
            isWithin(output, canonicalSource) ||
            isWithin(canonicalSource, output)) {
            throw std::runtime_error("refusing to replace a content source directory: " + output.string());
        }
    }
}

} // namespace

ContentInventory collectContentInventory(const ContentSourceRoots& roots)
{
    return InventoryBuilder(roots).build();
}

ContentInventory stageContent(
    const ContentSourceRoots& roots,
    const std::filesystem::path& outputRoot,
    std::string_view gameVersion)
{
    ensureSafeOutputRoot(roots, outputRoot);
    const ContentInventory inventory = collectContentInventory(roots);
    const std::filesystem::path stagingRoot = outputRoot.parent_path() /
        (outputRoot.filename().string() + ".staging");
    const std::filesystem::path backupRoot = outputRoot.parent_path() /
        (outputRoot.filename().string() + ".previous");

    std::error_code error;
    std::filesystem::remove_all(stagingRoot, error);
    if (error) {
        throw std::runtime_error("cannot clean temporary content directory: " + error.message());
    }
    std::filesystem::create_directories(stagingRoot);

    try {
        for (const ContentFile& file : inventory.files) {
            const std::filesystem::path destination = stagingRoot / file.destination;
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy_file(file.source, destination, std::filesystem::copy_options::overwrite_existing);
        }

        std::ofstream index(stagingRoot / "content.index", std::ios::binary);
        if (!index) {
            throw std::runtime_error("cannot create staged content index");
        }
        index << "format 1\n";
        index << "game-version " << gameVersion << '\n';
        index << "file-count " << inventory.files.size() << '\n';
        index << "total-bytes " << inventory.totalBytes << '\n';
        for (const ContentFile& file : inventory.files) {
            index << "file " << file.size << ' ' << file.destination.generic_string() << '\n';
        }
        index.close();
        if (!index) {
            throw std::runtime_error("cannot finish staged content index");
        }

        std::filesystem::remove_all(backupRoot, error);
        if (error) {
            throw std::runtime_error("cannot clean content backup directory: " + error.message());
        }
        const bool hadPreviousOutput = std::filesystem::exists(outputRoot);
        if (hadPreviousOutput) {
            std::filesystem::rename(outputRoot, backupRoot);
        }
        try {
        std::filesystem::rename(stagingRoot, outputRoot);
        } catch (...) {
            if (hadPreviousOutput && !std::filesystem::exists(outputRoot)) {
                std::filesystem::rename(backupRoot, outputRoot, error);
            }
            throw;
        }
        std::filesystem::remove_all(backupRoot, error);
    } catch (...) {
        std::filesystem::remove_all(stagingRoot, error);
        throw;
    }

    return inventory;
}

} // namespace sokoban
