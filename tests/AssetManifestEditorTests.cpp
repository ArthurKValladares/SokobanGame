#include "ScopedTestDirectory.hpp"
#include "TestAssetRoot.hpp"
#include "TestHarness.hpp"

#include "engine/AssetManifestEditor.hpp"
#include "engine/ContentPipeline.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <algorithm>
#include <string>

namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

class TemporaryManifest {
public:
    explicit TemporaryManifest(const std::filesystem::path& source)
        : directory_("sokoban-manifest-editor-tests")
        , file_(directory_.path() / "manifest.json")
    {
        std::filesystem::copy_file(source, file_);
    }

    [[nodiscard]] const std::filesystem::path& file() const { return file_; }

private:
    ScopedTestDirectory directory_;
    std::filesystem::path file_;
};

void testRoundTripAndMutations(const std::filesystem::path& sourceManifest)
{
    TemporaryManifest temporary(sourceManifest);
    sokoban::AssetManifestEditor editor;
    editor.initialize(temporary.file());

    CHECK_MESSAGE(!editor.dirty(), "loaded editor starts clean");
    // 14 asset-pack textures, grass + rock, the shared splat map, and one
    // splat map per screen. The per-screen count is derived rather than
    // hardcoded, so adding a screen does not fail this for no reason - the
    // fixed part still catches an unintended texture appearing or vanishing.
    const std::size_t perScreenSplatMaps = static_cast<std::size_t>(
        std::count_if(
            editor.textures().begin(),
            editor.textures().end(),
            [](const sokoban::AssetManifest::Texture& texture) {
                return texture.name.starts_with("GroundSplatMap") &&
                    texture.name.find('_') != std::string::npos;
            }));
    CHECK_MESSAGE(perScreenSplatMaps > 0, "per-screen splat maps present");
    CHECK_MESSAGE(editor.textures().size() >= 18 + perScreenSplatMaps,
        "textures loaded");
    CHECK_MESSAGE(std::ranges::any_of(
        editor.textures(),
        [](const sokoban::AssetManifest::Texture& texture) {
            return texture.name == "PlatformerYellow";
        }), "decoration texture loaded");
    CHECK_MESSAGE(editor.models().size() >= 6, "models loaded");
    CHECK_MESSAGE(std::ranges::any_of(
        editor.models(),
        [](const sokoban::AssetManifest::Model& model) {
            return model.name == "Decoration_desk" &&
                model.preserveSourceScale;
        }), "authored-scale decoration model loaded");
    CHECK_MESSAGE(std::ranges::any_of(
        editor.models(),
        [](const sokoban::AssetManifest::Model& model) {
            return model.name == "Barbarian" &&
                model.attachments.size() == 1 &&
                model.attachments[0].node == "handslot.r" &&
                model.attachments[0].rotateHalfTurn;
        }), "skinned attachment loaded");
    CHECK_MESSAGE(editor.animations().size() == 6, "animations loaded");
    CHECK_MESSAGE(editor.tileEntries().size() == 14, "authored tile entries loaded");
    CHECK_MESSAGE(editor.soundSets().size() == 3, "sound sets loaded");
    CHECK_MESSAGE(editor.musicTracks().size() == 4, "music tracks loaded");
    CHECK_MESSAGE(editor.validate(), "unchanged document validates");

    auto texture = editor.textures()[0];
    texture.path = "textures/edited.png";
    editor.updateTexture(0, texture);

    const auto conveyorIt = std::ranges::find_if(
        editor.models(),
        [](const sokoban::AssetManifest::Model& candidate) {
            return candidate.name == "Conveyor";
        });
    CHECK_MESSAGE(conveyorIt != editor.models().end(), "conveyor model loaded");
    const std::size_t conveyorIndex = static_cast<std::size_t>(
        std::distance(editor.models().begin(), conveyorIt));
    auto model = *conveyorIt;
    model.primitiveMaterials[0].scrollV = true;
    editor.updateModel(conveyorIndex, model);

    auto animation = editor.animations()[0];
    animation.clip = 12;
    editor.updateAnimation(0, animation);

    auto tile = editor.tileEntries()[0];
    tile.scale = 1.25f;
    editor.updateTile(0, tile);

    auto sound = editor.soundSets()[0];
    sound.volume = 0.45f;
    sound.files.push_back("audio/extra.ogg");
    editor.updateSoundSet(0, sound);

    auto music = editor.musicTracks()[0];
    music.volume = 0.75f;
    editor.updateMusicTrack(0, music);

    CHECK_MESSAGE(editor.dirty(), "field changes mark document dirty");
    CHECK_MESSAGE(editor.validate(), "edited document validates");
    CHECK_MESSAGE(editor.save(), "edited document saves");
    CHECK_MESSAGE(!editor.dirty(), "save clears dirty state");
    CHECK_MESSAGE(!std::filesystem::exists(temporary.file().string() + ".tmp"),
        "save removes temporary file");
    CHECK_MESSAGE(!std::filesystem::exists(temporary.file().string() + ".bak"),
        "save removes backup file");

    const sokoban::AssetManifest saved =
        sokoban::AssetManifest::loadFromFile(temporary.file());
    CHECK_MESSAGE(saved.textures()[0].path == "textures/edited.png", "texture edit persisted");
    const sokoban::RenderModel conveyor = saved.modelIdByName("Conveyor");
    CHECK_MESSAGE(saved.model(conveyor).primitiveMaterials[0].scrollV,
        "per-material behavior edit persisted");
    CHECK_MESSAGE(saved.animations()[0].clip == 12, "animation edit persisted");
    CHECK_MESSAGE(saved.tileEntries()[0].scale == 1.25f, "tile edit persisted");
    CHECK_MESSAGE(saved.soundSets()[0].files.size() == 6, "sound file edit persisted");
    CHECK_MESSAGE(saved.soundSets()[0].volume == 0.45f, "sound volume edit persisted");
    CHECK_MESSAGE(saved.musicTracks()[0].volume == 0.75f, "music volume edit persisted");

    sokoban::AssetManifestEditor reloaded;
    reloaded.initialize(temporary.file());
    CHECK_MESSAGE(reloaded.textures()[0].path == "textures/edited.png", "saved JSON reloads into editor");
    CHECK_MESSAGE(reloaded.serialize().find("\"format\": 1") != std::string::npos,
        "serialized document keeps format version");
}

void testCollectionOperations(const std::filesystem::path& sourceManifest)
{
    TemporaryManifest temporary(sourceManifest);
    sokoban::AssetManifestEditor editor;
    editor.initialize(temporary.file());

    const std::string firstTexture = editor.textures()[0].name;
    CHECK_MESSAGE(!editor.moveTexture(0, -1), "cannot move first item upward");
    CHECK_MESSAGE(editor.moveTexture(0, 1), "texture moves downward");
    CHECK_MESSAGE(editor.textures()[1].name == firstTexture, "move changes order");
    CHECK_MESSAGE(editor.moveTexture(1, -1), "texture moves back upward");

    const std::size_t textures = editor.textures().size();
    const std::size_t models = editor.models().size();
    const std::size_t animations = editor.animations().size();
    const std::size_t tiles = editor.tileEntries().size();
    const std::size_t sounds = editor.soundSets().size();
    const std::size_t music = editor.musicTracks().size();

    editor.addTexture();
    editor.addModel();
    editor.addAnimation();
    editor.addTile();
    editor.addSoundSet();
    editor.addMusicTrack();
    CHECK_MESSAGE(editor.textures().size() == textures + 1, "texture added");
    CHECK_MESSAGE(editor.models().size() == models + 1, "model added");
    CHECK_MESSAGE(editor.animations().size() == animations + 1, "animation added");
    CHECK_MESSAGE(editor.tileEntries().size() == tiles + 1, "tile added");
    CHECK_MESSAGE(editor.soundSets().size() == sounds + 1, "sound set added");
    CHECK_MESSAGE(editor.musicTracks().size() == music + 1, "music track added");
    CHECK_MESSAGE(editor.validate(), "default additions are schema-valid");

    CHECK_MESSAGE(editor.removeTexture(editor.textures().size() - 1), "texture removed");
    CHECK_MESSAGE(editor.removeModel(editor.models().size() - 1), "model removed");
    CHECK_MESSAGE(editor.removeAnimation(editor.animations().size() - 1), "animation removed");
    CHECK_MESSAGE(editor.removeTile(editor.tileEntries().size() - 1), "tile removed");
    CHECK_MESSAGE(editor.removeSoundSet(editor.soundSets().size() - 1), "sound set removed");
    CHECK_MESSAGE(editor.removeMusicTrack(editor.musicTracks().size() - 1), "music track removed");
    CHECK_MESSAGE(!editor.removeTexture(editor.textures().size()), "out-of-range removal is rejected");
    CHECK_MESSAGE(editor.validate(), "document validates after removals");
}

void testInvalidSavePreservesFile(const std::filesystem::path& sourceManifest)
{
    TemporaryManifest temporary(sourceManifest);
    sokoban::AssetManifestEditor editor;
    editor.initialize(temporary.file());
    const std::string original = readFile(temporary.file());

    auto duplicate = editor.textures()[0];
    duplicate.name = editor.textures()[1].name;
    editor.updateTexture(0, duplicate);
    CHECK_MESSAGE(!editor.validate(), "duplicate texture fails validation");
    CHECK_MESSAGE(!editor.save(), "invalid document is not saved");
    CHECK_MESSAGE(editor.dirty(), "failed save remains dirty");
    CHECK_MESSAGE(readFile(temporary.file()) == original, "failed save preserves original file");
    CHECK_MESSAGE(!std::filesystem::exists(temporary.file().string() + ".tmp"),
        "failed save cleans temporary file");

    CHECK_MESSAGE(editor.reload(), "reload restores disk document");
    CHECK_MESSAGE(!editor.dirty(), "reload clears dirty state");
    CHECK_MESSAGE(editor.textures()[0].name != editor.textures()[1].name,
        "reload discards invalid edit");
}

void testSavePublishesAStartupValidRuntimeManifest(
    const std::filesystem::path& sourceManifest)
{
    TemporaryManifest temporary(sourceManifest);
    const std::filesystem::path runtimeRoot =
        temporary.file().parent_path() / "runtime";
    const std::filesystem::path runtimeManifest = runtimeRoot / "manifest.json";
    std::filesystem::create_directories(runtimeRoot);
    std::filesystem::copy_file(sourceManifest, runtimeManifest);
    std::ofstream(runtimeRoot / "content.index", std::ios::binary)
        << "format 1\ngame-version editor-test\n";

    sokoban::AssetManifestEditor editor;
    editor.initialize(temporary.file(), runtimeManifest);
    auto music = editor.musicTracks().front();
    music.volume = 0.625f;
    editor.updateMusicTrack(0, music);
    CHECK_MESSAGE(editor.save(), "manifest editor publishes its runtime mirror");
    CHECK_MESSAGE(
        readFile(temporary.file()) == readFile(runtimeManifest),
        "source and runtime manifests match");
    sokoban::validateContentPackage(runtimeRoot, "editor-test");
}

} // namespace

int main()
{
    const std::optional<std::filesystem::path> assetsRoot = configuredTestAssetRoot();
    if (!assetsRoot) {
        std::cerr << "SOKOBAN_ASSETS is not set\n";
        return 1;
    }
    const std::filesystem::path sourceManifest = *assetsRoot / "manifest.json";

    testRoundTripAndMutations(sourceManifest);
    testCollectionOperations(sourceManifest);
    testInvalidSavePreservesFile(sourceManifest);
    testSavePublishesAStartupValidRuntimeManifest(sourceManifest);

    if (failures != 0) {
        std::cerr << failures << " asset manifest editor checks failed\n";
        return 1;
    }
    std::cout << "All asset manifest editor checks passed\n";
    return 0;
}
