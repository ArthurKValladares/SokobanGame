#include "engine/render/GltfMesh.hpp"

#include <chrono>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
        std::cerr << "FAIL [" << currentTest << "] line " << line << ": "
                  << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

class TempDirectory {
public:
    TempDirectory()
    {
        const auto id =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("sokoban-gltf-dependencies-" + std::to_string(id));
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

void writeTextFile(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
}

void appendUint32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
    bytes.push_back(static_cast<uint8_t>(value >> 16U));
    bytes.push_back(static_cast<uint8_t>(value >> 24U));
}

void appendFloat(std::vector<uint8_t>& bytes, float value)
{
    appendUint32(bytes, std::bit_cast<uint32_t>(value));
}

void appendUint16(std::vector<uint8_t>& bytes, uint16_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
}

void writeGlb(
    const std::filesystem::path& path,
    std::string json,
    std::span<const uint8_t> binary)
{
    while (json.size() % 4 != 0) {
        json.push_back(' ');
    }
    std::vector<uint8_t> paddedBinary(binary.begin(), binary.end());
    while (paddedBinary.size() % 4 != 0) {
        paddedBinary.push_back(0);
    }

    constexpr uint32_t headerSize = 12;
    constexpr uint32_t chunkHeaderSize = 8;
    const uint32_t totalSize = headerSize + chunkHeaderSize +
        static_cast<uint32_t>(json.size()) + chunkHeaderSize +
        static_cast<uint32_t>(paddedBinary.size());

    std::vector<uint8_t> bytes;
    bytes.reserve(totalSize);
    appendUint32(bytes, 0x46546C67); // glTF
    appendUint32(bytes, 2);
    appendUint32(bytes, totalSize);
    appendUint32(bytes, static_cast<uint32_t>(json.size()));
    appendUint32(bytes, 0x4E4F534A); // JSON
    bytes.insert(bytes.end(), json.begin(), json.end());
    appendUint32(bytes, static_cast<uint32_t>(paddedBinary.size()));
    appendUint32(bytes, 0x004E4942); // BIN
    bytes.insert(bytes.end(), paddedBinary.begin(), paddedBinary.end());

    std::ofstream stream(path, std::ios::binary);
    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

void testInspectsExternalAndDataUriDependenciesWithoutLoadingThem()
{
    TEST("inspectsExternalAndDataUriDependenciesWithoutLoadingThem");
    TempDirectory temp;
    const std::filesystem::path model = temp.path() / "external.gltf";
    writeTextFile(model, R"json({
  "asset":{"version":"2.0"},
  "extensionsUsed":["KHR_texture_transform"],
  "buffers":[
    {"name":"Missing external buffer","uri":"missing.bin","byteLength":16},
    {"name":"Inline buffer","uri":"data:application/octet-stream;base64,AAAAAA==","byteLength":4}
  ],
  "images":[
    {"name":"Albedo","uri":"textures/albedo.png"},
    {"name":"Packed","uri":"data:image/png;base64,AAAA","mimeType":"image/png"}
  ],
  "samplers":[{
    "name":"Authored sampler",
    "magFilter":9729,
    "minFilter":9987,
    "wrapS":33071,
    "wrapT":33648
  }],
  "textures":[
    {"name":"Albedo texture","source":0,"sampler":0},
    {"name":"Packed texture","source":1}
  ],
  "materials":[{
    "name":"Painted metal",
    "pbrMetallicRoughness":{
      "baseColorTexture":{
        "index":0,
        "texCoord":1,
        "extensions":{"KHR_texture_transform":{
          "offset":[0.25,0.5],"scale":[2.0,3.0],"rotation":0.75,"texCoord":0
        }}
      },
      "metallicRoughnessTexture":{"index":1}
    },
    "normalTexture":{"index":1,"scale":0.25},
    "occlusionTexture":{"index":0,"strength":0.5},
    "emissiveTexture":{"index":0,"texCoord":1}
  }]
})json");

    // Neither missing.bin nor textures/albedo.png exists. Successful
    // inspection proves this API reads document structure only.
    const GltfAssetDependencies dependencies =
        inspectGltfAssetDependencies(model);

    CHECK(dependencies.buffers.size() == 2);
    CHECK(dependencies.buffers[0].name == "Missing external buffer");
    CHECK(dependencies.buffers[0].sourceKind ==
        GltfBufferSourceKind::ExternalUri);
    CHECK(dependencies.buffers[0].uri == "missing.bin");
    CHECK(dependencies.buffers[0].byteLength == 16);
    CHECK(dependencies.buffers[1].sourceKind == GltfBufferSourceKind::DataUri);

    CHECK(dependencies.images.size() == 2);
    CHECK(dependencies.images[0].sourceKind ==
        GltfImageSourceKind::ExternalUri);
    CHECK(dependencies.images[0].uri == "textures/albedo.png");
    CHECK(dependencies.images[1].sourceKind == GltfImageSourceKind::DataUri);
    CHECK(dependencies.images[1].mimeType == "image/png");

    CHECK(dependencies.samplers.size() == 1);
    CHECK(dependencies.samplers[0].magFilter == GltfSamplerFilter::Linear);
    CHECK(dependencies.samplers[0].minFilter ==
        GltfSamplerFilter::LinearMipmapLinear);
    CHECK(dependencies.samplers[0].wrapS == GltfSamplerWrap::ClampToEdge);
    CHECK(dependencies.samplers[0].wrapT ==
        GltfSamplerWrap::MirroredRepeat);

    CHECK(dependencies.materials.size() == 1);
    const GltfMaterialDependency& material = dependencies.materials[0];
    CHECK(material.name == "Painted metal");
    CHECK(material.textures.size() == 5);
    CHECK(material.textures[0].semantic ==
        MaterialTextureSemantic::BaseColor);
    CHECK(material.textures[0].textureIndex == 0U);
    CHECK(material.textures[0].textureName == "Albedo texture");
    CHECK(material.textures[0].imageIndex == 0U);
    CHECK(material.textures[0].samplerIndex == 0U);
    CHECK(material.textures[0].texcoord == 1U);
    CHECK(material.textures[0].transform.has_value());
    CHECK(material.textures[0].transform->offset.x == 0.25f);
    CHECK(material.textures[0].transform->offset.y == 0.5f);
    CHECK(material.textures[0].transform->scale.x == 2.0f);
    CHECK(material.textures[0].transform->scale.y == 3.0f);
    CHECK(material.textures[0].transform->rotation == 0.75f);
    CHECK(material.textures[0].transform->texcoord == 0U);
    CHECK(material.textures[1].semantic ==
        MaterialTextureSemantic::MetallicRoughness);
    CHECK(material.textures[1].imageIndex == 1U);
    CHECK(!material.textures[1].samplerIndex.has_value());
    CHECK(material.textures[2].semantic == MaterialTextureSemantic::Normal);
    CHECK(material.textures[2].scale == 0.25f);
    CHECK(material.textures[3].semantic ==
        MaterialTextureSemantic::Occlusion);
    CHECK(material.textures[3].scale == 0.5f);
    CHECK(material.textures[4].semantic ==
        MaterialTextureSemantic::Emissive);
    CHECK(material.textures[4].texcoord == 1U);
}

void testInspectsEmbeddedGlbImage()
{
    TEST("inspectsEmbeddedGlbImage");
    TempDirectory temp;
    const std::filesystem::path model = temp.path() / "embedded.glb";
    constexpr uint8_t binary[] { 1, 2, 3, 4, 5, 6, 7, 8 };
    writeGlb(model, R"json({
  "asset":{"version":"2.0"},
  "buffers":[{"name":"GLB data","byteLength":8}],
  "bufferViews":[{
    "name":"Embedded image bytes","buffer":0,"byteOffset":4,"byteLength":4
  }],
  "images":[{
    "name":"Embedded image","bufferView":0,"mimeType":"image/png"
  }],
  "textures":[{"source":0}],
  "materials":[{
    "name":"Embedded material",
    "pbrMetallicRoughness":{"baseColorTexture":{"index":0}}
  }]
})json", binary);

    const GltfAssetDependencies dependencies =
        inspectGltfAssetDependencies(model);

    CHECK(dependencies.buffers.size() == 1);
    CHECK(dependencies.buffers[0].sourceKind ==
        GltfBufferSourceKind::EmbeddedGlb);
    CHECK(dependencies.buffers[0].uri.empty());
    CHECK(dependencies.buffers[0].byteLength == 8);
    CHECK(dependencies.images.size() == 1);
    CHECK(dependencies.images[0].sourceKind ==
        GltfImageSourceKind::BufferView);
    CHECK(dependencies.images[0].uri.empty());
    CHECK(dependencies.images[0].mimeType == "image/png");
    CHECK(dependencies.images[0].bufferViewIndex == 0U);
    CHECK(dependencies.images[0].bufferIndex == 0U);
    CHECK(dependencies.images[0].byteOffset == 4);
    CHECK(dependencies.images[0].byteLength == 4);
    CHECK(dependencies.samplers.empty());
    CHECK(dependencies.materials.size() == 1);
    CHECK(dependencies.materials[0].textures.size() == 1);
    CHECK(dependencies.materials[0].textures[0].imageIndex == 0U);
    CHECK(!dependencies.materials[0].textures[0].samplerIndex.has_value());
}

void testLoadsMaterialMapBindingsAndAuthoredParameters()
{
    TEST("loadsMaterialMapBindingsAndAuthoredParameters");
    TempDirectory temp;
    const std::filesystem::path model = temp.path() / "material-maps.glb";

    std::vector<uint8_t> binary;
    for (float value : {
             0.0f, 0.0f, 0.0f,
             1.0f, 0.0f, 0.0f,
             0.0f, 1.0f, 0.0f,
             0.0f, 0.0f, 1.0f,
             0.0f, 0.0f, 1.0f,
             0.0f, 0.0f, 1.0f,
             0.0f, 0.0f,
             1.0f, 0.0f,
             0.0f, 1.0f,
             0.25f, 0.25f,
             0.75f, 0.25f,
             0.25f, 0.75f,
         }) {
        appendFloat(binary, value);
    }
    appendUint16(binary, 0);
    appendUint16(binary, 1);
    appendUint16(binary, 2);

    writeGlb(model, R"json({
  "asset":{"version":"2.0"},
  "buffers":[{"byteLength":126}],
  "bufferViews":[
    {"buffer":0,"byteOffset":0,"byteLength":36},
    {"buffer":0,"byteOffset":36,"byteLength":36},
    {"buffer":0,"byteOffset":72,"byteLength":24},
    {"buffer":0,"byteOffset":96,"byteLength":24},
    {"buffer":0,"byteOffset":120,"byteLength":6}
  ],
  "accessors":[
    {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
    {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
    {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
    {"bufferView":3,"componentType":5126,"count":3,"type":"VEC2"},
    {"bufferView":4,"componentType":5123,"count":3,"type":"SCALAR"}
  ],
  "images":[
    {"uri":"data:image/png;base64,AAAA"},
    {"uri":"data:image/png;base64,AAAA"},
    {"uri":"data:image/png;base64,AAAA"},
    {"uri":"data:image/png;base64,AAAA"},
    {"uri":"data:image/png;base64,AAAA"}
  ],
  "textures":[
    {"source":0},{"source":1},{"source":2},{"source":3},{"source":4}
  ],
  "materials":[{
    "pbrMetallicRoughness":{
      "baseColorFactor":[0.1,0.2,0.3,0.4],
      "metallicFactor":0.6,
      "roughnessFactor":0.7,
      "baseColorTexture":{"index":0,"texCoord":1},
      "metallicRoughnessTexture":{"index":1,"texCoord":1}
    },
    "normalTexture":{"index":2,"texCoord":1,"scale":0.25},
    "occlusionTexture":{"index":3,"texCoord":0,"strength":0.5},
    "emissiveTexture":{"index":4,"texCoord":1},
    "emissiveFactor":[0.8,0.7,0.6],
    "alphaMode":"MASK",
    "alphaCutoff":0.35,
    "doubleSided":true
  }],
  "meshes":[{"primitives":[{
    "attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"TEXCOORD_1":3},
    "indices":4,
    "material":0
  }]}]
})json", binary);

    const MeshData unbound = loadGltfMesh(model);
    CHECK(unbound.materials.size() == 1);
    const MeshMaterial& authored = unbound.materials[0];
    CHECK(authored.baseColorTexture == 0U);
    CHECK(authored.normalTexture == 0U);
    CHECK(authored.metallicRoughnessTexture == 0U);
    CHECK(authored.emissiveTexture == 0U);
    CHECK(authored.occlusionTexture == 0U);
    CHECK(authored.baseColorUvSet == 1U);
    CHECK(authored.normalUvSet == 1U);
    CHECK(authored.metallicRoughnessUvSet == 1U);
    CHECK(authored.emissiveUvSet == 1U);
    CHECK(authored.occlusionUvSet == 0U);
    CHECK(authored.normalScale == 0.25f);
    CHECK(authored.occlusionStrength == 0.5f);

    PrimitiveMaterialBinding binding;
    binding.textureIndex = 4;
    binding.normalTextureIndex = 5;
    binding.metallicRoughnessTextureIndex = 6;
    binding.emissiveTextureIndex = 7;
    binding.occlusionTextureIndex = 8;
    binding.flags = PrimitiveMaterialScrollV;
    GltfMeshLoadOptions options;
    options.primitiveMaterials.push_back(binding);
    const MeshData bound = loadGltfMesh(model, options);
    const MeshMaterial& material = bound.materials[0];
    CHECK(material.baseColorTexture == 5U);
    CHECK(material.normalTexture == 6U);
    CHECK(material.metallicRoughnessTexture == 7U);
    CHECK(material.emissiveTexture == 8U);
    CHECK(material.occlusionTexture == 9U);
    CHECK(material.flags == PrimitiveMaterialScrollV);
    CHECK(material.baseColorFactor.x == 0.1f);
    CHECK(material.baseColorFactor.y == 0.2f);
    CHECK(material.baseColorFactor.z == 0.3f);
    CHECK(material.baseColorFactor.w == 0.4f);
    CHECK(material.metallicFactor == 0.6f);
    CHECK(material.roughnessFactor == 0.7f);
    CHECK(material.emissiveFactor.x == 0.8f);
    CHECK(material.emissiveFactor.y == 0.7f);
    CHECK(material.emissiveFactor.z == 0.6f);
    CHECK(material.alphaMode == MaterialAlphaMode::Mask);
    CHECK(material.alphaCutoff == 0.35f);
    CHECK(material.doubleSided);

    binding.bindBaseColorTexture = false;
    options.primitiveMaterials[0] = binding;
    const MeshData mapOnly = loadGltfMesh(model, options);
    const MeshMaterial& mapOnlyMaterial = mapOnly.materials[0];
    CHECK(mapOnlyMaterial.baseColorTexture == 0U);
    CHECK(mapOnlyMaterial.normalTexture == 6U);
    CHECK(mapOnlyMaterial.metallicRoughnessTexture == 7U);
    CHECK(mapOnlyMaterial.emissiveTexture == 8U);
    CHECK(mapOnlyMaterial.occlusionTexture == 9U);
}

} // namespace

int main()
{
    testInspectsExternalAndDataUriDependenciesWithoutLoadingThem();
    testInspectsEmbeddedGlbImage();
    testLoadsMaterialMapBindingsAndAuthoredParameters();

    if (failures == 0) {
        std::cout << "GltfDependencyTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "GltfDependencyTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
