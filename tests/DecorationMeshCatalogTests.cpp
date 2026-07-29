#include "engine/DecorationMeshCatalog.hpp"
#include "engine/DecorationAssetRegistry.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

int failures = 0;
int checks = 0;

void checkImpl(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

struct TemporaryDirectory {
    TemporaryDirectory()
    {
        const auto unique =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("sokoban_decoration_catalog_" + std::to_string(unique));
        std::filesystem::create_directories(path / "nested");
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;
};

void touch(const std::filesystem::path& path)
{
    std::ofstream(path) << "{}";
}

constexpr std::string_view manifestJson = R"json({
  "format": 1,
  "textures": [],
  "models": [
    { "name": "Registered", "path": "registered.gltf" },
    { "name": "Hero", "path": "hero.glb", "geometry": "skinned", "role": "player" }
  ],
  "animations": [
    { "name": "Idle", "path": "hero.glb", "role": "player-idle" },
    { "name": "Move", "path": "hero.glb", "role": "player-move" },
    { "name": "Push", "path": "hero.glb", "role": "player-push" },
    { "name": "Death", "path": "hero.glb", "role": "player-death" },
    { "name": "Dead", "path": "hero.glb", "role": "player-dead-idle" }
  ],
  "tiles": []
})json";

sokoban::AssetManifest manifest()
{
    return sokoban::AssetManifest::parse(manifestJson);
}

void testCatalogScansSupportedFilesAndResolvesManifestModels()
{
    TemporaryDirectory directory;
    touch(directory.path / "registered.gltf");
    touch(directory.path / "nested" / "loose.glb");
    touch(directory.path / "ignored.obj");
    touch(directory.path / "ignored.png");

    sokoban::DecorationMeshCatalog catalog;
    CHECK(catalog.refresh(directory.path, manifest()));
    CHECK(catalog.entries().size() == 2);
    CHECK(catalog.entries()[0].registered());
    CHECK(catalog.entries()[0].modelName == "Registered");
    CHECK(catalog.entries()[0].relativePath == "registered.gltf");
    CHECK(!catalog.entries()[1].registered());
    CHECK(catalog.entries()[1].relativePath ==
        std::filesystem::path("nested/loose.glb"));
    CHECK(catalog.status().find("1 registered") != std::string::npos);
}

void testMissingRootFailsWithoutStaleEntries()
{
    sokoban::DecorationMeshCatalog catalog;
    CHECK(!catalog.refresh(
        std::filesystem::temp_directory_path() /
            "sokoban_missing_decoration_mesh_root",
        manifest()));
    CHECK(catalog.entries().empty());
    CHECK(catalog.status().find("unavailable") != std::string::npos);
}

void testRegistrationPopulatesManifestsAndStagesGltfDependencies()
{
    TemporaryDirectory directory;
    const std::filesystem::path source = directory.path / "source";
    const std::filesystem::path runtime = directory.path / "runtime";
    std::filesystem::create_directories(source / "models/textures");
    std::filesystem::create_directories(runtime);
    std::ofstream(source / "manifest.json") << manifestJson;
    std::ofstream(source / "models/tree.gltf") << R"json({
      "buffers": [{ "uri": "tree.bin" }],
      "images": [{ "uri": "textures/tree.png" }],
      "textures": [{ "source": 0 }],
      "materials": [{
        "pbrMetallicRoughness": {
          "baseColorTexture": { "index": 0 }
        }
      }]
    })json";
    touch(source / "models/tree.bin");
    touch(source / "models/textures/tree.png");

    sokoban::AssetManifest live = manifest();
    sokoban::AssetManifestEditor editor;
    editor.initialize(source / "manifest.json");
    const sokoban::DecorationAssetRegistry::Result added =
        sokoban::DecorationAssetRegistry::registerMesh({
            .sourceAssetRoot = source,
            .runtimeAssetRoot = runtime,
            .relativeMeshPath = "models/tree.gltf",
            .runtimeManifest = live,
            .manifestEditor = editor,
        });

    CHECK(added.succeeded);
    CHECK(added.added);
    CHECK(added.modelName == "Decoration_tree");
    CHECK(live.models().size() == 3);
    CHECK(live.textures().size() == 1);
    CHECK(live.modelIdByName(added.modelName).value == 3);
    const sokoban::AssetManifest::Model& liveModel =
        live.model(live.modelIdByName(added.modelName));
    CHECK(liveModel.preserveSourceScale);
    CHECK(liveModel.materialMode ==
        sokoban::ModelMaterialMode::SingleTexture);
    CHECK(liveModel.materialTextureName ==
        "DecorationTexture_tree");
    CHECK(live.textures().front().path ==
        "models/textures/tree.png");
    CHECK(std::filesystem::exists(runtime / "models/tree.gltf"));
    CHECK(std::filesystem::exists(runtime / "models/tree.bin"));
    CHECK(std::filesystem::exists(runtime / "models/textures/tree.png"));

    const sokoban::AssetManifest sourceManifest =
        sokoban::AssetManifest::loadFromFile(source / "manifest.json");
    const sokoban::AssetManifest runtimeManifest =
        sokoban::AssetManifest::loadFromFile(runtime / "manifest.json");
    CHECK(sourceManifest.modelIdByName(added.modelName).value == 3);
    CHECK(runtimeManifest.modelIdByName(added.modelName).value == 3);
    CHECK(sourceManifest.model(
        sourceManifest.modelIdByName(added.modelName)).preserveSourceScale);
    CHECK(sourceManifest.textures().size() == 1);
    CHECK(runtimeManifest.textures().size() == 1);

    const sokoban::DecorationAssetRegistry::Result repeated =
        sokoban::DecorationAssetRegistry::registerMesh({
            .sourceAssetRoot = source,
            .runtimeAssetRoot = runtime,
            .relativeMeshPath = "models/tree.gltf",
            .runtimeManifest = live,
            .manifestEditor = editor,
        });
    CHECK(repeated.succeeded);
    CHECK(!repeated.added);
    CHECK(repeated.modelName == added.modelName);
    CHECK(live.models().size() == 3);
    CHECK(live.textures().size() == 1);
}

} // namespace

int main()
{
    testCatalogScansSupportedFilesAndResolvesManifestModels();
    testMissingRootFailsWithoutStaleEntries();
    testRegistrationPopulatesManifestsAndStagesGltfDependencies();

    if (failures == 0) {
        std::cout << "DecorationMeshCatalogTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "DecorationMeshCatalogTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
