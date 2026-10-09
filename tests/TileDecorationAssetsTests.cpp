#include "TestAssetRoot.hpp"
#include "TestHarness.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/ContentPipeline.hpp"
#include "engine/Level.hpp"
#include "engine/render/GltfMesh.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/render/RuntimeTextureCatalog.hpp"
#include "engine/render/TextureSourceLoader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace sokoban;

struct Bounds {
    Vec3 minimum {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vec3 maximum {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
};

void checkDecorationMesh(const MeshData& mesh, bool grass, std::string_view layout)
{
    CHECK(!mesh.vertices.empty());
    CHECK(!mesh.indices.empty());
    CHECK(mesh.indices.size() % 3 == 0);
    CHECK(mesh.materials.size() == 1);
    Bounds bounds;
    bool finitePositions = true;
    bool unitNormals = true;
    bool paletteUvs = true;
    bool validMaterialIndices = true;
    float minimumU = 1.0f;
    float maximumU = 0.0f;
    float minimumV = 1.0f;
    float maximumV = 0.0f;
    for (const MeshVertex& vertex : mesh.vertices) {
        finitePositions &= std::isfinite(vertex.position.x) &&
            std::isfinite(vertex.position.y) && std::isfinite(vertex.position.z);
        const float normalLengthSquared = vertex.normal.x * vertex.normal.x +
            vertex.normal.y * vertex.normal.y + vertex.normal.z * vertex.normal.z;
        unitNormals &= std::isfinite(normalLengthSquared) &&
            std::abs(normalLengthSquared - 1.0f) < 0.001f;
        paletteUvs &= std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y) &&
            vertex.uv.x >= 0.0f && vertex.uv.x <= 1.0f &&
            vertex.uv.y >= 0.0f && vertex.uv.y <= 1.0f;
        validMaterialIndices &= vertex.materialIndex == 0;
        minimumU = std::min(minimumU, vertex.uv.x);
        maximumU = std::max(maximumU, vertex.uv.x);
        minimumV = std::min(minimumV, vertex.uv.y);
        maximumV = std::max(maximumV, vertex.uv.y);
        bounds.minimum.x = std::min(bounds.minimum.x, vertex.position.x);
        bounds.minimum.y = std::min(bounds.minimum.y, vertex.position.y);
        bounds.minimum.z = std::min(bounds.minimum.z, vertex.position.z);
        bounds.maximum.x = std::max(bounds.maximum.x, vertex.position.x);
        bounds.maximum.y = std::max(bounds.maximum.y, vertex.position.y);
        bounds.maximum.z = std::max(bounds.maximum.z, vertex.position.z);
    }
    CHECK(finitePositions);
    CHECK(unitNormals);
    CHECK(paletteUvs);
    CHECK(validMaterialIndices);
    CHECK(maximumU > minimumU || maximumV > minimumV);
    CHECK(std::ranges::all_of(mesh.indices, [&](uint32_t index) {
        return index < mesh.vertices.size();
    }));

    // Partial-edge meshes retain the authored tile-centred pivot and their
    // shallow surface height. Fitting to a unit cube would break both.
    CHECK(bounds.minimum.x >= -0.5001f);
    CHECK(bounds.maximum.x <= 0.5001f);
    CHECK(bounds.minimum.y >= -0.5001f);
    CHECK(bounds.maximum.y <= 0.5001f);
    CHECK(bounds.minimum.z >= -0.0151f);
    CHECK(bounds.minimum.z < 0.0f);
    CHECK(bounds.maximum.z > 0.01f);
    CHECK(bounds.maximum.z <= (grass ? 0.1701f : 0.0601f));
    if (layout == "Edge") {
        CHECK(bounds.maximum.y < 0.0f);
        CHECK(bounds.minimum.y < -0.30f);
    } else if (layout == "Strip") {
        CHECK(bounds.minimum.y < -0.30f);
        CHECK(bounds.maximum.y > 0.30f);
    }

    if (mesh.materials.size() == 1) {
        const MeshMaterial& material = mesh.materials[0];
        CHECK(material.alphaMode == MaterialAlphaMode::Opaque);
        CHECK(material.metallicFactor == 0.0f);
        CHECK(std::abs(material.roughnessFactor - (grass ? 0.85f : 0.91f)) < 0.001f);
        CHECK(material.baseColorTexture != 0);
        CHECK(material.baseColorUvSet == 0);
    }
}

void testGameReadyTileDecorations()
{
    TEST("game ready tile decorations");
    const std::filesystem::path assets = testAssetRoot();
    const AssetManifest manifest = AssetManifest::loadFromFile(assets / "manifest.json");
    const RuntimeTextureCatalog textures = collectRuntimeTextureCatalog(assets, manifest);
    std::vector<Level::Decoration> decorations;
    for (const std::string_view style : { "Pebbles", "Grass" }) {
        for (const std::string_view layout : { "Edge", "Corner", "Strip", "End" }) {
            for (int variant = 1; variant <= 4; ++variant) {
                const std::string name = "TileDecoration" + std::string(style) +
                    std::string(layout) + "0" + std::to_string(variant);
                TEST(name.c_str());
                const RenderModel id = manifest.modelIdByName(name);
                CHECK(!id.isCube());
                const AssetManifest::Model& definition = manifest.model(id);
                CHECK(definition.geometry == ModelGeometry::Static);
                CHECK(definition.preserveSourceScale);
                CHECK(!definition.rotateHalfTurn);
                CHECK(definition.materialMode == ModelMaterialMode::Auto);
                CHECK(definition.path.starts_with("custom/models/tile_decorations/tile_"));
                CHECK(std::filesystem::is_regular_file(assets / definition.path));
                CHECK(resolveGltfExternalFiles(assets, definition.path, name).empty());

                const RuntimeModelTextures& runtime = textures.model(
                    static_cast<uint32_t>(id.index()));
                CHECK(runtime.preparedBytes > 0);
                CHECK(runtime.materialMode == ModelMaterialMode::PrimitiveMaterials);
                CHECK(runtime.primitiveMaterials.size() == 1);
                CHECK(runtime.requiredTextures.size() == 1);
                if (runtime.primitiveMaterials.size() == 1 &&
                    runtime.requiredTextures.size() == 1) {
                    const PrimitiveMaterialBinding& binding = runtime.primitiveMaterials[0];
                    CHECK(binding.bindBaseColorTexture);
                    CHECK(binding.textureIndex == runtime.requiredTextures[0]);
                    const RuntimeTextureDefinition& palette = textures.textures()[binding.textureIndex];
                    CHECK(!palette.manifestOwned);
                    CHECK(palette.identity.interpretation.colorSpace == TextureColorSpace::Srgb);
                    CHECK(std::holds_alternative<GltfBufferViewTextureSource>(palette.identity.source));
                    const ImageData image = loadRgbaTextureSource(assets, palette.identity.source);
                    CHECK(image.width > 0 && image.height > 0);
                    CHECK(image.rgba.size() == static_cast<std::size_t>(image.width) * image.height * 4);
                    bool opaque = true;
                    for (std::size_t alpha = 3; alpha < image.rgba.size(); alpha += 4) {
                        opaque &= image.rgba[alpha] == std::byte { 255 };
                    }
                    CHECK(opaque);
                }
                const MeshData mesh = loadGltfMesh(assets / definition.path, {
                    .preserveSourceScale = definition.preserveSourceScale,
                    .primitiveMaterials = runtime.primitiveMaterials,
                });
                checkDecorationMesh(mesh, style == "Grass", layout);
                decorations.push_back({ .model = name, .position = { 0.5f, 0.5f, 1.0f } });
            }
        }
    }
    TEST("tile decoration level requirements");
    CHECK(decorations.size() == 32);
    CHECK(std::ranges::count_if(manifest.models(), [](const auto& model) {
        return model.name.starts_with("TileDecoration");
    }) == 32);
    const Level level = Level::loadFromLayers({ { "." }, { "C" } },
        "tile decorations", std::nullopt, decorations);
    const RenderAssetRequirements requirements = renderAssetRequirementsForLevel(level, manifest);
    for (const Level::Decoration& decoration : decorations) {
        CHECK(requirements.contains(manifest.modelIdByName(decoration.model)));
    }
}

} // namespace

int main()
{
    try {
        testGameReadyTileDecorations();
    } catch (const std::exception& error) {
        std::cerr << "TileDecorationAssetsTests: " << error.what() << '\n';
        return 1;
    }
    if (failures == 0) {
        std::cout << "TileDecorationAssetsTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "TileDecorationAssetsTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
