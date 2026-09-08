#include "TestHarness.hpp"

#include "engine/DecorationMeshCatalog.hpp"
#include "engine/DecorationAssetRegistry.hpp"
#include "engine/ContentPipeline.hpp"
#include "engine/render/GltfMesh.hpp"

#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

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

void appendUint32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
    bytes.push_back(static_cast<uint8_t>(value >> 16U));
    bytes.push_back(static_cast<uint8_t>(value >> 24U));
}

void appendUint16(std::vector<uint8_t>& bytes, uint16_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
}

void appendFloat(std::vector<uint8_t>& bytes, float value)
{
    appendUint32(bytes, std::bit_cast<uint32_t>(value));
}

std::vector<uint8_t> triangleBytes()
{
    std::vector<uint8_t> bytes;
    for (const float value : {
             0.0f, 0.0f, 0.0f,
             1.0f, 0.0f, 0.0f,
             0.0f, 1.0f, 0.0f,
             0.0f, 0.0f, 1.0f,
             0.0f, 0.0f, 1.0f,
             0.0f, 0.0f, 1.0f,
             0.0f, 0.0f,
             1.0f, 0.0f,
             0.0f, 1.0f,
         }) {
        appendFloat(bytes, value);
    }
    appendUint16(bytes, 0);
    appendUint16(bytes, 1);
    appendUint16(bytes, 2);
    return bytes;
}

void writeBytes(
    const std::filesystem::path& path,
    std::span<const uint8_t> bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

std::string triangleJson(
    std::string_view bufferUri,
    std::string_view imageUri)
{
    std::ostringstream json;
    json << R"json({
  "asset":{"version":"2.0"},
  "buffers":[{"uri":")json" << bufferUri << R"json(","byteLength":102}],
  "bufferViews":[
    {"buffer":0,"byteOffset":0,"byteLength":36},
    {"buffer":0,"byteOffset":36,"byteLength":36},
    {"buffer":0,"byteOffset":72,"byteLength":24},
    {"buffer":0,"byteOffset":96,"byteLength":6}
  ],
  "accessors":[
    {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
    {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
    {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
    {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}
  ],
  "images":[
    {"uri":")json" << imageUri << R"json("},
    {"uri":"data:image/png;base64,AAAA"}
  ],
  "textures":[{"source":0}],
  "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],
  "meshes":[{"primitives":[{
    "attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},
    "indices":3,
    "material":0
  }]}],
  "extras":{"uri":"decoy-that-is-not-a-dependency.bin"}
})json";
    return json.str();
}

void writeGlb(const std::filesystem::path& path, std::string json)
{
    while (json.size() % 4 != 0) {
        json.push_back(' ');
    }
    std::vector<uint8_t> bytes;
    appendUint32(bytes, 0x46546C67U);
    appendUint32(bytes, 2U);
    appendUint32(bytes, 20U + static_cast<uint32_t>(json.size()));
    appendUint32(bytes, static_cast<uint32_t>(json.size()));
    appendUint32(bytes, 0x4E4F534AU);
    bytes.insert(bytes.end(), json.begin(), json.end());
    writeBytes(path, bytes);
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

void initializePackage(
    const std::filesystem::path& source,
    const std::filesystem::path& runtime)
{
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(runtime);
    std::ofstream(source / "manifest.json") << manifestJson;
    std::ofstream(runtime / "manifest.json") << manifestJson;
    std::ofstream(runtime / "content.index", std::ios::binary)
        << "format 1\ngame-version editor-test\n";
}

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
    initializePackage(source, runtime);
    std::filesystem::create_directories(source / "models/geometry");
    std::filesystem::create_directories(source / "models/textures");
    std::ofstream(source / "models/tree.gltf")
        << triangleJson("geometry/tree.bin", "textures/tree.png");
    writeBytes(source / "models/geometry/tree.bin", triangleBytes());
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
    if (!added.succeeded) {
        std::cerr << added.status << '\n';
    }

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
    CHECK(std::filesystem::exists(runtime / "models/geometry/tree.bin"));
    CHECK(std::filesystem::exists(runtime / "models/textures/tree.png"));
    CHECK(!std::filesystem::exists(
        runtime / "models/decoy-that-is-not-a-dependency.bin"));
    CHECK(sokoban::loadGltfMesh(runtime / "models/tree.gltf").vertices.size() ==
        3);

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
    sokoban::validateContentPackage(runtime, "editor-test");

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

    std::filesystem::create_directories(
        source / "models/nested/geometry");
    std::filesystem::create_directories(
        source / "models/nested/textures");
    writeGlb(
        source / "models/nested/rock.glb",
        triangleJson("geometry/rock.bin", "textures/rock.png"));
    writeBytes(
        source / "models/nested/geometry/rock.bin", triangleBytes());
    touch(source / "models/nested/textures/rock.png");

    const sokoban::DecorationAssetRegistry::Result glbAdded =
        sokoban::DecorationAssetRegistry::registerMesh({
            .sourceAssetRoot = source,
            .runtimeAssetRoot = runtime,
            .relativeMeshPath = "models/nested/rock.glb",
            .runtimeManifest = live,
            .manifestEditor = editor,
        });
    if (!glbAdded.succeeded) {
        std::cerr << glbAdded.status << '\n';
    }
    CHECK(glbAdded.succeeded);
    CHECK(glbAdded.added);
    CHECK(glbAdded.modelName == "Decoration_rock");
    CHECK(live.models().size() == 4);
    CHECK(live.textures().size() == 2);
    CHECK(live.textures().back().path ==
        "models/nested/textures/rock.png");
    CHECK(std::filesystem::exists(runtime / "models/nested/rock.glb"));
    CHECK(std::filesystem::exists(
        runtime / "models/nested/geometry/rock.bin"));
    CHECK(std::filesystem::exists(
        runtime / "models/nested/textures/rock.png"));
    CHECK(!std::filesystem::exists(
        runtime / "models/nested/decoy-that-is-not-a-dependency.bin"));
    CHECK(sokoban::loadGltfMesh(
              runtime / "models/nested/rock.glb").vertices.size() == 3);
    const sokoban::AssetManifest publishedGlbManifest =
        sokoban::AssetManifest::loadFromFile(runtime / "manifest.json");
    CHECK(publishedGlbManifest.modelIdByName(glbAdded.modelName).value == 4);
    CHECK(publishedGlbManifest.textures().size() == 2);
    sokoban::validateContentPackage(runtime, "editor-test");
}

void testRegistrationRejectsMissingAndEscapingDependenciesBeforePublication()
{
    TemporaryDirectory directory;
    const std::filesystem::path source = directory.path / "source";
    const std::filesystem::path runtime = directory.path / "runtime";
    initializePackage(source, runtime);
    std::filesystem::create_directories(source / "models/textures");
    writeGlb(
        source / "models/missing.glb",
        triangleJson("geometry/missing.bin", "textures/present.png"));
    touch(source / "models/textures/present.png");

    sokoban::AssetManifest live = manifest();
    sokoban::AssetManifestEditor editor;
    editor.initialize(source / "manifest.json");
    const sokoban::DecorationAssetRegistry::Result missing =
        sokoban::DecorationAssetRegistry::registerMesh({
            .sourceAssetRoot = source,
            .runtimeAssetRoot = runtime,
            .relativeMeshPath = "models/missing.glb",
            .runtimeManifest = live,
            .manifestEditor = editor,
        });
    CHECK(!missing.succeeded);
    CHECK(missing.status.find("missing") != std::string::npos);
    CHECK(live.models().size() == 2);
    CHECK(editor.models().size() == 2);
    CHECK(!std::filesystem::exists(runtime / "models/missing.glb"));
    sokoban::validateContentPackage(runtime, "editor-test");

    std::ofstream(source / "models/escaping.gltf")
        << triangleJson("../../outside.bin", "textures/present.png");
    const sokoban::DecorationAssetRegistry::Result escaping =
        sokoban::DecorationAssetRegistry::registerMesh({
            .sourceAssetRoot = source,
            .runtimeAssetRoot = runtime,
            .relativeMeshPath = "models/escaping.gltf",
            .runtimeManifest = live,
            .manifestEditor = editor,
        });
    CHECK(!escaping.succeeded);
    CHECK(escaping.status.find("escapes") != std::string::npos);
    CHECK(live.models().size() == 2);
    CHECK(editor.models().size() == 2);
    CHECK(!std::filesystem::exists(runtime / "models/escaping.gltf"));
    sokoban::validateContentPackage(runtime, "editor-test");
}

} // namespace

int main()
{
    testCatalogScansSupportedFilesAndResolvesManifestModels();
    testMissingRootFailsWithoutStaleEntries();
    testRegistrationPopulatesManifestsAndStagesGltfDependencies();
    testRegistrationRejectsMissingAndEscapingDependenciesBeforePublication();

    if (failures == 0) {
        std::cout << "DecorationMeshCatalogTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "DecorationMeshCatalogTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
