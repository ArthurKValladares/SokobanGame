#include "TestHarness.hpp"

#include "engine/render/ImageData.hpp"

#include <array>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

const std::filesystem::path texturePath =
    std::filesystem::path(SOKOBAN_TEST_ASSET_DIR)
    / "KayKit Adventurers 2.0/Characters/gltf/rogue_texture.png";

void testLoadsRgbaPixels()
{
    const sokoban::ImageData image = sokoban::loadRgbaImage(texturePath);
    CHECK(image.width > 0);
    CHECK(image.height > 0);
    CHECK(image.rgba.size()
        == static_cast<size_t>(image.width) * image.height * 4);
    CHECK(sokoban::inspectRgbaImagePayloadBytes(texturePath) ==
        image.rgba.size());
}

void testConcurrentLoads()
{
    constexpr size_t loadCount = 8;
    std::array<std::future<sokoban::ImageData>, loadCount> loads;
    for (auto& load : loads) {
        load = std::async(std::launch::async, [] {
            return sokoban::loadRgbaImage(texturePath);
        });
    }

    const sokoban::ImageData reference = loads.front().get();
    for (size_t index = 1; index < loads.size(); ++index) {
        const sokoban::ImageData image = loads[index].get();
        CHECK(image.width == reference.width);
        CHECK(image.height == reference.height);
        CHECK(image.rgba == reference.rgba);
    }
}

void testMissingFileDiagnostic()
{
    const std::filesystem::path missing =
        std::filesystem::path(SOKOBAN_TEST_ASSET_DIR) / "missing-texture.png";
    try {
        static_cast<void>(sokoban::loadRgbaImage(missing));
        CHECK(false);
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        CHECK(message.find("Failed to open image") != std::string::npos);
        CHECK(message.find("missing-texture.png") != std::string::npos);
    }
}

void testNonImageDiagnostic()
{
    try {
        static_cast<void>(sokoban::loadRgbaImage(
            std::filesystem::path(SOKOBAN_TEST_ASSET_DIR) / "manifest.json"));
        CHECK(false);
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        CHECK(message.find("Failed to decode image") != std::string::npos);
        CHECK(message.find("manifest.json") != std::string::npos);
    }
}

void testNonImageInspectionDiagnostic()
{
    try {
        static_cast<void>(sokoban::inspectRgbaImagePayloadBytes(
            std::filesystem::path(SOKOBAN_TEST_ASSET_DIR) / "manifest.json"));
        CHECK(false);
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        CHECK(message.find("Failed to inspect image") != std::string::npos);
        CHECK(message.find("manifest.json") != std::string::npos);
    }
}

void testLegacySplatConversionPreservesRgbaWeights()
{
    sokoban::ImageData grayscale {
        .width = 3, .height = 1,
        .rgba = { std::byte { 0 }, std::byte { 0 }, std::byte { 0 }, std::byte { 255 },
            std::byte { 128 }, std::byte { 128 }, std::byte { 128 }, std::byte { 255 },
            std::byte { 255 }, std::byte { 255 }, std::byte { 255 }, std::byte { 255 } },
        .sourceChannels = 1,
    };
    sokoban::convertLegacySplatWeights(grayscale);
    CHECK(grayscale.sourceChannels == 4);
    CHECK(grayscale.rgba[0] == std::byte { 255 });
    CHECK(grayscale.rgba[1] == std::byte { 0 });
    CHECK(grayscale.rgba[4] == std::byte { 127 });
    CHECK(grayscale.rgba[5] == std::byte { 128 });
    CHECK(grayscale.rgba[6] == std::byte { 0 });
    CHECK(grayscale.rgba[7] == std::byte { 0 });
    CHECK(grayscale.rgba[8] == std::byte { 0 });
    CHECK(grayscale.rgba[9] == std::byte { 255 });
    const auto converted = grayscale.rgba;
    sokoban::convertLegacySplatWeights(grayscale);
    CHECK(grayscale.rgba == converted);

    sokoban::ImageData rgb {
        .width = 1, .height = 1,
        .rgba = { std::byte { 100 }, std::byte { 60 }, std::byte { 20 }, std::byte { 255 } },
        .sourceChannels = 3,
    };
    sokoban::convertLegacySplatWeights(rgb);
    CHECK(rgb.sourceChannels == 4);
    CHECK(rgb.rgba[0] == std::byte { 100 });
    CHECK(rgb.rgba[1] == std::byte { 60 });
    CHECK(rgb.rgba[2] == std::byte { 20 });
    CHECK(rgb.rgba[3] == std::byte { 0 });

    sokoban::ImageData rgba {
        .width = 1, .height = 1,
        .rgba = { std::byte { 0 }, std::byte { 0 }, std::byte { 0 }, std::byte { 255 } },
    };
    sokoban::convertLegacySplatWeights(rgba);
    CHECK(rgba.rgba[0] == std::byte { 0 });
    CHECK(rgba.rgba[3] == std::byte { 255 });
}

} // namespace

int main()
{
    testLoadsRgbaPixels();
    testConcurrentLoads();
    testMissingFileDiagnostic();
    testNonImageDiagnostic();
    testNonImageInspectionDiagnostic();
    testLegacySplatConversionPreservesRgbaWeights();

    if (failures == 0) {
        std::cout << "ImageDataTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "ImageDataTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
