#include "engine/ContentPipeline.hpp"

#include <chrono>
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

std::string manifest(std::string_view texturePath = "textures/hero.png")
{
    return R"json({
  "format": 1,
  "textures": [
    { "name": "HeroTexture", "path": ")json" + std::string(texturePath) + R"json(" }
  ],
  "models": [
    {
      "name": "Hero",
      "path": "models/hero.gltf",
      "geometry": "skinned",
      "material": { "mode": "texture", "texture": "HeroTexture" },
      "role": "player"
    }
  ],
  "animations": [
    { "name": "Idle", "path": "animations/idle.glb", "role": "player-idle" },
    { "name": "Move", "path": "animations/move.glb", "role": "player-move" },
    { "name": "Push", "path": "animations/push.glb", "role": "player-push" }
  ],
  "sounds": [
    { "name": "footsteps", "files": ["audio/step.ogg"] }
  ],
  "music": [
    { "level": 0, "file": "audio/music.ogg" }
  ]
})json";
}

sokoban::ContentSourceRoots createValidContent(const std::filesystem::path& root)
{
    const std::filesystem::path assets = root / "source-assets";
    const std::filesystem::path levels = root / "source-levels";
    const std::filesystem::path shaders = root / "compiled-shaders";

    writeFile(assets / "manifest.json", manifest());
    writeFile(assets / "textures/hero.png");
    writeFile(assets / "ui/Karla-Regular.ttf");
    writeFile(assets / "ui/OFL.txt", "font license");
    writeFile(assets / "custom/ui/main-menu-rogue-pushing-rock-4k.png");
    writeFile(assets / "models/hero.gltf", R"({"buffers":[{"uri":"hero.bin"}]})");
    writeFile(assets / "models/hero.bin");
    writeFile(assets / "models/LICENSE.txt", "model license");
    writeFile(assets / "animations/idle.glb");
    writeFile(assets / "animations/move.glb");
    writeFile(assets / "animations/push.glb");
    writeFile(assets / "audio/step.ogg");
    writeFile(assets / "audio/music.ogg");
    writeFile(levels / "level0/screen0.scr", "@layer 0\n...\n\n@layer 1\n.CE\n");
    writeFile(levels / "Deleted/level9/screen0.scr", "not shipped");

    constexpr const char* shaderNames[] {
        "triangle.vert.glsl.spv",
        "triangle.frag.glsl.spv",
        "shadow.vert.glsl.spv",
        "model.vert.glsl.spv",
        "model_shadow.vert.glsl.spv",
        "fullscreen.vert.glsl.spv",
        "ssao.frag.glsl.spv",
        "ssao_composite.frag.glsl.spv",
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
    check(contains(inventory, "models/hero.gltf"), "model included");
    check(contains(inventory, "models/hero.bin"), "external glTF buffer included");
    check(contains(inventory, "models/LICENSE.txt"), "nearby asset license included");
    check(contains(inventory, "ui/Karla-Regular.ttf"), "UI font included");
    check(contains(inventory, "ui/OFL.txt"), "UI font license included");
    check(
        contains(inventory, "custom/ui/main-menu-rogue-pushing-rock-4k.png"),
        "title background included");
    check(contains(inventory, "levels/level0/screen0.scr"), "playable level included");
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
    testInventoryAndStaging();
    testValidationFailures();

    if (failures != 0) {
        std::cerr << failures << " content pipeline checks failed\n";
        return 1;
    }
    std::cout << "All content pipeline checks passed\n";
    return 0;
}
