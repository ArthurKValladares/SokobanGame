#include "ScopedTestDirectory.hpp"
#include "TestAssetRoot.hpp"
#include "TestHarness.hpp"

#include "engine/AssetManifestEditor.hpp"
#include "engine/AtomicFile.hpp"
#include "engine/ContentPipeline.hpp"
#include "engine/LevelAssetAssociations.hpp"
#include "engine/render/GltfMesh.hpp"
#include "engine/render/CompressedTextureArtifact.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/PngWriter.hpp"
#include "engine/render/RuntimeTextureCatalog.hpp"
#include "engine/render/TextureSourceLoader.hpp"

#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

void writeFile(const std::filesystem::path& path, std::string_view contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

using TreeSnapshot = std::vector<std::pair<std::filesystem::path, std::string>>;

TreeSnapshot snapshotTree(const std::filesystem::path& root)
{
    TreeSnapshot snapshot;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        snapshot.emplace_back(entry.path().lexically_relative(root),
            entry.is_directory() ? "<directory>" : readFile(entry.path()));
    }
    std::ranges::sort(snapshot, {}, &TreeSnapshot::value_type::first);
    return snapshot;
}

void appendUint32(std::string& bytes, uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

std::string triangleBuffer(float size)
{
    std::string bytes;
    for (const float value : std::array {
             0.0f, 0.0f, 0.0f, size, 0.0f, 0.0f, 0.0f, size, 0.0f,
             0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
             0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
         }) {
        appendUint32(bytes, std::bit_cast<uint32_t>(value));
    }
    bytes.append(12, '\0'); // Three JOINTS_0 vectors, all bound to root.
    for (const float value : std::array {
             1.0f, 0.0f, 0.0f, 0.0f,
             1.0f, 0.0f, 0.0f, 0.0f,
             1.0f, 0.0f, 0.0f, 0.0f,
         }) {
        appendUint32(bytes, std::bit_cast<uint32_t>(value));
    }
    bytes.append("\0\0\1\0\2\0", 6);
    bytes.append(2, '\0'); // Align the identity inverse bind matrix to 4 bytes.
    for (const float value : std::array {
             1.0f, 0.0f, 0.0f, 0.0f,
             0.0f, 1.0f, 0.0f, 0.0f,
             0.0f, 0.0f, 1.0f, 0.0f,
             0.0f, 0.0f, 0.0f, 1.0f,
         }) {
        appendUint32(bytes, std::bit_cast<uint32_t>(value));
    }
    return bytes;
}

std::string triangleDocument(bool externalBuffer)
{
    return std::string(R"json({
      "asset":{"version":"2.0"},
      "buffers":[{)json") + (externalBuffer ? "\"uri\":\"hero.bin\"," : "") + R"json("byteLength":228}],
      "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":36},
        {"buffer":0,"byteOffset":36,"byteLength":36},
        {"buffer":0,"byteOffset":72,"byteLength":24},
        {"buffer":0,"byteOffset":96,"byteLength":12},
        {"buffer":0,"byteOffset":108,"byteLength":48},
        {"buffer":0,"byteOffset":156,"byteLength":6},
        {"buffer":0,"byteOffset":164,"byteLength":64}
      ],
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[2,2,0]},
        {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
        {"bufferView":3,"componentType":5121,"count":3,"type":"VEC4"},
        {"bufferView":4,"componentType":5126,"count":3,"type":"VEC4"},
        {"bufferView":5,"componentType":5123,"count":3,"type":"SCALAR"},
        {"bufferView":6,"componentType":5126,"count":1,"type":"MAT4"}
      ],
      "images":[{"uri":"maps/albedo.png"}],
      "textures":[{"source":0}],
      "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],
      "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"JOINTS_0":3,"WEIGHTS_0":4},"indices":5,"material":0}]}],
      "nodes":[{"name":"root","children":[1,2]},{"name":"mesh","mesh":0,"skin":0},{"name":"handslot.r"}],
      "skins":[{"joints":[0],"inverseBindMatrices":6}],"scenes":[{"nodes":[0]}],"scene":0
    })json";
}

void writeGlb(const std::filesystem::path& path, std::string json, std::string binary)
{
    while (json.size() % 4 != 0) json.push_back(' ');
    while (binary.size() % 4 != 0) binary.push_back('\0');
    std::string bytes;
    appendUint32(bytes, 0x46546c67);
    appendUint32(bytes, 2);
    appendUint32(bytes, static_cast<uint32_t>(28 + json.size() + binary.size()));
    appendUint32(bytes, static_cast<uint32_t>(json.size()));
    appendUint32(bytes, 0x4e4f534a);
    bytes += json;
    appendUint32(bytes, static_cast<uint32_t>(binary.size()));
    appendUint32(bytes, 0x004e4942);
    bytes += binary;
    writeFile(path, bytes);
}

void writePublicationAssets(const std::filesystem::path& root, std::string_view version)
{
    const auto modelRoot = root / "models" / version;
    const auto attachmentRoot = root / "attachments" / version;
    const auto animationRoot = root / "animations" / version;
    writeFile(modelRoot / "hero.gltf", triangleDocument(true));
    writeFile(modelRoot / "hero.bin", triangleBuffer(2.0f));
    writeGlb(attachmentRoot / "sword.glb", triangleDocument(false), triangleBuffer(2.0f));
    for (const auto& path : std::array {
             root / "textures" / (std::string(version) + ".png"),
             modelRoot / "maps/albedo.png", attachmentRoot / "maps/albedo.png",
         }) {
        std::filesystem::create_directories(path.parent_path());
        sokoban::writeRgbaPng(path, 1, 1, { 255, 32, 16, 255 });
    }
    writeFile(animationRoot / "clip.gltf", R"json({
      "asset":{"version":"2.0"},"buffers":[{"uri":"clip.bin","byteLength":32}],
      "bufferViews":[{"buffer":0,"byteLength":8},{"buffer":0,"byteOffset":8,"byteLength":24}],
      "accessors":[{"bufferView":0,"componentType":5126,"count":2,"type":"SCALAR"},
        {"bufferView":1,"componentType":5126,"count":2,"type":"VEC3"}],
      "nodes":[{"name":"root"}],"animations":[{"name":"Test",
        "samplers":[{"input":0,"output":1,"interpolation":"LINEAR"}],
        "channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}]
    })json");
    std::string animation;
    for (const float value : std::array { 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }) {
        appendUint32(animation, std::bit_cast<uint32_t>(value));
    }
    writeFile(animationRoot / "clip.bin", animation);
    writeFile(root / "audio" / version / "step.ogg", "fixture sound");
    writeFile(root / "audio" / version / "music.ogg", "fixture music");
}

std::filesystem::path seedCompiledTexture(
    const std::filesystem::path& root,
    const sokoban::AssetManifest::Texture& texture)
{
    const auto identity = sokoban::runtimeTextureDefinitionFor(texture).identity;
    const auto relative = sokoban::compressedTextureArtifactPath(identity);
    const auto bytes = sokoban::buildBc7Ktx2(
        sokoban::loadRgbaImage(root / texture.path), identity.interpretation);
    writeFile(root / relative, std::string_view(
        reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    writeFile(root / "compiled-textures/sentinel", "existing prepared texture");
    CHECK(sokoban::refreshContentPackageIndex(root));
    return relative;
}

class PublicationProject {
public:
    PublicationProject()
        : directory("sokoban-manifest-publication-tests")
        , source(directory.path() / "source")
        , runtime(directory.path() / "runtime")
    {
        writePublicationAssets(source, "old");
        writeFile(source / "manifest.json", R"json({
          "format":1,"textures":[{"name":"HeroTexture","path":"textures/old.png"}],
          "models":[{"name":"Hero","path":"models/old/hero.gltf","geometry":"skinned","role":"player",
            "attachments":[{"path":"attachments/old/sword.glb","node":"handslot.r"}]}],
          "animations":[
            {"name":"Idle","path":"animations/old/clip.gltf","role":"player-idle"},
            {"name":"Move","path":"animations/old/clip.gltf","role":"player-move"},
            {"name":"Push","path":"animations/old/clip.gltf","role":"player-push"},
            {"name":"Death","path":"animations/old/clip.gltf","role":"player-death"},
            {"name":"DeadIdle","path":"animations/old/clip.gltf","role":"player-dead-idle"}
          ],
          "sounds":[{"name":"step","files":["audio/old/step.ogg"]}],
          "music":[{"level":0,"file":"audio/old/music.ogg"}]
        })json");
        std::filesystem::copy(source, runtime, std::filesystem::copy_options::recursive);
        writeFile(runtime / "levels/untouched.scr", "existing level");
        writeFile(runtime / "shaders/untouched.spv", "existing shader");
        writeFile(runtime / "content.index", "format 1\ngame-version publication-test\n");
        CHECK(sokoban::refreshContentPackageIndex(runtime));
        writePublicationAssets(source, "new");
        editor.initialize(source / "manifest.json", runtime / "manifest.json");
    }

    void selectNewAssets()
    {
        auto texture = editor.textures()[0];
        texture.path = "textures/new.png";
        editor.updateTexture(0, texture);
        auto model = editor.models()[0];
        model.path = "models/new/hero.gltf";
        model.attachments[0].path = "attachments/new/sword.glb";
        editor.updateModel(0, model);
        for (std::size_t index = 0; index < editor.animations().size(); ++index) {
            auto animation = editor.animations()[index];
            animation.path = "animations/new/clip.gltf";
            editor.updateAnimation(index, animation);
        }
        auto sound = editor.soundSets()[0];
        sound.files = { "audio/new/step.ogg" };
        editor.updateSoundSet(0, sound);
        auto music = editor.musicTracks()[0];
        music.file = "audio/new/music.ogg";
        editor.updateMusicTrack(0, music);
    }

    ScopedTestDirectory directory;
    std::filesystem::path source;
    std::filesystem::path runtime;
    sokoban::AssetManifestEditor editor;
};

bool checkSaveSucceeded(sokoban::AssetManifestEditor& editor)
{
    const bool saved = editor.save();
    CHECK_MESSAGE(saved, editor.status().c_str());
    return saved;
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
    CHECK_MESSAGE(editor.animations().size() == 7, "animations loaded");
    CHECK_MESSAGE(editor.tileEntries().size() ==
        sokoban::AssetManifest::loadFromFile(sourceManifest).tileEntries().size(),
        "all authored tile entries loaded");
    CHECK_MESSAGE(editor.soundSets().size() >= 3, "sound sets loaded, including authoring drafts");
    CHECK_MESSAGE(std::ranges::count_if(editor.musicTracks(),
        [](const auto& track) { return !track.character; }) == 4, "level music tracks loaded");
    CHECK_MESSAGE(std::ranges::any_of(editor.musicTracks(), [](const auto& track) {
        return track.character == sokoban::CharacterType::Bard && track.file.ends_with("Alpha Dance.ogg");
    }), "bard soundtrack loaded");
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

void testAtmosphericRoundTrip(const std::filesystem::path& sourceManifest)
{
    TEST("atmosphericRoundTrip");
    TemporaryManifest temporary(sourceManifest);
    sokoban::AssetManifestEditor editor;
    editor.initialize(temporary.file());
    editor.addSoundSet();
    const auto index = editor.soundSets().size() - 1;
    auto sound = editor.soundSets()[index];
    sound.atmosphere = sokoban::AssetManifest::Atmosphere {
        .source = sokoban::AssetManifest::AtmosphericSource::Conveyor,
        .audibleDistanceTiles = 4.0f,
        .fullVolumeDistanceTiles = 0.5f,
        .falloffExponent = 2.0f,
    };
    editor.updateSoundSet(index, sound);
    CHECK(editor.save());
    auto saved = sokoban::AssetManifest::loadFromFile(temporary.file());
    CHECK(saved.soundSets()[index] == sound);
    CHECK(saved.soundSets()[index].files.empty());
    CHECK(saved.soundSets()[index - 1].atmosphere == editor.soundSets()[index - 1].atmosphere);
    sound.atmosphere.reset();
    editor.updateSoundSet(index, sound);
    CHECK(editor.save());
    saved = sokoban::AssetManifest::loadFromFile(temporary.file());
    CHECK(!saved.soundSets()[index].atmosphere);
}

void testCharacterMusicRoundTrip(const std::filesystem::path& sourceManifest)
{
    TEST("characterMusicRoundTrip");
    TemporaryManifest temporary(sourceManifest);
    sokoban::AssetManifestEditor editor;
    editor.initialize(temporary.file());
    const auto found = std::ranges::find_if(editor.musicTracks(), [](const auto& track) {
        return track.character == sokoban::CharacterType::Bard;
    });
    CHECK(found != editor.musicTracks().end());
    if (found == editor.musicTracks().end()) {
        return;
    }
    const auto index = static_cast<std::size_t>(found - editor.musicTracks().begin());
    auto track = *found;
    track.volume = 0.625f;
    editor.updateMusicTrack(index, track);
    CHECK(editor.save());
    auto saved = sokoban::AssetManifest::loadFromFile(temporary.file());
    CHECK(saved.musicTracks()[index] == track);
    CHECK(saved.musicTrackFor(0, sokoban::CharacterType::Bard)->volume == 0.625f);
    track.character = sokoban::CharacterType::Rogue;
    editor.updateMusicTrack(index, track);
    CHECK(editor.save());
    saved = sokoban::AssetManifest::loadFromFile(temporary.file());
    CHECK(saved.musicTracks()[index].character == sokoban::CharacterType::Rogue);
    CHECK(saved.musicForLevel(0) && saved.musicTrackFor(0, sokoban::CharacterType::Bard)->file == *saved.musicForLevel(0));
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
    CHECK_MESSAGE(editor.soundSets().back().files.empty(), "new sound set has no fake file path");
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
    const std::filesystem::path&)
{
    TEST("savePublishesAStartupValidRuntimeManifest");
    PublicationProject project;
    auto& editor = project.editor;
    const auto artifact = seedCompiledTexture(project.runtime, editor.textures()[0]);
    const auto preparedBefore = readFile(project.runtime / artifact);
    auto music = editor.musicTracks().front();
    music.volume = 0.625f;
    editor.updateMusicTrack(0, music);
    CHECK_MESSAGE(editor.save(), "manifest editor publishes its runtime mirror");
    CHECK_MESSAGE(
        readFile(project.source / "manifest.json") == readFile(project.runtime / "manifest.json"),
        "source and runtime manifests match");
    sokoban::validateContentPackage(project.runtime, "publication-test");
    CHECK(readFile(project.runtime / artifact) == preparedBefore);
    CHECK(readFile(project.runtime / "compiled-textures/sentinel") == "existing prepared texture");
}

void testSoundSelection(const std::filesystem::path&)
{
    TEST("soundSelection");
    PublicationProject project;
    ScopedTestDirectory external("sokoban-sound-import-tests");
    const auto assetRoot = project.source;
    const auto inside = assetRoot / "audio/inside sound.wav";
    const auto outside = external.path() / "outside.wav";
    std::filesystem::create_directories(inside.parent_path());
    std::ofstream(inside, std::ios::binary) << "inside audio";
    std::ofstream(outside, std::ios::binary) << "outside audio";

    const auto runtimeRoot = project.runtime;
    auto& editor = project.editor;
    editor.addSoundSet();
    const auto soundIndex = editor.soundSets().size() - 1;
    auto sound = editor.soundSets()[soundIndex];
    sound.name = "draft-laser";
    editor.updateSoundSet(soundIndex, sound);
    CHECK_MESSAGE(editor.save(), "empty draft sound set saves");
    sound.files = { "audio/not-yet-authored.ogg" };
    editor.updateSoundSet(soundIndex, sound);
    CHECK_MESSAGE(editor.save(), "missing draft sound remains optional during publication");
    CHECK(!std::filesystem::exists(runtimeRoot / sound.files[0]));
    CHECK_MESSAGE(editor.chooseSoundFile(soundIndex, 0, inside), "picker appends asset file");
    CHECK_MESSAGE(editor.soundSets()[soundIndex].files[0] == "audio/inside sound.wav",
        "picker stores portable asset-relative paths");
    CHECK_MESSAGE(editor.soundFilePath("audio/inside sound.wav") == std::filesystem::canonical(inside),
        "unsaved selection resolves for preview");
    CHECK_MESSAGE(editor.chooseSoundFile(soundIndex, 0, outside), "picker imports external audio");
    CHECK_MESSAGE(editor.soundSets()[soundIndex].files[0] == "custom/audio/outside.wav",
        "external audio is stored under custom/audio");
    CHECK_MESSAGE(editor.dirty(), "selection marks document dirty");
    CHECK_MESSAGE(editor.chooseSoundFile(soundIndex, 1, outside), "second import appends a variation");
    CHECK_MESSAGE(editor.soundSets()[soundIndex].files[1] == "custom/audio/outside_2.wav",
        "imports do not overwrite existing files");
    CHECK_MESSAGE(readFile(assetRoot / "custom/audio/outside.wav") == "outside audio",
        "import keeps file bytes");
    CHECK_MESSAGE(editor.save(), "selected sounds publish with runtime manifest");
    CHECK_MESSAGE(readFile(runtimeRoot / "custom/audio/outside_2.wav") == "outside audio",
        "runtime receives selected sound bytes");
    sokoban::validateContentPackage(runtimeRoot, "publication-test");
    CHECK_MESSAGE(editor.reload(), "selected files reload");
    const auto before = editor.serialize();
    CHECK_MESSAGE(!editor.chooseSoundFile(soundIndex, 99, inside), "stale file slot rejected");
    CHECK_MESSAGE(!editor.chooseSoundFile(soundIndex, 0, external.path() / "missing.wav"),
        "missing selection rejected");
    CHECK_MESSAGE(editor.serialize() == before, "failed selections preserve the document");
    checkThrows([&] { (void)editor.soundFilePath("../outside.wav"); },
        "preview rejects path traversal");
    checkThrows([&] { (void)editor.soundFilePath(outside.string()); },
        "preview rejects absolute manifest paths");
}

void testSavePublishesCompleteAssetClosureAndReplacesChangedBytes()
{
    TEST("savePublishesCompleteAssetClosureAndReplacesChangedBytes");
    PublicationProject project;
    project.selectNewAssets();
    if (!checkSaveSucceeded(project.editor)) return;
    CHECK(!project.editor.dirty());
    const std::array<std::filesystem::path, 9> dependencies {
        "textures/new.png", "models/new/hero.gltf", "models/new/hero.bin",
        "models/new/maps/albedo.png", "attachments/new/sword.glb",
        "attachments/new/maps/albedo.png", "animations/new/clip.gltf",
        "animations/new/clip.bin", "audio/new/music.ogg",
    };
    for (const auto& relative : dependencies) {
        CHECK_MESSAGE(std::filesystem::is_regular_file(project.runtime / relative),
            relative.string().c_str());
        CHECK(readFile(project.runtime / relative) == readFile(project.source / relative));
    }
    CHECK(readFile(project.runtime / "audio/new/step.ogg") == "fixture sound");
    CHECK(readFile(project.runtime / "manifest.json") == project.editor.serialize());
    CHECK(readFile(project.source / "manifest.json") == project.editor.serialize());
    CHECK(readFile(project.runtime / "levels/untouched.scr") == "existing level");
    CHECK(readFile(project.runtime / "shaders/untouched.spv") == "existing shader");
    sokoban::validateContentPackage(project.runtime, "publication-test");

    const auto manifest = sokoban::AssetManifest::loadFromFile(project.runtime / "manifest.json");
    const auto catalog = sokoban::collectRuntimeTextureCatalog(project.runtime, manifest);
    CHECK(catalog.manifestTextureCount() == 1);
    CHECK(catalog.discoveredTextureCount() == 1);
    CHECK(catalog.model(0).preparedBytes > 0);
    CHECK(catalog.animationPreparedBytes(0) > 0);
    for (const auto& relative : { "textures/new.png", "models/new/maps/albedo.png",
             "attachments/new/maps/albedo.png" }) {
        const auto image = sokoban::loadRgbaImage(project.runtime / relative);
        CHECK(image.width == 1 && image.height == 1 && image.rgba.size() == 4);
    }
    auto mesh = sokoban::loadGltfSkinnedMesh(project.runtime / manifest.models()[0].path);
    CHECK(mesh.vertices.size() == 3);
    CHECK(mesh.indices.size() == 3);
    auto attachment = sokoban::loadGltfMesh(project.runtime / manifest.models()[0].attachments[0].path);
    CHECK(attachment.vertices.size() == 3);
    sokoban::addSkinnedAttachment(mesh, std::move(attachment), "handslot.r");
    CHECK(mesh.attachments.size() == 1);
    CHECK(sokoban::loadGltfAnimationClip(project.runtime / manifest.animations()[0].path, 0)
        .durationSeconds == 1.0f);

    const auto oldBuffer = readFile(project.runtime / "models/new/hero.bin");
    const auto oldPng = readFile(project.runtime / "textures/new.png");
    const auto identity = sokoban::runtimeTextureDefinitionFor(project.editor.textures()[0]).identity;
    const auto artifact = seedCompiledTexture(project.runtime, project.editor.textures()[0]);
    CHECK(std::holds_alternative<sokoban::CompressedTextureArtifact>(
        sokoban::loadPreparedTextureSource(project.runtime, identity, true)));
    sokoban::writeRgbaPng(project.source / "textures/new.png", 1, 1, { 16, 64, 255, 255 });
    auto model = project.editor.models()[0];
    model.preserveSourceScale = true;
    project.editor.updateModel(0, model);
    if (!checkSaveSucceeded(project.editor)) return;
    CHECK(!std::filesystem::exists(project.runtime / artifact));
    CHECK(readFile(project.runtime / "compiled-textures/sentinel") == "existing prepared texture");
    CHECK(std::holds_alternative<sokoban::ImageData>(
        sokoban::loadPreparedTextureSource(project.runtime, identity, true)));
    CHECK(readFile(project.runtime / "textures/new.png") ==
        readFile(project.source / "textures/new.png"));
    CHECK(readFile(project.runtime / "textures/new.png") != oldPng);
    CHECK(readFile(project.runtime / "models/new/hero.bin") == oldBuffer);

    // Changed model inputs also invalidate prepared textures, even when the
    // manifest's image paths and source image bytes stay unchanged.
    CHECK(seedCompiledTexture(project.runtime, project.editor.textures()[0]) == artifact);
    auto changedBuffer = triangleBuffer(2.0f);
    std::string changedPosition;
    appendUint32(changedPosition, std::bit_cast<uint32_t>(0.25f));
    changedBuffer.replace(0, 4, changedPosition);
    writeFile(project.source / "models/new/hero.bin", changedBuffer);
    model.rotateHalfTurn = true;
    project.editor.updateModel(0, model);
    if (!checkSaveSucceeded(project.editor)) return;
    CHECK(readFile(project.runtime / "models/new/hero.bin") == changedBuffer);
    CHECK(readFile(project.runtime / "models/new/hero.bin") != oldBuffer);
    CHECK(!std::filesystem::exists(project.runtime / artifact));
    CHECK(!std::filesystem::exists(project.runtime / "compiled-textures/sentinel"));
    sokoban::validateContentPackage(project.runtime, "publication-test");
    CHECK(sokoban::collectRuntimeTextureCatalog(project.runtime,
        sokoban::AssetManifest::loadFromFile(project.runtime / "manifest.json"))
        .model(0).preparedBytes > 0);
}

void testFailedAssetPublicationPreservesSourceAndRuntimeAndCanRetry()
{
    const std::array cases {
        "missingTexture", "missingModelBuffer", "missingModelImage",
        "missingAttachmentImage", "missingAnimationBuffer", "missingMusic",
        "escapingTexture", "escapingModelBuffer", "corruptIndex", "blockedRuntimePath",
        "existingRuntimeStage", "existingRuntimeBackup", "existingSourceStage", "existingSourceBackup",
    };
    for (const std::string_view failure : cases) {
        const std::string testName = "failedAssetPublication/" + std::string(failure);
        TEST(testName.c_str());
        PublicationProject project;
        project.selectNewAssets();
        const auto validIndex = readFile(project.runtime / "content.index");
        if (failure == "missingTexture") {
            std::filesystem::remove(project.source / "textures/new.png");
        } else if (failure == "missingModelBuffer") {
            std::filesystem::remove(project.source / "models/new/hero.bin");
        } else if (failure == "missingModelImage") {
            std::filesystem::remove(project.source / "models/new/maps/albedo.png");
        } else if (failure == "missingAttachmentImage") {
            std::filesystem::remove(project.source / "attachments/new/maps/albedo.png");
        } else if (failure == "missingAnimationBuffer") {
            std::filesystem::remove(project.source / "animations/new/clip.bin");
        } else if (failure == "missingMusic") {
            std::filesystem::remove(project.source / "audio/new/music.ogg");
        } else if (failure == "escapingTexture") {
            std::filesystem::copy_file(project.source / "textures/new.png",
                project.directory.path() / "outside.png");
            auto texture = project.editor.textures()[0];
            texture.path = "../outside.png";
            project.editor.updateTexture(0, texture);
        } else if (failure == "escapingModelBuffer") {
            writeFile(project.directory.path() / "outside.bin", triangleBuffer(2.0f));
            auto document = triangleDocument(true);
            document.replace(document.find("hero.bin"), 8, "../../../outside.bin");
            writeFile(project.source / "models/new/hero.gltf", document);
        } else if (failure == "corruptIndex") {
            writeFile(project.runtime / "content.index", "corrupt index");
        } else if (failure == "blockedRuntimePath") {
            writeFile(project.runtime / "models/new", "unrelated existing runtime file");
            CHECK(sokoban::refreshContentPackageIndex(project.runtime));
        } else if (failure == "existingRuntimeStage" || failure == "existingRuntimeBackup") {
            const auto suffix = failure == "existingRuntimeStage" ? ".manifest-stage" : ".manifest-backup";
            writeFile(std::filesystem::path(project.runtime.string() + suffix) / "keep.txt", "preexisting transaction data");
        } else if (failure == "existingSourceStage" || failure == "existingSourceBackup") {
            const auto suffix = failure == "existingSourceStage" ? ".publish-stage" : ".publish-backup";
            writeFile(project.source / ("manifest.json" + std::string(suffix)), "preexisting transaction data");
        }
        const auto sourceBefore = snapshotTree(project.source);
        const auto runtimeBefore = snapshotTree(project.runtime);
        const auto projectBefore = snapshotTree(project.directory.path());
        const auto draft = project.editor.serialize();
        CHECK(project.editor.dirty());
        CHECK(!project.editor.save());
        CHECK(project.editor.dirty());
        CHECK(project.editor.status().find("Save failed") != std::string::npos);
        CHECK(project.editor.serialize() == draft);
        CHECK(snapshotTree(project.source) == sourceBefore);
        CHECK(snapshotTree(project.runtime) == runtimeBefore);
        CHECK(snapshotTree(project.directory.path()) == projectBefore);
        const auto oldManifest = sokoban::AssetManifest::loadFromFile(project.runtime / "manifest.json");
        CHECK(oldManifest.models()[0].path == "models/old/hero.gltf");
        CHECK(sokoban::collectRuntimeTextureCatalog(project.runtime, oldManifest)
            .model(0).preparedBytes > 0);

        writePublicationAssets(project.source, "new");
        project.selectNewAssets();
        if (failure == "corruptIndex") {
            writeFile(project.runtime / "content.index", validIndex);
        } else if (failure == "blockedRuntimePath") {
            std::filesystem::remove(project.runtime / "models/new");
        } else if (failure == "existingRuntimeStage" || failure == "existingRuntimeBackup") {
            const auto suffix = failure == "existingRuntimeStage" ? ".manifest-stage" : ".manifest-backup";
            const auto transactionPath = std::filesystem::path(project.runtime.string() + suffix);
            std::filesystem::remove(transactionPath / "keep.txt");
            std::filesystem::remove(transactionPath);
        } else if (failure == "existingSourceStage" || failure == "existingSourceBackup") {
            const auto suffix = failure == "existingSourceStage" ? ".publish-stage" : ".publish-backup";
            std::filesystem::remove(project.source / ("manifest.json" + std::string(suffix)));
        }
        if (!checkSaveSucceeded(project.editor)) continue;
        CHECK(!project.editor.dirty());
        CHECK(readFile(project.runtime / "manifest.json") == project.editor.serialize());
        CHECK(readFile(project.source / "manifest.json") == project.editor.serialize());
        sokoban::validateContentPackage(project.runtime, "publication-test");
        CHECK(sokoban::collectRuntimeTextureCatalog(project.runtime,
            sokoban::AssetManifest::loadFromFile(project.runtime / "manifest.json"))
            .model(0).preparedBytes > 0);
    }
}

void testRuntimePublicationCannotAliasSourceTransactionFiles()
{
    for (const std::string_view suffix : { ".publish-stage", ".publish-backup" }) {
        const std::string testName = "runtimePublicationAlias/" + std::string(suffix);
        TEST(testName.c_str());
        PublicationProject project;
        const auto aliasedRuntime = project.source / ("manifest.json" + std::string(suffix));
        project.editor.initialize(project.source / "manifest.json", aliasedRuntime / "manifest.json");
        project.selectNewAssets();
        const auto before = snapshotTree(project.directory.path());
        const auto draft = project.editor.serialize();
        CHECK(!project.editor.save());
        CHECK(project.editor.dirty());
        CHECK(project.editor.status().find("Save failed") != std::string::npos);
        CHECK(project.editor.serialize() == draft);
        CHECK(snapshotTree(project.directory.path()) == before);
        CHECK(!std::filesystem::exists(aliasedRuntime));
        project.editor.initialize(project.source / "manifest.json", project.runtime / "manifest.json");
        project.selectNewAssets();
        if (!checkSaveSucceeded(project.editor)) continue;
        sokoban::validateContentPackage(project.runtime, "publication-test");
    }
}

#ifdef _WIN32
void testUnchangedStreamedMusicDoesNotBlockRuntimePublication()
{
    TEST("unchangedStreamedMusicDoesNotBlockRuntimePublication");
    PublicationProject project;
    const auto musicPath = project.runtime / "audio/old/music.ogg";
    const HANDLE music = CreateFileW(musicPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(music != INVALID_HANDLE_VALUE);
    if (music == INVALID_HANDLE_VALUE) return;
    struct CloseFile {
        HANDLE handle;
        ~CloseFile() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    } closeMusic { music };
    const auto readOpenMusic = [&] {
        LARGE_INTEGER start {};
        CHECK(SetFilePointerEx(music, start, nullptr, FILE_BEGIN) != FALSE);
        std::array<char, 64> bytes {};
        DWORD size = 0;
        CHECK(ReadFile(music, bytes.data(), static_cast<DWORD>(bytes.size()),
            &size, nullptr) != FALSE);
        return std::string(bytes.data(), size);
    };
    const auto originalMusic = readOpenMusic();
    CHECK(originalMusic == "fixture music");

    auto track = project.editor.musicTracks()[0];
    track.volume = 0.625f;
    project.editor.updateMusicTrack(0, track);
    if (!checkSaveSucceeded(project.editor)) return;
    CHECK(readOpenMusic() == originalMusic);
    CHECK(readFile(musicPath) == originalMusic);
    sokoban::validateContentPackage(project.runtime, "publication-test");

    auto model = project.editor.models()[0];
    model.path = "models/new/hero.gltf";
    model.attachments[0].path = "attachments/new/sword.glb";
    project.editor.updateModel(0, model);
    if (!checkSaveSucceeded(project.editor)) return;
    CHECK(readOpenMusic() == originalMusic);
    CHECK(readFile(musicPath) == originalMusic);
    CHECK(readFile(project.source / "manifest.json") == project.editor.serialize());
    CHECK(readFile(project.runtime / "manifest.json") == project.editor.serialize());
    const auto manifest = sokoban::AssetManifest::loadFromFile(project.runtime / "manifest.json");
    CHECK(manifest.musicTracks()[0].file == "audio/old/music.ogg");
    CHECK(sokoban::collectRuntimeTextureCatalog(project.runtime, manifest)
        .model(0).preparedBytes > 0);
    sokoban::validateContentPackage(project.runtime, "publication-test");

    TEST("changedStreamedMusicRollsBackAndCanRetryAfterClose");
    project.selectNewAssets();
    track = project.editor.musicTracks()[0];
    track.file = "audio/old/music.ogg";
    project.editor.updateMusicTrack(0, track);
    writeFile(project.source / track.file, "replacement music");
    const auto before = snapshotTree(project.directory.path());
    const auto draft = project.editor.serialize();
    CHECK(!project.editor.save());
    CHECK(project.editor.dirty());
    CHECK(project.editor.status().find("Save failed") != std::string::npos);
    CHECK(project.editor.serialize() == draft);
    CHECK(snapshotTree(project.directory.path()) == before);
    CHECK(readOpenMusic() == originalMusic);
    CHECK(readFile(musicPath) == originalMusic);
    sokoban::validateContentPackage(project.runtime, "publication-test");

    CHECK(CloseHandle(closeMusic.handle) != FALSE);
    closeMusic.handle = INVALID_HANDLE_VALUE;
    if (!checkSaveSucceeded(project.editor)) return;
    CHECK(!project.editor.dirty());
    CHECK(readFile(musicPath) == "replacement music");
    CHECK(readFile(project.source / "manifest.json") == project.editor.serialize());
    CHECK(readFile(project.runtime / "manifest.json") == project.editor.serialize());
    sokoban::validateContentPackage(project.runtime, "publication-test");
}
#endif

#ifdef SOKOBAN_ENABLE_TEST_HOOKS
void testAssetPublicationWriteFailuresLeaveLiveTreesUntouched()
{
    for (uint32_t writesBeforeFailure = 0; writesBeforeFailure < 3; ++writesBeforeFailure) {
        const std::string testName = "assetPublicationWriteFailure/" +
            std::to_string(writesBeforeFailure);
        TEST(testName.c_str());
        PublicationProject project;
        project.selectNewAssets();
        const auto before = snapshotTree(project.directory.path());
        const auto draft = project.editor.serialize();
        sokoban::atomicFile::failWriteAfterForTesting(
            writesBeforeFailure, std::errc::no_space_on_device);
        CHECK(!project.editor.save());
        CHECK(project.editor.dirty());
        CHECK(project.editor.status().find("Save failed") != std::string::npos);
        CHECK(project.editor.serialize() == draft);
        CHECK(snapshotTree(project.directory.path()) == before);
        sokoban::validateContentPackage(project.runtime, "publication-test");
        if (!checkSaveSucceeded(project.editor)) continue;
        CHECK(!project.editor.dirty());
        CHECK(readFile(project.source / "manifest.json") == project.editor.serialize());
        CHECK(readFile(project.runtime / "manifest.json") == project.editor.serialize());
        sokoban::validateContentPackage(project.runtime, "publication-test");
    }
}

void testAssetPublicationInstallFailuresRestoreBothTrees()
{
    for (const uint32_t replacementsBeforeFailure : { 3U, 15U }) {
        for (const bool afterInstall : { false, true }) {
            const std::string testName = std::string("assetPublicationInstallFailure/") +
                (replacementsBeforeFailure == 3 ? "firstAsset/" : "sourceManifest/") +
                (afterInstall ? "afterInstall" : "beforeInstall");
            TEST(testName.c_str());
            PublicationProject project;
            project.selectNewAssets();
            const auto before = snapshotTree(project.directory.path());
            const auto draft = project.editor.serialize();
            sokoban::atomicFile::failReplaceAfterForTesting(
                replacementsBeforeFailure, std::errc::permission_denied, afterInstall);
            CHECK(!project.editor.save());
            CHECK(project.editor.dirty());
            CHECK(project.editor.status().find("Save failed") != std::string::npos);
            CHECK(project.editor.serialize() == draft);
            CHECK(snapshotTree(project.directory.path()) == before);
            sokoban::validateContentPackage(project.runtime, "publication-test");
            const auto oldManifest = sokoban::AssetManifest::loadFromFile(project.runtime / "manifest.json");
            CHECK(oldManifest.models()[0].path == "models/old/hero.gltf");
            CHECK(sokoban::collectRuntimeTextureCatalog(project.runtime, oldManifest)
                .model(0).preparedBytes > 0);
            if (!checkSaveSucceeded(project.editor)) continue;
            CHECK(!project.editor.dirty());
            CHECK(readFile(project.source / "manifest.json") == project.editor.serialize());
            CHECK(readFile(project.runtime / "manifest.json") == project.editor.serialize());
            sokoban::validateContentPackage(project.runtime, "publication-test");
        }
    }
}
#endif

void testLevelAssociationsFollowInsertDeleteAndRestore()
{
    ScopedTestDirectory directory("sokoban-level-association-tests");
    const std::filesystem::path manifestPath =
        directory.path() / "manifest.json";
    std::ofstream(manifestPath, std::ios::binary) << R"json({
      "format": 1,
      "textures": [
        { "name": "GroundSplatMap0_0", "path": "maps/first.png", "colorSpace": "linear" },
        { "name": "GroundSplatMap0_1", "path": "maps/second.png", "colorSpace": "linear" },
        { "name": "GroundSplatMap1_0", "path": "maps/third.png", "colorSpace": "linear" }
      ],
      "models": [
        { "name": "Hero", "path": "hero.glb", "geometry": "skinned", "role": "player" }
      ],
      "animations": [
        { "name": "Idle", "path": "hero.glb", "role": "player-idle" },
        { "name": "Move", "path": "hero.glb", "role": "player-move" },
        { "name": "Push", "path": "hero.glb", "role": "player-push" },
        { "name": "Death", "path": "hero.glb", "role": "player-death" },
        { "name": "DeadIdle", "path": "hero.glb", "role": "player-dead-idle" }
      ],
      "music": [
        { "character": "bard", "file": "music/bard.ogg" },
        { "level": 0, "file": "music/first.ogg" },
        { "level": 1, "file": "music/second.ogg" }
      ]
    })json";

    const std::array insertRemaps {
        sokoban::LevelLocationAssociationRemap {
            .source = { .level = 0, .screen = 0 },
            .destination = sokoban::LevelLocation { .level = 0, .screen = 1 },
        },
        sokoban::LevelLocationAssociationRemap {
            .source = { .level = 0, .screen = 1 },
            .destination = sokoban::LevelLocation { .level = 0, .screen = 2 },
        },
        sokoban::LevelLocationAssociationRemap {
            .source = { .level = 1, .screen = 0 },
            .destination = sokoban::LevelLocation { .level = 2, .screen = 0 },
        },
    };
    sokoban::remapLevelAssetAssociations(manifestPath, insertRemaps);
    sokoban::AssetManifest inserted =
        sokoban::AssetManifest::loadFromFile(manifestPath);
    CHECK_MESSAGE(inserted.textureIdByName("GroundSplatMap0_1").index() == 0,
        "first map follows its logical screen during insertion");
    CHECK_MESSAGE(inserted.textureIdByName("GroundSplatMap0_2").index() == 1,
        "second map follows its logical screen during insertion");
    CHECK_MESSAGE(inserted.textureIdByName("GroundSplatMap2_0").index() == 2,
        "map follows its logical level during insertion");
    CHECK_MESSAGE(inserted.musicForLevel(0) &&
            *inserted.musicForLevel(0) == "music/first.ogg",
        "screen insertion keeps the owning level's music");
    CHECK_MESSAGE(inserted.musicForLevel(2) &&
            *inserted.musicForLevel(2) == "music/second.ogg",
        "music follows its logical level during insertion");
    CHECK(inserted.musicTrackFor(99, sokoban::CharacterType::Bard)->file == "music/bard.ogg");

    const sokoban::DeletedLevelAssetAssociations archived =
        sokoban::captureLevelAssetAssociations(manifestPath, 0);
    CHECK(archived.music && archived.music->file == "music/first.ogg");
    const std::filesystem::path deletedLevel = directory.path() / "DeletedLevel";
    std::filesystem::create_directories(deletedLevel);
    sokoban::writeDeletedLevelAssetAssociations(deletedLevel, archived);
    const std::array deleteRemaps {
        sokoban::LevelLocationAssociationRemap {
            .source = { .level = 0, .screen = 1 },
            .destination = std::nullopt,
        },
        sokoban::LevelLocationAssociationRemap {
            .source = { .level = 0, .screen = 2 },
            .destination = std::nullopt,
        },
        sokoban::LevelLocationAssociationRemap {
            .source = { .level = 2, .screen = 0 },
            .destination = sokoban::LevelLocation { .level = 1, .screen = 0 },
        },
    };
    sokoban::remapLevelAssetAssociations(manifestPath, deleteRemaps);
    const sokoban::AssetManifest deleted =
        sokoban::AssetManifest::loadFromFile(manifestPath);
    CHECK_MESSAGE(deleted.findTextureIdByName("GroundSplatMap0_1").isNone(),
        "deleted level releases its active splat names");
    CHECK_MESSAGE(deleted.textureIdByName("GroundSplatMap1_0").index() == 0,
        "later map shifts down after level deletion");
    CHECK_MESSAGE(deleted.musicForLevel(1) &&
            *deleted.musicForLevel(1) == "music/second.ogg",
        "later music shifts down after level deletion");
    CHECK(deleted.musicTrackFor(99, sokoban::CharacterType::Bard)->file == "music/bard.ogg");

    const auto restoredArchive =
        sokoban::readDeletedLevelAssetAssociations(deletedLevel);
    CHECK(restoredArchive.has_value());
    sokoban::restoreLevelAssetAssociations(
        manifestPath, *restoredArchive, 2);
    const sokoban::AssetManifest restored =
        sokoban::AssetManifest::loadFromFile(manifestPath);
    const sokoban::RenderTexture restoredFirst =
        restored.textureIdByName("GroundSplatMap2_1");
    CHECK_MESSAGE(
        restored.textures()[restoredFirst.index()].path == "maps/first.png",
        "restored level recovers its original map path");
    CHECK_MESSAGE(restored.musicForLevel(2) &&
            *restored.musicForLevel(2) == "music/first.ogg",
        "restored level recovers its original music");
    CHECK(restored.musicTrackFor(99, sokoban::CharacterType::Bard)->file == "music/bard.ogg");
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
    testAtmosphericRoundTrip(sourceManifest);
    testCharacterMusicRoundTrip(sourceManifest);
    testCollectionOperations(sourceManifest);
    testInvalidSavePreservesFile(sourceManifest);
    testSavePublishesAStartupValidRuntimeManifest(sourceManifest);
    testSavePublishesCompleteAssetClosureAndReplacesChangedBytes();
    testFailedAssetPublicationPreservesSourceAndRuntimeAndCanRetry();
    testRuntimePublicationCannotAliasSourceTransactionFiles();
#ifdef _WIN32
    testUnchangedStreamedMusicDoesNotBlockRuntimePublication();
#endif
#ifdef SOKOBAN_ENABLE_TEST_HOOKS
    testAssetPublicationWriteFailuresLeaveLiveTreesUntouched();
    testAssetPublicationInstallFailuresRestoreBothTrees();
#endif
    testSoundSelection(sourceManifest);
    testLevelAssociationsFollowInsertDeleteAndRestore();

    if (failures != 0) {
        std::cerr << failures << " asset manifest editor checks failed\n";
        return 1;
    }
    std::cout << "All asset manifest editor checks passed\n";
    return 0;
}
