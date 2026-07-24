#include "engine/AssetManifestEditor.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* label)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

std::optional<std::filesystem::path> assetsRootFromEnvironment()
{
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "SOKOBAN_ASSETS") != 0 || value == nullptr) {
        return std::nullopt;
    }
    const std::filesystem::path result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv("SOKOBAN_ASSETS");
    return value == nullptr
        ? std::nullopt
        : std::optional<std::filesystem::path>(value);
#endif
}

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
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("sokoban-manifest-editor-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(root_);
        file_ = root_ / "manifest.json";
        std::filesystem::copy_file(source, file_);
    }

    ~TemporaryManifest()
    {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] const std::filesystem::path& file() const { return file_; }

private:
    std::filesystem::path root_;
    std::filesystem::path file_;
};

void testRoundTripAndMutations(const std::filesystem::path& sourceManifest)
{
    TemporaryManifest temporary(sourceManifest);
    sokoban::AssetManifestEditor editor;
    editor.initialize(temporary.file());

    check(!editor.dirty(), "loaded editor starts clean");
    check(editor.textures().size() == 3, "textures loaded");
    check(editor.models().size() == 5, "models loaded");
    check(editor.animations().size() == 3, "animations loaded");
    check(editor.tileEntries().size() == 8, "authored tile entries loaded");
    check(editor.soundSets().size() == 2, "sound sets loaded");
    check(editor.musicTracks().size() == 4, "music tracks loaded");
    check(editor.validate(), "unchanged document validates");

    auto texture = editor.textures()[0];
    texture.path = "textures/edited.png";
    editor.updateTexture(0, texture);

    auto model = editor.models()[0];
    model.beltScroll = true;
    editor.updateModel(0, model);

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

    check(editor.dirty(), "field changes mark document dirty");
    check(editor.validate(), "edited document validates");
    check(editor.save(), "edited document saves");
    check(!editor.dirty(), "save clears dirty state");
    check(!std::filesystem::exists(temporary.file().string() + ".tmp"),
        "save removes temporary file");
    check(!std::filesystem::exists(temporary.file().string() + ".bak"),
        "save removes backup file");

    const sokoban::AssetManifest saved =
        sokoban::AssetManifest::loadFromFile(temporary.file());
    check(saved.textures()[0].path == "textures/edited.png", "texture edit persisted");
    check(saved.models()[0].beltScroll, "model edit persisted");
    check(saved.animations()[0].clip == 12, "animation edit persisted");
    check(saved.tileEntries()[0].scale == 1.25f, "tile edit persisted");
    check(saved.soundSets()[0].files.size() == 6, "sound file edit persisted");
    check(saved.soundSets()[0].volume == 0.45f, "sound volume edit persisted");
    check(saved.musicTracks()[0].volume == 0.75f, "music volume edit persisted");

    sokoban::AssetManifestEditor reloaded;
    reloaded.initialize(temporary.file());
    check(reloaded.textures()[0].path == "textures/edited.png", "saved JSON reloads into editor");
    check(reloaded.serialize().find("\"format\": 1") != std::string::npos,
        "serialized document keeps format version");
}

void testCollectionOperations(const std::filesystem::path& sourceManifest)
{
    TemporaryManifest temporary(sourceManifest);
    sokoban::AssetManifestEditor editor;
    editor.initialize(temporary.file());

    const std::string firstTexture = editor.textures()[0].name;
    check(!editor.moveTexture(0, -1), "cannot move first item upward");
    check(editor.moveTexture(0, 1), "texture moves downward");
    check(editor.textures()[1].name == firstTexture, "move changes order");
    check(editor.moveTexture(1, -1), "texture moves back upward");

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
    check(editor.textures().size() == textures + 1, "texture added");
    check(editor.models().size() == models + 1, "model added");
    check(editor.animations().size() == animations + 1, "animation added");
    check(editor.tileEntries().size() == tiles + 1, "tile added");
    check(editor.soundSets().size() == sounds + 1, "sound set added");
    check(editor.musicTracks().size() == music + 1, "music track added");
    check(editor.validate(), "default additions are schema-valid");

    check(editor.removeTexture(editor.textures().size() - 1), "texture removed");
    check(editor.removeModel(editor.models().size() - 1), "model removed");
    check(editor.removeAnimation(editor.animations().size() - 1), "animation removed");
    check(editor.removeTile(editor.tileEntries().size() - 1), "tile removed");
    check(editor.removeSoundSet(editor.soundSets().size() - 1), "sound set removed");
    check(editor.removeMusicTrack(editor.musicTracks().size() - 1), "music track removed");
    check(!editor.removeTexture(editor.textures().size()), "out-of-range removal is rejected");
    check(editor.validate(), "document validates after removals");
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
    check(!editor.validate(), "duplicate texture fails validation");
    check(!editor.save(), "invalid document is not saved");
    check(editor.dirty(), "failed save remains dirty");
    check(readFile(temporary.file()) == original, "failed save preserves original file");
    check(!std::filesystem::exists(temporary.file().string() + ".tmp"),
        "failed save cleans temporary file");

    check(editor.reload(), "reload restores disk document");
    check(!editor.dirty(), "reload clears dirty state");
    check(editor.textures()[0].name != editor.textures()[1].name,
        "reload discards invalid edit");
}

} // namespace

int main()
{
    const std::optional<std::filesystem::path> assetsRoot = assetsRootFromEnvironment();
    if (!assetsRoot) {
        std::cerr << "SOKOBAN_ASSETS is not set\n";
        return 1;
    }
    const std::filesystem::path sourceManifest = *assetsRoot / "manifest.json";

    testRoundTripAndMutations(sourceManifest);
    testCollectionOperations(sourceManifest);
    testInvalidSavePreservesFile(sourceManifest);

    if (failures != 0) {
        std::cerr << failures << " asset manifest editor checks failed\n";
        return 1;
    }
    std::cout << "All asset manifest editor checks passed\n";
    return 0;
}
