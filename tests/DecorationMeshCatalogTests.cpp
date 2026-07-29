#include "engine/DecorationMeshCatalog.hpp"

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

sokoban::AssetManifest manifest()
{
    return sokoban::AssetManifest::parse(R"json({
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
    })json");
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

} // namespace

int main()
{
    testCatalogScansSupportedFilesAndResolvesManifestModels();
    testMissingRootFailsWithoutStaleEntries();

    if (failures == 0) {
        std::cout << "DecorationMeshCatalogTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "DecorationMeshCatalogTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
