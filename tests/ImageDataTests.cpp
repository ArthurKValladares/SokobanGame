#include "engine/render/ImageData.hpp"

#include <array>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>

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

} // namespace

int main()
{
    testLoadsRgbaPixels();
    testConcurrentLoads();
    testMissingFileDiagnostic();
    testNonImageDiagnostic();

    if (failures == 0) {
        std::cout << "ImageDataTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "ImageDataTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
