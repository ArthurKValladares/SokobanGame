#include "engine/ContentPipeline.hpp"
#include "engine/TileThumbnailBake.hpp"
#include "engine/TileTypes.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
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

template <typename Fn>
void checkThrows(Fn&& fn, const char* label)
{
    try {
        fn();
        ++failures;
        std::cerr << "FAIL (no throw): " << label << '\n';
    } catch (const std::exception&) {
    }
}

class TempDirectory {
public:
    TempDirectory()
    {
        const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("sokoban-content-pipeline-" + std::to_string(id));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void writeFile(const std::filesystem::path& path, std::string_view contents = "data")
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

std::string manifest(std::string_view texturePath = "textures/hero.png")
{
    return R"json({
  "format": 1,
  "textures": [
    { "name": "HeroTexture", "path": ")json" + std::string(texturePath) + R"json(" },
    { "name": "BoardGameBits", "path": "textures/boardgame.png" }
  ],
  "models": [
    {
      "name": "Hero",
      "path": "models/hero.gltf",
      "geometry": "skinned",
      "material": { "mode": "texture", "texture": "HeroTexture" },
      "attachments": [
        { "path": "models/sword.gltf", "node": "handslot.r" }
      ],
      "role": "player"
    },
    {
      "name": "ScreenSelectorAPlayable",
      "path": "models/flag-a-blue.gltf",
      "material": { "mode": "texture", "texture": "BoardGameBits" }
    },
    {
      "name": "ScreenSelectorASolved",
      "path": "models/flag-a-green.gltf",
      "material": { "mode": "texture", "texture": "BoardGameBits" }
    },
    {
      "name": "ScreenSelectorAUnavailable",
      "path": "models/flag-a-red.gltf",
      "material": { "mode": "texture", "texture": "BoardGameBits" }
    },
    {
      "name": "ScreenSelectorBPlayable",
      "path": "models/flag-b-blue.gltf",
      "material": { "mode": "texture", "texture": "BoardGameBits" }
    },
    {
      "name": "ScreenSelectorBSolved",
      "path": "models/flag-b-green.gltf",
      "material": { "mode": "texture", "texture": "BoardGameBits" }
    },
    {
      "name": "ScreenSelectorBUnavailable",
      "path": "models/flag-b-red.gltf",
      "material": { "mode": "texture", "texture": "BoardGameBits" }
    }
  ],
  "animations": [
    { "name": "Idle", "path": "animations/all.gltf", "role": "player-idle" },
    { "name": "Move", "path": "animations/all.gltf", "role": "player-move" },
    { "name": "Push", "path": "animations/all.gltf", "role": "player-push" },
    { "name": "Death", "path": "animations/all.gltf", "role": "player-death" },
    { "name": "DeadIdle", "path": "animations/static.gltf", "role": "player-dead-idle" }
  ],
  "sounds": [
    { "name": "footsteps", "files": ["audio/step.ogg"] }
  ],
  "music": [
    { "level": 0, "file": "audio/music.ogg" }
  ]
})json";
}

std::string animationCatalog()
{
    return R"json({
  "format": 2,
  "clips": [
    { "animation": "Idle", "speed": 1.0, "duration": 1.0 },
    { "animation": "Move", "speed": 1.0, "duration": 1.0 },
    { "animation": "Push", "speed": 1.0, "duration": 1.0 },
    { "animation": "Death", "speed": 1.0, "duration": 1.0 },
    { "animation": "DeadIdle", "speed": 1.0, "duration": 0.0 }
  ],
  "uses": [
    { "id": "player.idle", "animation": "Idle", "speed": 1.0 },
    { "id": "player.move", "animation": "Move", "speed": 1.0 },
    { "id": "player.push", "animation": "Push", "speed": 1.0 },
    { "id": "player.death", "animation": "Death", "speed": 1.0 },
    { "id": "player.dead-idle", "animation": "DeadIdle", "speed": 1.0 },
    { "id": "enemy.idle", "animation": "Idle", "speed": 1.0 },
    { "id": "enemy.attack", "animation": "Idle", "speed": 1.0 },
    { "id": "mirror-preview.player-idle", "animation": "Idle", "speed": 1.0 },
    { "id": "mirror-preview.player-dead-idle", "animation": "DeadIdle", "speed": 1.0 },
    { "id": "editor.player-idle", "animation": "Idle", "speed": 1.0 },
    { "id": "editor.enemy-idle", "animation": "Idle", "speed": 1.0 },
    { "id": "thumbnail.player-idle", "animation": "Idle", "speed": 1.0 },
    { "id": "thumbnail.enemy-idle", "animation": "Idle", "speed": 1.0 }
  ]
})json";
}

sokoban::ContentSourceRoots createValidContent(const std::filesystem::path& root)
{
    const std::filesystem::path assets = root / "source-assets";
    const std::filesystem::path levels = root / "source-levels";
    const std::filesystem::path shaders = root / "compiled-shaders";

    writeFile(assets / "manifest.json", manifest());
    writeFile(assets / "animation_catalog.json", animationCatalog());
    writeFile(assets / "textures/hero.png");
    writeFile(assets / "textures/boardgame.png");
    writeFile(assets / "ui/Karla-Regular.ttf");
    writeFile(assets / "ui/OFL.txt", "font license");
    writeFile(assets / "custom/ui/main-menu-rogue-pushing-rock-4k.png");
    constexpr std::array<std::string_view, 7> inputPromptAtlases {
        "kenney_input-prompts_1.5/Keyboard & Mouse/keyboard-&-mouse_sheet_default.xml",
        "kenney_input-prompts_1.5/Generic/generic_sheet_default.xml",
        "kenney_input-prompts_1.5/Xbox Series/xbox-series_sheet_default.xml",
        "kenney_input-prompts_1.5/PlayStation Series/playstation-series_sheet_default.xml",
        "kenney_input-prompts_1.5/Nintendo Switch/nintendo-switch_sheet_default.xml",
        "kenney_input-prompts_1.5/Nintendo Gamecube/nintendo-gamecube_sheet_default.xml",
        "kenney_input-prompts_1.5/Steam Deck/steam-deck_sheet_default.xml",
    };
    for (const std::string_view atlas : inputPromptAtlases) {
        writeFile(assets / atlas, "<TextureAtlas imagePath=\"fixture.png\"/>");
    }
    writeFile(
        assets / "kenney_input-prompts_1.5/License.txt",
        "input prompt license");
    writeFile(assets / "models/hero.gltf", R"({"buffers":[{"uri":"hero.bin"}]})");
    writeFile(assets / "models/hero.bin");
    writeFile(assets / "models/sword.gltf", R"({"buffers":[{"uri":"sword.bin"}]})");
    writeFile(assets / "models/sword.bin");
    for (std::string_view flag : {
            "flag-a-blue",
            "flag-a-green",
            "flag-a-red",
            "flag-b-blue",
            "flag-b-green",
            "flag-b-red",
        }) {
        writeFile(
            assets / "models" / (std::string(flag) + ".gltf"),
            "{\"buffers\":[{\"uri\":\"" + std::string(flag) +
                ".bin\"}]}");
        writeFile(assets / "models" / (std::string(flag) + ".bin"));
    }
    writeFile(assets / "models/LICENSE.txt", "model license");
    const std::string animationGltf = R"json({
      "asset":{"version":"2.0"},
      "buffers":[{"uri":"clip.bin","byteLength":32}],
      "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":8},
        {"buffer":0,"byteOffset":8,"byteLength":24}
      ],
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":2,"type":"SCALAR"},
        {"bufferView":1,"componentType":5126,"count":2,"type":"VEC3"}
      ],
      "nodes":[{"name":"root"}],
      "animations":[{
        "name":"Test",
        "samplers":[{"input":0,"output":1,"interpolation":"LINEAR"}],
        "channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]
      }]
    })json";
    const std::string staticAnimationGltf = R"json({
      "asset":{"version":"2.0"},
      "buffers":[{"uri":"static.bin","byteLength":16}],
      "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":4},
        {"buffer":0,"byteOffset":4,"byteLength":12}
      ],
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":1,"type":"SCALAR"},
        {"bufferView":1,"componentType":5126,"count":1,"type":"VEC3"}
      ],
      "nodes":[{"name":"root"}],
      "animations":[{
        "name":"Static",
        "samplers":[{"input":0,"output":1,"interpolation":"STEP"}],
        "channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]
      }]
    })json";
    writeFile(assets / "animations/all.gltf", animationGltf);
    writeFile(assets / "animations/static.gltf", staticAnimationGltf);
    std::string clipBuffer(32, '\0');
    const float oneSecond = 1.0f;
    std::memcpy(clipBuffer.data() + sizeof(float),
        &oneSecond, sizeof(oneSecond));
    writeFile(assets / "animations/clip.bin", clipBuffer);
    writeFile(assets / "animations/static.bin", std::string(16, '\0'));
    writeFile(assets / "audio/step.ogg");
    writeFile(assets / "audio/music.ogg");
    writeFile(levels / "level0/screen0.scr", "@layer 0\n...\n\n@layer 1\n.CE\n");
    writeFile(
        levels / "overworld.scr",
        "@selector {\"id\":1,\"cell\":[1,0,1],"
        "\"target\":{\"level\":0,\"screen\":0}}\n\n"
        "@layer 0\n...\n\n@layer 1\nC  \n");
    writeFile(
        levels / "level0/metadata.json",
        R"json({"format":1,"name":"First Light","screens":["Arrival"]})json");
    writeFile(levels / "Deleted/level9/screen0.scr", "not shipped");

    constexpr const char* shaderNames[] {
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
        "world_transition.frag.glsl.spv",
    };
    for (const char* shader : shaderNames) {
        writeFile(shaders / shader);
    }
    return { assets, levels, shaders };
}

bool contains(const sokoban::ContentInventory& inventory, std::string_view destination)
{
    for (const auto& file : inventory.files) {
        if (file.destination.generic_string() == destination) {
            return true;
        }
    }
    return false;
}

void testInventoryAndStaging()
{
    TempDirectory temp;
    const auto roots = createValidContent(temp.path());
    const sokoban::ContentInventory inventory = sokoban::collectContentInventory(roots);

    check(contains(inventory, "manifest.json"), "manifest included");
    check(
        contains(inventory, "animation_catalog.json"),
        "animation catalog included");
    check(contains(inventory, "models/hero.gltf"), "model included");
    check(contains(inventory, "models/hero.bin"), "external glTF buffer included");
    check(contains(inventory, "models/sword.gltf"), "attachment mesh included");
    check(contains(inventory, "models/sword.bin"), "attachment dependency included");
    check(contains(inventory, "models/LICENSE.txt"), "nearby asset license included");
    check(contains(inventory, "ui/Karla-Regular.ttf"), "UI font included");
    check(contains(inventory, "ui/OFL.txt"), "UI font license included");
    check(
        contains(inventory, "custom/ui/main-menu-rogue-pushing-rock-4k.png"),
        "title background included");
    check(
        contains(
            inventory,
            "kenney_input-prompts_1.5/Keyboard & Mouse/keyboard-&-mouse_sheet_default.xml"),
        "input prompt atlas included");
    check(
        contains(inventory, "kenney_input-prompts_1.5/License.txt"),
        "input prompt license included");
    check(contains(inventory, "levels/level0/screen0.scr"), "playable level included");
    check(contains(inventory, "levels/overworld.scr"), "overworld included");
    check(contains(inventory, "levels/level0/metadata.json"),
        "level names included");
    check(!contains(inventory, "levels/Deleted/level9/screen0.scr"), "deleted level excluded");
    check(contains(inventory, "shaders/model.vert.glsl.spv"), "compiled shader included");

    const std::filesystem::path output = temp.path() / "package/assets";
    writeFile(output / "stale.file");
    const sokoban::ContentInventory staged = sokoban::stageContent(roots, output, "1.2.3");
    check(staged.files.size() == inventory.files.size(), "stage returns inventory");
    check(std::filesystem::is_regular_file(output / "content.index"), "content index written");
    check(
        std::ifstream(output / "content.index").good(),
        "content index readable");
    check(std::filesystem::is_regular_file(output / "models/hero.bin"), "dependency staged");
    check(!std::filesystem::exists(output / "stale.file"), "stale output removed");
}

void testStagedContentIndexValidation()
{
    TempDirectory temp;
    const auto roots = createValidContent(temp.path());
    const std::filesystem::path output = temp.path() / "package/assets";

    (void)sokoban::stageContent(roots, output, "1.2.3");
    sokoban::validateContentPackage(output, "1.2.3");
    checkThrows(
        [&] { sokoban::validateContentPackage(output, "wrong-version"); },
        "content index rejects a different game version");

    std::filesystem::remove(output / "textures/hero.png");
    checkThrows(
        [&] { sokoban::validateContentPackage(output, "1.2.3"); },
        "content index rejects a missing declared file");

    (void)sokoban::stageContent(roots, output, "1.2.3");
    std::ofstream(output / "textures/hero.png", std::ios::app | std::ios::binary)
        << "tampered";
    checkThrows(
        [&] { sokoban::validateContentPackage(output, "1.2.3"); },
        "content index rejects a changed declared size");

    (void)sokoban::stageContent(roots, output, "1.2.3");
    writeFile(output / "unindexed-artifact.bin");
    checkThrows(
        [&] { sokoban::validateContentPackage(output, "1.2.3"); },
        "content index rejects an unindexed package file");

    (void)sokoban::stageContent(roots, output, "1.2.3");
    const std::string validIndex = readFile(output / "content.index");
    const std::size_t finalEntry = validIndex.rfind("file ");
    writeFile(output / "content.index", validIndex.substr(0, finalEntry));
    checkThrows(
        [&] { sokoban::validateContentPackage(output, "1.2.3"); },
        "content index rejects a truncated file list");
}

void testUnassignedLegacySelectorIsStaged()
{
    TempDirectory temp;
    const auto roots = createValidContent(temp.path());
    writeFile(
        roots.levels / "overworld.scr",
        "@selector {\"id\":1,\"cell\":[1,0,1],"
        "\"target\":{\"level\":0,\"screen\":0}}\n"
        "@selector {\"id\":2,\"cell\":[2,0,1],\"target\":null}\n\n"
        "@layer 0\n...\n\n@layer 1\nC  \n");

    const std::filesystem::path output =
        temp.path() / "unassigned-legacy-package/assets";
    (void)sokoban::stageContent(roots, output, "1.2.3");
    check(
        std::filesystem::is_regular_file(output / "levels/overworld.scr"),
        "legacy overworld with an unassigned selector is staged");
}

void testComposedOverworldIsValidatedAndStaged()
{
    TempDirectory temp;
    const auto roots = createValidContent(temp.path());
    std::filesystem::remove(roots.levels / "overworld.scr");
    writeFile(
        roots.levels / "level0/screen1.scr",
        "@layer 0\n...\n\n@layer 1\n.CE\n");
    writeFile(
        roots.levels / "level0/metadata.json",
        R"json({"format":1,"name":"First Light","screens":["Arrival","Repair Me"]})json");
    writeFile(
        roots.levels / "overworld/screen1.scr",
        "@selector {\"id\":1,\"cell\":[1,0,1],"
        "\"target\":{\"level\":0,\"screen\":0}}\n"
        "@selector {\"id\":2,\"cell\":[2,0,1],\"target\":null}\n\n"
        "@layer 0\n...\n\n@layer 1\nC  \n");
    writeFile(
        roots.levels / "overworld/layout.json",
        R"json({
          "format":3,
          "screenSize":[3,1],
          "screens":[{"id":1,"file":"screen1.scr","slot":[0,0]}]
        })json");

    const sokoban::ContentInventory inventory =
        sokoban::collectContentInventory(roots);
    check(
        contains(inventory, "levels/overworld/layout.json"),
        "composed overworld layout included");
    check(
        contains(inventory, "levels/overworld/screen1.scr"),
        "composed overworld screen included");
    check(
        contains(inventory, "levels/level0/screen1.scr"),
        "uncovered puzzle screen does not block composed staging");
    check(
        !contains(inventory, "levels/overworld.scr"),
        "legacy overworld omitted when layout exists");

    const std::filesystem::path output =
        temp.path() / "composed-package/assets";
    (void)sokoban::stageContent(roots, output, "1.2.3");
    check(
        std::filesystem::is_regular_file(
            output / "levels/overworld/layout.json"),
        "composed layout staged");
    check(
        std::filesystem::is_regular_file(
            output / "levels/overworld/screen1.scr"),
        "composed screen staged");
}

void testBakedThumbnailsAreStaged()
{
    TempDirectory temp;
    const auto roots = createValidContent(temp.path());

    // Nothing declares thumbnails - they are editor pictures, not manifest
    // assets - so they used to be dropped. Staging wipes the output root and
    // copies only what it was told about, and every build re-stages, so a bake
    // survived until the next build and the palette then fell back to coloured
    // squares while the files sat untouched in the source tree.
    const std::string wall =
        sokoban::tileThumbnails::assetPathFor(sokoban::TileType::Wall);
    const std::string player =
        sokoban::tileThumbnails::assetPathFor(sokoban::TileType::Player);
    writeFile(roots.assets / wall, "png");
    writeFile(roots.assets / player, "png");

    const sokoban::ContentInventory inventory =
        sokoban::collectContentInventory(roots);
    check(contains(inventory, wall), "baked thumbnail included");
    check(contains(inventory, player), "second baked thumbnail included");

    const std::filesystem::path output = temp.path() / "thumbnails/assets";
    (void)sokoban::stageContent(roots, output, "1.2.3");
    check(
        std::filesystem::is_regular_file(output / wall),
        "baked thumbnail staged to the runtime root");
}

void testMissingThumbnailsAreNotFatal()
{
    TempDirectory temp;
    const auto roots = createValidContent(temp.path());

    // Before the first bake there is nothing to copy. Unlike a manifest asset,
    // a thumbnail that is not there must cost the palette a picture rather
    // than refuse to stage the game at all.
    const sokoban::ContentInventory inventory =
        sokoban::collectContentInventory(roots);
    check(
        !contains(
            inventory,
            sokoban::tileThumbnails::assetPathFor(sokoban::TileType::Wall)),
        "absent thumbnail is not staged");
    check(contains(inventory, "manifest.json"), "staging still succeeds");

    // A partial bake is normal too: one tile present must not drag in the rest.
    const std::string wall =
        sokoban::tileThumbnails::assetPathFor(sokoban::TileType::Wall);
    writeFile(roots.assets / wall, "png");
    const sokoban::ContentInventory partial =
        sokoban::collectContentInventory(roots);
    check(contains(partial, wall), "the one baked thumbnail is staged");
    check(
        !contains(
            partial,
            sokoban::tileThumbnails::assetPathFor(sokoban::TileType::Player)),
        "the tiles never baked are still skipped");
}

void testValidationFailures()
{
    TempDirectory temp;
    auto roots = createValidContent(temp.path());

    std::filesystem::remove(roots.assets / "audio/music.ogg");
    checkThrows([&] { (void)sokoban::collectContentInventory(roots); }, "missing manifest file");
    writeFile(roots.assets / "audio/music.ogg");

    writeFile(roots.assets / "manifest.json", manifest("../outside.png"));
    checkThrows([&] { (void)sokoban::collectContentInventory(roots); }, "asset path traversal");
    writeFile(roots.assets / "manifest.json", manifest());

    writeFile(
        roots.levels / "level0/screen0.scr",
        "@decoration {\"model\":\"MissingDecoration\","
        "\"position\":[0.5,0.5,1.0],\"rotation\":[0,0,0],"
        "\"scale\":[1,1,1]}\n\n"
        "@layer 0\n...\n\n@layer 1\n.CE\n");
    checkThrows(
        [&] { (void)sokoban::collectContentInventory(roots); },
        "unknown decoration model");
    writeFile(
        roots.levels / "level0/screen0.scr",
        "@layer 0\n...\n\n@layer 1\n.CE\n");

    writeFile(
        roots.levels / "level0/screen0.scr",
        "@selector {\"id\":1,\"cell\":[1,0,1],"
        "\"target\":{\"level\":0,\"screen\":0}}\n\n"
        "@layer 0\n...\n\n@layer 1\nC E\n");
    checkThrows(
        [&] { (void)sokoban::collectContentInventory(roots); },
        "selector outside overworld");
    writeFile(
        roots.levels / "level0/screen0.scr",
        "@layer 0\n...\n\n@layer 1\n.CE\n");

    writeFile(
        roots.levels / "overworld.scr",
        "@selector {\"id\":1,\"cell\":[1,0,1],\"target\":null}\n\n"
        "@layer 0\n...\n\n@layer 1\nC  \n");
    check(
        contains(
            sokoban::collectContentInventory(roots),
            "levels/overworld.scr"),
        "an overworld with only an unassigned selector remains stageable");
    writeFile(
        roots.levels / "overworld.scr",
        "@selector {\"id\":1,\"cell\":[1,0,1],"
        "\"target\":{\"level\":4,\"screen\":0}}\n\n"
        "@layer 0\n...\n\n@layer 1\nC  \n");
    checkThrows(
        [&] { (void)sokoban::collectContentInventory(roots); },
        "missing overworld target");
    writeFile(
        roots.levels / "overworld.scr",
        "@selector {\"id\":1,\"cell\":[1,0,1],"
        "\"target\":{\"level\":0,\"screen\":0}}\n\n"
        "@layer 0\n...\n\n@layer 1\nCE \n");
    checkThrows(
        [&] { (void)sokoban::collectContentInventory(roots); },
        "end tile in overworld");
    writeFile(
        roots.levels / "overworld.scr",
        "@selector {\"id\":1,\"cell\":[1,0,1],"
        "\"target\":{\"level\":0,\"screen\":0}}\n\n"
        "@layer 0\n...\n\n@layer 1\nC  \n");

    writeFile(
        roots.levels / "level0/screen1.scr",
        "@layer 0\n...\n\n@layer 1\n.CE\n");
    check(
        contains(
            sokoban::collectContentInventory(roots),
            "levels/level0/screen1.scr"),
        "missing selector coverage remains repairable after startup");
    std::filesystem::remove(roots.levels / "level0/screen1.scr");

    std::filesystem::create_directories(roots.levels / "level2");
    writeFile(roots.levels / "level2/screen0.scr", "@layer 0\n...\n\n@layer 1\n.CE\n");
    checkThrows([&] { (void)sokoban::collectContentInventory(roots); }, "non-contiguous levels");

    checkThrows(
        [&] { (void)sokoban::stageContent(roots, roots.assets, "1.0"); },
        "source root cannot be staging output");
}

} // namespace

int main()
{
    try {
        testInventoryAndStaging();
        testStagedContentIndexValidation();
        testUnassignedLegacySelectorIsStaged();
        testComposedOverworldIsValidatedAndStaged();
        testBakedThumbnailsAreStaged();
        testMissingThumbnailsAreNotFatal();
        testValidationFailures();
    } catch (const std::exception& error) {
        std::cerr << "UNEXPECTED EXCEPTION: " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "UNEXPECTED NON-STANDARD EXCEPTION\n";
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " content pipeline checks failed\n";
        return 1;
    }
    std::cout << "All content pipeline checks passed\n";
    return 0;
}
