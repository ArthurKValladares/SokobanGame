#include "engine/render/GltfMesh.hpp"
#include "engine/render/VulkanRenderConstants.hpp"

#include <iostream>

namespace {

using namespace sokoban;

int failures = 0;
int checks = 0;
const char* currentTest = "";

void checkImpl(bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] line " << line
                  << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

void testFallbackEntryZeroDefaults()
{
    TEST("fallbackEntryZeroDefaults");
    const GpuMaterial fallback {};

    CHECK(fallback.baseColorFactor == (Vec4 { 1.0f, 1.0f, 1.0f, 1.0f }));
    CHECK(fallback.emissiveAndMetallic == (Vec4 {}));
    CHECK(fallback.materialScalars == (Vec4 { 1.0f, 1.0f, 1.0f, 0.5f }));
    CHECK(fallback.primaryTextureHandles.x == 0U);
    CHECK(fallback.primaryTextureHandles.y == 0U);
    CHECK(fallback.primaryTextureHandles.z == 0U);
    CHECK(fallback.primaryTextureHandles.w == 0U);
    CHECK(fallback.occlusionTextureAndPadding.x == 0U);
    CHECK(fallback.textureUvSets.x == 0U);
    CHECK(fallback.textureUvSets.y == 0U);
    CHECK(fallback.textureUvSets.z == 0U);
    CHECK(fallback.textureUvSets.w == 0U);
    CHECK(fallback.materialState.x == 0U);
    CHECK(fallback.materialState.y == 0U);
    CHECK(fallback.materialState.z == 0U);
    CHECK(fallback.materialState.w == 0U);
}

void testDefaultMeshMaterialConversion()
{
    TEST("defaultMeshMaterialConversion");
    const GpuMaterial material = gpuMaterialFrom(MeshMaterial {});

    CHECK(material.baseColorFactor == (Vec4 { 1.0f, 1.0f, 1.0f, 1.0f }));
    CHECK(material.emissiveAndMetallic == (Vec4 { 0.0f, 0.0f, 0.0f, 1.0f }));
    CHECK(material.materialScalars == (Vec4 { 1.0f, 1.0f, 1.0f, 0.5f }));
    CHECK(material.primaryTextureHandles.x == 0U);
    CHECK(material.occlusionTextureAndPadding.x == 0U);
    CHECK(material.materialState.y ==
        static_cast<uint32_t>(MaterialAlphaMode::Opaque));
    CHECK(material.materialState.z == PrimitiveMaterialNone);
    CHECK(material.materialState.w == 0U);
}

void testCompleteMeshMaterialConversion()
{
    TEST("completeMeshMaterialConversion");
    MeshMaterial source;
    source.baseColorFactor = { 0.1f, 0.2f, 0.3f, 0.4f };
    source.emissiveFactor = { 0.5f, 0.6f, 0.7f };
    source.metallicFactor = 0.8f;
    source.roughnessFactor = 0.9f;
    source.normalScale = 0.25f;
    source.occlusionStrength = 0.75f;
    source.alphaCutoff = 0.35f;
    source.baseColorTexture = 11;
    source.normalTexture = 12;
    source.metallicRoughnessTexture = 13;
    source.emissiveTexture = 14;
    source.occlusionTexture = 15;
    source.baseColorUvSet = 0;
    source.normalUvSet = 1;
    source.metallicRoughnessUvSet = 1;
    source.emissiveUvSet = 0;
    source.occlusionUvSet = 1;
    source.alphaMode = MaterialAlphaMode::Blend;
    source.flags = PrimitiveMaterialScrollV;
    source.doubleSided = true;

    const GpuMaterial material = gpuMaterialFrom(source);
    CHECK(material.baseColorFactor == source.baseColorFactor);
    CHECK(material.emissiveAndMetallic == (Vec4 { 0.5f, 0.6f, 0.7f, 0.8f }));
    CHECK(material.materialScalars == (Vec4 { 0.9f, 0.25f, 0.75f, 0.35f }));
    CHECK(material.primaryTextureHandles.x == 11U);
    CHECK(material.primaryTextureHandles.y == 12U);
    CHECK(material.primaryTextureHandles.z == 13U);
    CHECK(material.primaryTextureHandles.w == 14U);
    CHECK(material.occlusionTextureAndPadding.x == 15U);
    CHECK(material.occlusionTextureAndPadding.y == 0U);
    CHECK(material.occlusionTextureAndPadding.z == 0U);
    CHECK(material.occlusionTextureAndPadding.w == 0U);
    CHECK(material.textureUvSets.x == 0U);
    CHECK(material.textureUvSets.y == 1U);
    CHECK(material.textureUvSets.z == 1U);
    CHECK(material.textureUvSets.w == 0U);
    CHECK(material.materialState.x == 1U);
    CHECK(material.materialState.y ==
        static_cast<uint32_t>(MaterialAlphaMode::Blend));
    CHECK(material.materialState.z == PrimitiveMaterialScrollV);
    CHECK(material.materialState.w == 1U);
}

} // namespace

int main()
{
    testFallbackEntryZeroDefaults();
    testDefaultMeshMaterialConversion();
    testCompleteMeshMaterialConversion();

    if (failures == 0) {
        std::cout << "GpuMaterialTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "GpuMaterialTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
