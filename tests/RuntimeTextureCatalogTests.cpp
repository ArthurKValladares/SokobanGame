#include "engine/AssetManifest.hpp"
#include "engine/render/RuntimeTextureCatalog.hpp"
#include "engine/render/TextureSourceLoader.hpp"
#include "engine/render/PngWriter.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
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

template <typename Function>
void checkThrows(Function&& function)
{
    try {
        function();
        CHECK(false);
    } catch (const std::exception&) {
        CHECK(true);
    }
}

constexpr std::string_view manifestJson = R"json({
  "format": 1,
  "textures": [
    {"name":"HeroBase","path":"textures/hero.png"},
    {"name":"BeltA","path":"textures/belt-a.png"},
    {"name":"BeltB","path":"textures/belt-b.png"}
  ],
  "models": [
    {"name":"Hero","path":"models/shared.glb","geometry":"skinned","role":"player","material":{"mode":"texture","texture":"HeroBase"}},
    {"name":"Belt","path":"models/shared.glb","material":{"mode":"primitive-materials","slots":[{"texture":"BeltA"},{"texture":"BeltB","scrollV":true}]}},
    {"name":"Crate","path":"models/crate.glb"}
  ],
  "animations": [
    {"name":"Idle","path":"anims/a.glb","role":"player-idle"},
    {"name":"Move","path":"anims/a.glb","role":"player-move"},
    {"name":"Push","path":"anims/a.glb","role":"player-push"},
    {"name":"Death","path":"anims/a.glb","role":"player-death"},
    {"name":"DeadIdle","path":"anims/a.glb","role":"player-dead-idle"}
  ]
})json";

TextureSourceIdentity identity(
    std::string path,
    TextureColorSpace colorSpace = TextureColorSpace::Linear)
{
    return {
        .source = ExternalTextureSource { std::move(path) },
        .interpretation = { .colorSpace = colorSpace },
    };
}

ResolvedMaterialTexture materialTexture(
    std::string document,
    uint32_t material,
    MaterialTextureSemantic semantic,
    TextureSourceIdentity source)
{
    return {
        .document = std::move(document),
        .assetLabel = "test model",
        .materialIndex = material,
        .materialName = "test material",
        .textureName = "test texture",
        .semantic = semantic,
        .identity = std::move(source),
    };
}

bool contains(const std::vector<uint32_t>& values, uint32_t value)
{
    return std::ranges::find(values, value) != values.end();
}

void testBuildsDeduplicatedPerModelCatalog()
{
    TEST("buildsDeduplicatedPerModelCatalog");
    const AssetManifest manifest = AssetManifest::parse(manifestJson);
    const TextureSourceIdentity packed = identity("maps/packed.png");
    const TextureSourceIdentity emissive =
        identity("maps/packed.png", TextureColorSpace::Srgb);
    const TextureSourceIdentity crateNormal = identity("maps/crate.png");
    const TextureSourceIdentity ignoredBase =
        identity("maps/gltf-base.png", TextureColorSpace::Srgb);
    const std::vector<ResolvedMaterialTexture> materialTextures {
        materialTexture("models/shared.glb", 0,
            MaterialTextureSemantic::Normal, packed),
        materialTexture("models/shared.glb", 0,
            MaterialTextureSemantic::Occlusion, packed),
        materialTexture("models/shared.glb", 1,
            MaterialTextureSemantic::Emissive, emissive),
        materialTexture("models/shared.glb", 0,
            MaterialTextureSemantic::BaseColor, ignoredBase),
        materialTexture("models/crate.glb", 0,
            MaterialTextureSemantic::Normal, crateNormal),
    };

    const RuntimeTextureCatalog catalog =
        buildRuntimeTextureCatalog(manifest, materialTextures);
    CHECK(catalog.manifestTextureCount() == 3U);
    CHECK(catalog.discoveredTextureCount() == 3U);
    CHECK(catalog.textures().size() == 6U);

    const RuntimeModelTextures& hero = catalog.model(0);
    const RuntimeModelTextures& belt = catalog.model(1);
    const RuntimeModelTextures& crate = catalog.model(2);
    CHECK(contains(hero.requiredTextures, 0U));
    CHECK(contains(hero.requiredTextures, 3U));
    CHECK(contains(hero.requiredTextures, 4U));
    CHECK(!contains(hero.requiredTextures, 5U));
    CHECK(contains(belt.requiredTextures, 1U));
    CHECK(contains(belt.requiredTextures, 2U));
    CHECK(contains(belt.requiredTextures, 3U));
    CHECK(contains(belt.requiredTextures, 4U));
    CHECK(!contains(belt.requiredTextures, 5U));
    CHECK(crate.requiredTextures.size() == 1U);
    CHECK(crate.requiredTextures[0] == 5U);

    CHECK(hero.primitiveMaterials[0].normalTextureIndex == 3U);
    CHECK(hero.primitiveMaterials[0].occlusionTextureIndex == 3U);
    CHECK(hero.primitiveMaterials[1].emissiveTextureIndex == 4U);
    CHECK(!hero.primitiveMaterials[0].bindBaseColorTexture);
    CHECK(belt.primitiveMaterials[0].bindBaseColorTexture);
    CHECK(belt.primitiveMaterials[0].textureIndex == 1U);
    CHECK(belt.primitiveMaterials[1].textureIndex == 2U);
    CHECK(belt.primitiveMaterials[1].flags == PrimitiveMaterialScrollV);

    CHECK(catalog.descriptorIndex(0, 12) == 0U);
    CHECK(catalog.descriptorIndex(2, 12) == 2U);
    CHECK(catalog.descriptorIndex(3, 12) == 9U);
    CHECK(catalog.descriptorIndex(5, 12) == 11U);
    checkThrows([&] { (void)catalog.descriptorIndex(6, 12); });
    checkThrows([&] { (void)catalog.descriptorIndex(3, 5); });
}

class TempDirectory {
public:
    TempDirectory()
    {
        const auto id =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("sokoban-runtime-textures-" + std::to_string(id));
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

std::vector<uint8_t> readBytes(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Could not open test image");
    }
    const std::streamsize size = stream.tellg();
    std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

std::string base64Encode(std::span<const uint8_t> bytes)
{
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 3) {
        const uint32_t a = bytes[offset];
        const uint32_t b = offset + 1 < bytes.size() ? bytes[offset + 1] : 0;
        const uint32_t c = offset + 2 < bytes.size() ? bytes[offset + 2] : 0;
        const uint32_t packed = (a << 16U) | (b << 8U) | c;
        result.push_back(alphabet[(packed >> 18U) & 63U]);
        result.push_back(alphabet[(packed >> 12U) & 63U]);
        result.push_back(offset + 1 < bytes.size()
            ? alphabet[(packed >> 6U) & 63U]
            : '=');
        result.push_back(offset + 2 < bytes.size()
            ? alphabet[packed & 63U]
            : '=');
    }
    return result;
}

void appendUint32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
    bytes.push_back(static_cast<uint8_t>(value >> 16U));
    bytes.push_back(static_cast<uint8_t>(value >> 24U));
}

void writeGlb(
    const std::filesystem::path& path,
    std::string json,
    std::span<const uint8_t> binary)
{
    while (json.size() % 4 != 0) {
        json.push_back(' ');
    }
    std::vector<uint8_t> padded(binary.begin(), binary.end());
    while (padded.size() % 4 != 0) {
        padded.push_back(0);
    }
    const uint32_t totalSize = 12U + 8U +
        static_cast<uint32_t>(json.size()) + 8U +
        static_cast<uint32_t>(padded.size());
    std::vector<uint8_t> bytes;
    appendUint32(bytes, 0x46546C67);
    appendUint32(bytes, 2);
    appendUint32(bytes, totalSize);
    appendUint32(bytes, static_cast<uint32_t>(json.size()));
    appendUint32(bytes, 0x4E4F534A);
    bytes.insert(bytes.end(), json.begin(), json.end());
    appendUint32(bytes, static_cast<uint32_t>(padded.size()));
    appendUint32(bytes, 0x004E4942);
    bytes.insert(bytes.end(), padded.begin(), padded.end());
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

void testLoadsEverySupportedSourceForm()
{
    TEST("loadsEverySupportedSourceForm");
    const std::filesystem::path assets = SOKOBAN_TEST_ASSET_DIR;
    const std::filesystem::path relative =
        "KayKit Adventurers 2.0/Characters/gltf/rogue_texture.png";
    const std::vector<uint8_t> png = readBytes(assets / relative);

    const ImageData external = loadRgbaTextureSource(
        assets, ExternalTextureSource { relative });
    const ImageData inlineImage = loadRgbaTextureSource(
        assets,
        DataUriTextureSource {
            "data:image/png;base64," + base64Encode(png),
        });
    CHECK(inlineImage.width == external.width);
    CHECK(inlineImage.height == external.height);
    CHECK(inlineImage.rgba == external.rgba);

    TempDirectory temp;
    const std::filesystem::path glb = temp.path() / "embedded.glb";
    writeGlb(glb,
        "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":" +
            std::to_string(png.size()) +
            "}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":" +
            std::to_string(png.size()) +
            "}],\"images\":[{\"bufferView\":0,\"mimeType\":\"image/png\"}]}",
        png);
    const ImageData embedded = loadRgbaTextureSource(
        temp.path(),
        GltfBufferViewTextureSource {
            .document = "embedded.glb",
            .bufferViewIndex = 0,
            .mimeType = "image/png",
        });
    CHECK(embedded.width == external.width);
    CHECK(embedded.height == external.height);
    CHECK(embedded.rgba == external.rgba);

    checkThrows([&] {
        (void)loadRgbaTextureSource(
            assets,
            DataUriTextureSource { "data:image/png;base64,***=" });
    });
}

void writeBytes(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

void testPreparedTextureSelectsArtifactOrSourceFallback()
{
    TEST("preparedTextureSelectsArtifactOrSourceFallback");
    TempDirectory temp;
    const std::filesystem::path relative = "textures/test.png";
    const std::vector<std::byte> png = encodeRgbaPng(
        2, 2,
        {
            255, 0, 0, 255,
            0, 255, 0, 255,
            0, 0, 255, 255,
            255, 255, 255, 255,
        });
    writeBytes(temp.path() / relative, png);
    const TextureSourceIdentity source =
        identity(relative.generic_string(), TextureColorSpace::Srgb);
    const ImageData rgba = loadRgbaTextureSource(temp.path(), source.source);
    const std::filesystem::path artifactPath =
        temp.path() / compressedTextureArtifactPath(source);
    const std::vector<std::byte> ktx =
        buildBc7Ktx2(rgba, source.interpretation);
    writeBytes(artifactPath, ktx);

    const PreparedTextureSource compressed =
        loadPreparedTextureSource(temp.path(), source, true);
    CHECK(std::holds_alternative<CompressedTextureArtifact>(compressed));
    CHECK(std::get<CompressedTextureArtifact>(compressed).residentBytes() == 32U);

    const PreparedTextureSource unsupported =
        loadPreparedTextureSource(temp.path(), source, false);
    CHECK(std::holds_alternative<ImageData>(unsupported));
    CHECK(std::get<ImageData>(unsupported).rgba == rgba.rgba);

    std::filesystem::remove(artifactPath);
    const PreparedTextureSource missing =
        loadPreparedTextureSource(temp.path(), source, true);
    CHECK(std::holds_alternative<ImageData>(missing));

    writeBytes(artifactPath, std::span<const std::byte>(ktx).first(20));
    checkThrows([&] {
        (void)loadPreparedTextureSource(temp.path(), source, true);
    });
}

void testCollectsProductionCatalog()
{
    TEST("collectsProductionCatalog");
    const std::filesystem::path assets = SOKOBAN_TEST_ASSET_DIR;
    const AssetManifest manifest =
        AssetManifest::loadFromFile(assets / "manifest.json");
    const RuntimeTextureCatalog catalog =
        collectRuntimeTextureCatalog(assets, manifest);

    CHECK(catalog.manifestTextureCount() == manifest.textures().size());
    CHECK(catalog.textures().size() >= manifest.textures().size());
    CHECK(catalog.descriptorIndex(
        static_cast<uint32_t>(catalog.textures().size() - 1), 1024) < 1024U);
    for (uint32_t index = 0; index < manifest.models().size(); ++index) {
        (void)catalog.model(index);
        CHECK(true);
    }
}

} // namespace

int main()
{
    testBuildsDeduplicatedPerModelCatalog();
    testLoadsEverySupportedSourceForm();
    testPreparedTextureSelectsArtifactOrSourceFallback();
    testCollectsProductionCatalog();

    if (failures == 0) {
        std::cout << "RuntimeTextureCatalogTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "RuntimeTextureCatalogTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
