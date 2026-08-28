#include "engine/ContentPipeline.hpp"
#include "engine/render/SelectorRenderConfig.hpp"

#include "engine/AnimationCatalog.hpp"
#include "engine/render/GltfMesh.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/Level.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/OverworldMap.hpp"
#include "engine/render/ShaderCatalog.hpp"
#include "engine/TileThumbnailBake.hpp"
#include "engine/TileTypes.hpp"
#include "engine/ui/UiConfig.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace sokoban {
namespace {

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

[[nodiscard]] std::uintmax_t parseIndexNumber(
    std::string_view text,
    std::string_view field,
    const std::filesystem::path& indexPath)
{
    std::uintmax_t value = 0;
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc {} || end != text.data() + text.size()) {
        throw std::runtime_error(
            "invalid " + std::string(field) + " in runtime content index: " +
            indexPath.string());
    }
    return value;
}

[[nodiscard]] std::filesystem::path indexEntryPath(
    std::string_view text,
    const std::filesystem::path& indexPath)
{
    const std::filesystem::path parsed { std::string(text) };
    const std::filesystem::path normalized =
        normalizedRelativePath(parsed, "content index path");
    if (normalized.generic_string() != parsed.generic_string()) {
        throw std::runtime_error(
            "non-canonical file path in runtime content index: " +
            indexPath.string());
    }
    if (normalized == "content.index") {
        throw std::runtime_error(
            "runtime content index cannot list itself: " + indexPath.string());
    }
    return normalized;
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

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string contentPathKey(const std::filesystem::path& path)
{
    std::string key = path.generic_string();
#ifdef _WIN32
    key = lowercase(std::move(key));
#endif
    return key;
}

bool isNoticeFile(const std::filesystem::path& path)
{
    const std::string name = lowercase(path.filename().string());
    return name == "license" || name == "license.txt" || name == "ofl.txt" || name == "copying" ||
        name == "copying.txt" || name == "readme" || name == "readme.txt" ||
        name == "readme.html" || name == "copyright" || name == "copyright.txt";
}

std::filesystem::path pathFromUtf8(std::string_view text)
{
    std::u8string utf8;
    utf8.reserve(text.size());
    for (const char character : text) {
        utf8.push_back(static_cast<char8_t>(character));
    }
    return std::filesystem::path(utf8);
}

std::filesystem::path gltfExternalRelativePath(
    const std::filesystem::path& document,
    std::string_view uri)
{
    // URI decoding is deliberately not implicit. Reject percent escapes,
    // schemes, fragments and platform separators so encoded traversal and
    // two spellings of the same source cannot acquire different identities.
    if (uri.empty() || uri.find('\\') != std::string_view::npos ||
        uri.find('%') != std::string_view::npos ||
        uri.find(':') != std::string_view::npos ||
        uri.find('?') != std::string_view::npos ||
        uri.find('#') != std::string_view::npos ||
        uri.find('\0') != std::string_view::npos) {
        throw std::runtime_error(
            "unsupported external glTF URI in " + document.string() +
            ": " + std::string(uri));
    }
    return normalizedRelativePath(
        document.parent_path() / pathFromUtf8(uri),
        "external glTF URI in " + document.string());
}

TextureColorSpace colorSpaceFor(GltfMaterialTextureSemantic semantic)
{
    switch (semantic) {
    case GltfMaterialTextureSemantic::BaseColor:
    case GltfMaterialTextureSemantic::Emissive:
        return TextureColorSpace::Srgb;
    case GltfMaterialTextureSemantic::MetallicRoughness:
    case GltfMaterialTextureSemantic::Normal:
    case GltfMaterialTextureSemantic::Occlusion:
        return TextureColorSpace::Linear;
    }
    throw std::logic_error("unknown glTF material texture semantic");
}

void appendKeyPart(std::string& key, std::string_view part)
{
    key += std::to_string(part.size());
    key.push_back(':');
    key.append(part);
}

std::string textureSourceKey(const TextureSourceIdentity& identity)
{
    std::string key;
    std::visit(
        [&key](const auto& source) {
            using Source = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<Source, ExternalTextureSource>) {
                key = "file:";
                appendKeyPart(key, contentPathKey(source.path));
            } else if constexpr (
                std::is_same_v<Source, GltfBufferViewTextureSource>) {
                key = "view:";
                appendKeyPart(key, contentPathKey(source.document));
                appendKeyPart(key, std::to_string(source.bufferViewIndex));
                appendKeyPart(key, source.mimeType);
            } else {
                key = "data:";
                appendKeyPart(key, source.uri);
            }
        },
        identity.source);
    key += identity.interpretation.colorSpace == TextureColorSpace::Srgb
        ? "|srgb"
        : "|linear";
    return key;
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
        inventory.textureSources = textureSources_;
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

    void addTextureSource(TextureSourceIdentity identity)
    {
        const std::string key = textureSourceKey(identity);
        if (textureSourceKeys_.insert(key).second) {
            textureSources_.push_back(std::move(identity));
        }
    }

    std::filesystem::path addExternalGltfFile(
        const std::filesystem::path& document,
        std::string_view uri,
        std::string_view kind)
    {
        const std::filesystem::path relative =
            gltfExternalRelativePath(document, uri);
        addFile(
            roots_.assets,
            relative,
            relative,
            std::string(kind) + " referenced by " + document.string());
        return relative;
    }

    TextureSource sourceForImage(
        const std::filesystem::path& document,
        const GltfImageDependency& image)
    {
        switch (image.sourceKind) {
        case GltfImageSourceKind::ExternalUri:
            return ExternalTextureSource {
                gltfExternalRelativePath(document, image.uri),
            };
        case GltfImageSourceKind::DataUri:
            if (!image.uri.starts_with("data:image/png;base64,") &&
                !image.uri.starts_with("data:image/jpeg;base64,")) {
                throw std::runtime_error(
                    "unsupported glTF image data URI in " +
                    document.string());
            }
            return DataUriTextureSource { image.uri };
        case GltfImageSourceKind::BufferView:
            if (!image.bufferViewIndex) {
                throw std::runtime_error(
                    "glTF buffer-view image has no buffer view index in " +
                    document.string());
            }
            return GltfBufferViewTextureSource {
                .document = document,
                .bufferViewIndex = *image.bufferViewIndex,
                .mimeType = image.mimeType,
            };
        }
        throw std::logic_error("unknown glTF image source kind");
    }

    void addGltfDependencies(
        const std::filesystem::path& document,
        const std::filesystem::path& absolute)
    {
        const GltfAssetDependencies dependencies =
            inspectGltfAssetDependencies(absolute);

        // Stage every external dependency, including images that are present
        // but currently unused. A package should never depend on whether a
        // future material edit happens to make an already-authored image live.
        for (const GltfBufferDependency& buffer : dependencies.buffers) {
            if (buffer.sourceKind == GltfBufferSourceKind::ExternalUri) {
                (void)addExternalGltfFile(
                    document, buffer.uri, "glTF buffer");
            }
        }
        for (const GltfImageDependency& image : dependencies.images) {
            if (image.sourceKind == GltfImageSourceKind::ExternalUri) {
                (void)addExternalGltfFile(
                    document, image.uri, "glTF image");
            }
        }

        for (std::size_t materialIndex = 0;
             materialIndex < dependencies.materials.size();
             ++materialIndex) {
            const GltfMaterialDependency& material =
                dependencies.materials[materialIndex];
            for (const GltfMaterialTextureDependency& texture :
                 material.textures) {
                if (!texture.imageIndex ||
                    *texture.imageIndex >= dependencies.images.size()) {
                    throw std::runtime_error(
                        "glTF material texture has no supported core image in " +
                        document.string() + " (material " +
                        std::to_string(materialIndex) + " '" + material.name +
                        "', texture '" + texture.textureName + "')");
                }
                addTextureSource({
                    .source = sourceForImage(
                        document, dependencies.images[*texture.imageIndex]),
                    .interpretation = {
                        .colorSpace = colorSpaceFor(texture.semantic),
                    },
                });
            }
        }
    }

    void addAssetPath(
        const std::filesystem::path& relative,
        std::string_view label)
    {
        const std::filesystem::path safeRelative =
            normalizedRelativePath(relative, label);
        addFile(roots_.assets, safeRelative, safeRelative, label);
        const std::string extension =
            lowercase(safeRelative.extension().string());
        if (extension != ".gltf" && extension != ".glb") {
            return;
        }
        if (!inspectedGltf_.insert(contentPathKey(safeRelative)).second) {
            return;
        }
        const std::filesystem::path absolute =
            sourceFile(roots_.assets, safeRelative, label);
        addGltfDependencies(safeRelative, absolute);
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
        constexpr std::array<std::string_view, 7> inputPromptAtlases {
            "kenney_input-prompts_1.5/Keyboard & Mouse/keyboard-&-mouse_sheet_default.xml",
            "kenney_input-prompts_1.5/Generic/generic_sheet_default.xml",
            "kenney_input-prompts_1.5/Xbox Series/xbox-series_sheet_default.xml",
            "kenney_input-prompts_1.5/PlayStation Series/playstation-series_sheet_default.xml",
            "kenney_input-prompts_1.5/Nintendo Switch/nintendo-switch_sheet_default.xml",
            "kenney_input-prompts_1.5/Nintendo Gamecube/nintendo-gamecube_sheet_default.xml",
            "kenney_input-prompts_1.5/Steam Deck/steam-deck_sheet_default.xml",
        };
        for (std::string_view atlas : inputPromptAtlases) {
            addAssetPath(std::filesystem::path(atlas), "input prompt atlas");
        }
        addAssetPath(
            "kenney_input-prompts_1.5/License.txt",
            "Kenney Input Prompts license");
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
            addTextureSource({
                .source = ExternalTextureSource {
                    normalizedRelativePath(
                        texture.path, "texture '" + texture.name + "'"),
                },
                .interpretation = { .colorSpace = texture.colorSpace },
            });
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
                screenCounts, OverworldValidationMode::Structural);
            return;
        }

        for (const Level::ScreenSelector& selector : overworld->selectors()) {
            if (!selector.target) {
                // Project mutations preserve orphaned flags so they can be
                // reassigned or removed in the editor after the game starts.
                // They do not count toward playable-screen coverage.
                continue;
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
        }
    }

    // A catalog entry whose module is not on disk throws out of sourceFile
    // ("compiled shader is missing: ..."), so the build fails here rather
    // than the game failing at launch. The false is includeNotices: the
    // generated SPIR-V tree has no licence files to carry.
    void addShaders()
    {
        for (const std::string_view shader : shaderCatalog::sources) {
            const std::string compiled = shaderCatalog::compiledName(shader);
            addFile(
                roots_.shaders,
                std::filesystem::path(compiled),
                std::filesystem::path("shaders") / compiled,
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
    std::unordered_set<std::string> inspectedGltf_;
    std::unordered_set<std::string> textureSourceKeys_;
    std::vector<TextureSourceIdentity> textureSources_;
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

void validateContentPackage(
    const std::filesystem::path& root,
    std::string_view expectedGameVersion)
{
    const std::filesystem::path packageRoot = canonicalRoot(root, "runtime content");
    const std::filesystem::path indexPath = packageRoot / "content.index";
    const std::filesystem::path manifestPath = packageRoot / "manifest.json";
    std::error_code error;
    if (!std::filesystem::is_regular_file(indexPath, error) || error) {
        throw std::runtime_error(
            "runtime content is missing or was not staged: " + indexPath.string());
    }
    if (!std::filesystem::is_regular_file(manifestPath, error) || error) {
        throw std::runtime_error(
            "runtime asset manifest is missing: " + manifestPath.string());
    }

    std::ifstream index(indexPath, std::ios::binary);
    if (!index) {
        throw std::runtime_error("cannot read runtime content index: " + indexPath.string());
    }
    const auto readExpectedLine = [&index, &indexPath](std::string_view expected) {
        std::string line;
        if (!std::getline(index, line) || line != expected) {
            throw std::runtime_error(
                "unsupported or corrupt runtime content index: " + indexPath.string());
        }
    };
    readExpectedLine("format 1");
    readExpectedLine("game-version " + std::string(expectedGameVersion));

    const auto readNumberLine = [&index, &indexPath](std::string_view field) {
        std::string line;
        const std::string prefix = std::string(field) + ' ';
        if (!std::getline(index, line) || !line.starts_with(prefix)) {
            throw std::runtime_error(
                "invalid " + std::string(field) + " in runtime content index: " +
                indexPath.string());
        }
        return parseIndexNumber(
            std::string_view(line).substr(prefix.size()), field, indexPath);
    };
    const std::uintmax_t declaredCount = readNumberLine("file-count");
    const std::uintmax_t declaredTotal = readNumberLine("total-bytes");

    std::unordered_set<std::string> declaredFiles;
    std::uintmax_t actualTotal = 0;
    for (std::uintmax_t fileIndex = 0; fileIndex < declaredCount; ++fileIndex) {
        std::string line;
        if (!std::getline(index, line) || !line.starts_with("file ")) {
            throw std::runtime_error(
                "truncated runtime content index: " + indexPath.string());
        }
        const std::size_t sizeEnd = line.find(' ', 5);
        if (sizeEnd == std::string::npos || sizeEnd + 1 == line.size()) {
            throw std::runtime_error(
                "invalid file entry in runtime content index: " + indexPath.string());
        }
        const std::uintmax_t declaredSize = parseIndexNumber(
            std::string_view(line).substr(5, sizeEnd - 5), "file size", indexPath);
        const std::filesystem::path relative = indexEntryPath(
            std::string_view(line).substr(sizeEnd + 1), indexPath);
        const std::string key = contentPathKey(relative);
        if (!declaredFiles.insert(key).second) {
            throw std::runtime_error(
                "duplicate file entry in runtime content index: " + indexPath.string());
        }

        const std::filesystem::path candidate =
            std::filesystem::weakly_canonical(packageRoot / relative, error);
        if (error || !isWithin(packageRoot, candidate) ||
            !std::filesystem::is_regular_file(candidate, error) || error) {
            throw std::runtime_error(
                "runtime content file is missing or invalid: " + relative.string());
        }
        const std::uintmax_t actualSize = std::filesystem::file_size(candidate, error);
        if (error || actualSize != declaredSize) {
            throw std::runtime_error(
                "runtime content file size does not match index: " +
                relative.string());
        }
        if (actualTotal > std::numeric_limits<std::uintmax_t>::max() - actualSize) {
            throw std::runtime_error("runtime content byte total overflows: " + indexPath.string());
        }
        actualTotal += actualSize;
    }
    std::string extraLine;
    if (std::getline(index, extraLine) || !index.eof()) {
        throw std::runtime_error(
            "runtime content index has unexpected trailing data: " + indexPath.string());
    }
    if (actualTotal != declaredTotal) {
        throw std::runtime_error(
            "runtime content byte total does not match index: " + indexPath.string());
    }

    std::uintmax_t discoveredCount = 0;
    for (std::filesystem::recursive_directory_iterator it(packageRoot, error), end;
         it != end;
         it.increment(error)) {
        if (error) {
            throw std::runtime_error(
                "cannot enumerate runtime content: " + error.message());
        }
        const std::filesystem::directory_entry& entry = *it;
        if (entry.is_symlink(error) || error) {
            throw std::runtime_error(
                "runtime content package contains a symbolic link: " +
                entry.path().string());
        }
        if (entry.is_directory(error) && !error) {
            continue;
        }
        if (error || !entry.is_regular_file(error) || error) {
            throw std::runtime_error(
                "runtime content package contains an unsupported artifact: " +
                entry.path().string());
        }
        const std::filesystem::path relative =
            entry.path().lexically_relative(packageRoot);
        if (relative == "content.index") {
            continue;
        }
        if (!declaredFiles.contains(contentPathKey(relative))) {
            throw std::runtime_error(
                "runtime content file is missing from index: " + relative.string());
        }
        ++discoveredCount;
    }
    if (discoveredCount != declaredCount) {
        throw std::runtime_error(
            "runtime content file count does not match index: " + indexPath.string());
    }
}

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
        validateContentPackage(stagingRoot, gameVersion);

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
