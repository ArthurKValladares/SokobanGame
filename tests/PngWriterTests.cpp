// Round-trip tests for the greyscale PNG encoder.
//
// The encoder is hand-written (fixed-Huffman deflate, adaptive scanline
// filters), so correctness is established the only way that matters: every
// image is decoded back with stb_image - the exact decoder the game uses to
// load textures - and compared pixel for pixel.

#include "TestHarness.hpp"

#include "engine/render/ImageData.hpp"
#include "engine/render/PngWriter.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

namespace {

using namespace sokoban;

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
              ("sokoban_png_test_" +
                  std::to_string(
                      std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count())))
    {
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

// Encodes, writes, reloads through stb_image, and returns the red channel.
[[nodiscard]] std::vector<uint8_t> roundTrip(
    const std::filesystem::path& directory,
    const std::string& name,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& pixels)
{
    const std::filesystem::path file = directory / name;
    writeGrayscalePng(file, width, height, pixels);
    const ImageData decoded = loadRgbaImage(file);
    if (decoded.width != width || decoded.height != height) {
        return {};
    }
    std::vector<uint8_t> red(static_cast<std::size_t>(width) * height);
    for (std::size_t i = 0; i < red.size(); ++i) {
        red[i] = static_cast<uint8_t>(decoded.rgba[i * 4]);
    }
    return red;
}

void testSignatureAndStructure()
{
    TEST("signatureAndStructure");
    const std::vector<uint8_t> pixels(4 * 4, 128);
    const std::vector<std::byte> png = encodeGrayscalePng(4, 4, pixels);

    CHECK(png.size() > 8);
    const std::vector<std::byte> signature {
        std::byte { 0x89 }, std::byte { 'P' }, std::byte { 'N' },
        std::byte { 'G' }, std::byte { '\r' }, std::byte { '\n' },
        std::byte { 0x1A }, std::byte { '\n' },
    };
    CHECK(std::equal(signature.begin(), signature.end(), png.begin()));

    const std::string text(
        reinterpret_cast<const char*>(png.data()), png.size());
    CHECK(text.find("IHDR") != std::string::npos);
    CHECK(text.find("IDAT") != std::string::npos);
    CHECK(text.find("IEND") != std::string::npos);
    // IEND must be last.
    CHECK(text.rfind("IEND") == text.size() - 8);
}

void testFlatImagesRoundTrip()
{
    TEST("flatImagesRoundTrip");
    const TemporaryDirectory directory;
    for (const uint8_t value : { uint8_t { 0 }, uint8_t { 1 },
             uint8_t { 127 }, uint8_t { 254 }, uint8_t { 255 } }) {
        const std::vector<uint8_t> pixels(16 * 16, value);
        const std::vector<uint8_t> decoded = roundTrip(
            directory.path(),
            "flat" + std::to_string(value) + ".png",
            16, 16, pixels);
        CHECK(decoded == pixels);
    }
}

void testGradientRoundTrips()
{
    TEST("gradientRoundTrips");
    const TemporaryDirectory directory;
    constexpr uint32_t width = 61;  // deliberately not a power of two
    constexpr uint32_t height = 37;
    std::vector<uint8_t> pixels(
        static_cast<std::size_t>(width) * height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            pixels[static_cast<std::size_t>(y) * width + x] =
                static_cast<uint8_t>((x * 4 + y * 3) % 256);
        }
    }
    CHECK(roundTrip(directory.path(), "gradient.png", width, height, pixels) ==
        pixels);
}

void testRandomNoiseRoundTrips()
{
    TEST("randomNoiseRoundTrips");
    const TemporaryDirectory directory;
    // Incompressible data exercises the literal path and the worst case for
    // the bit writer; a splat map is somewhere between this and a gradient.
    std::mt19937 generator(1234);
    std::uniform_int_distribution<int> distribution(0, 255);
    constexpr uint32_t width = 128;
    constexpr uint32_t height = 96;
    std::vector<uint8_t> pixels(
        static_cast<std::size_t>(width) * height);
    for (uint8_t& value : pixels) {
        value = static_cast<uint8_t>(distribution(generator));
    }
    CHECK(roundTrip(directory.path(), "noise.png", width, height, pixels) ==
        pixels);
}

void testLongRunsRoundTripAndCompress()
{
    TEST("longRunsRoundTripAndCompress");
    const TemporaryDirectory directory;
    // Long identical runs push LZ77 to its maximum match length (258) and
    // maximum distance handling.
    constexpr uint32_t width = 256;
    constexpr uint32_t height = 256;
    const std::vector<uint8_t> pixels(
        static_cast<std::size_t>(width) * height, 200);
    CHECK(roundTrip(directory.path(), "runs.png", width, height, pixels) ==
        pixels);

    // A flat image must actually compress, otherwise the LZ77 stage is not
    // matching and painted maps would bloat the repository.
    const std::vector<std::byte> png =
        encodeGrayscalePng(width, height, pixels);
    CHECK(png.size() < pixels.size() / 20);
}

void testSplatLikeContentCompressesWell()
{
    TEST("splatLikeContentCompressesWell");
    // Smooth noise, like a real splat map: this is the case the encoder
    // actually ships for, so pin down that it stays far smaller than raw.
    constexpr uint32_t width = 288;
    constexpr uint32_t height = 224;
    std::vector<uint8_t> pixels(
        static_cast<std::size_t>(width) * height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const double value =
                128.0 + 100.0 * std::sin(x * 0.05) * std::cos(y * 0.04);
            pixels[static_cast<std::size_t>(y) * width + x] =
                static_cast<uint8_t>(std::clamp(value, 0.0, 255.0));
        }
    }
    const TemporaryDirectory directory;
    CHECK(roundTrip(directory.path(), "splat.png", width, height, pixels) ==
        pixels);
    CHECK(encodeGrayscalePng(width, height, pixels).size() < pixels.size() / 2);
}

void testEdgeSizesRoundTrip()
{
    TEST("edgeSizesRoundTrip");
    const TemporaryDirectory directory;
    // 1xN and Nx1 exercise the filter code's row/column neighbour handling.
    const std::vector<uint8_t> single { 42 };
    CHECK(roundTrip(directory.path(), "1x1.png", 1, 1, single) == single);

    std::vector<uint8_t> column(64);
    for (std::size_t i = 0; i < column.size(); ++i) {
        column[i] = static_cast<uint8_t>(i * 3);
    }
    CHECK(roundTrip(directory.path(), "1x64.png", 1, 64, column) == column);
    CHECK(roundTrip(directory.path(), "64x1.png", 64, 1, column) == column);
}

void testInvalidInputIsRejected()
{
    TEST("invalidInputIsRejected");
    const auto throws = [](auto&& call) {
        try {
            (void)call();
        } catch (const std::exception&) {
            return true;
        }
        return false;
    };
    CHECK(throws([] { return encodeGrayscalePng(0, 4, {}); }));
    CHECK(throws([] { return encodeGrayscalePng(4, 0, {}); }));
    // Pixel count that disagrees with the dimensions would otherwise read out
    // of bounds while filtering.
    CHECK(throws([] { return encodeGrayscalePng(4, 4, std::vector<uint8_t>(15)); }));
    CHECK(throws([] { return encodeGrayscalePng(4, 4, std::vector<uint8_t>(17)); }));
}

void testRgbaRoundTrips()
{
    TEST("rgbaRoundTrips");
    const TemporaryDirectory directory;
    constexpr uint32_t width = 53; // deliberately not a power of two
    constexpr uint32_t height = 31;

    // Colour, an alpha ramp, and per-channel variation: baked thumbnails are
    // screenshots, so all four channels carry real data and the filter's
    // "left neighbour" must step a whole pixel rather than one byte.
    std::vector<uint8_t> pixels(
        static_cast<std::size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const std::size_t at =
                (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[at + 0] = static_cast<uint8_t>(x * 5);
            pixels[at + 1] = static_cast<uint8_t>(y * 7);
            pixels[at + 2] = static_cast<uint8_t>((x + y) * 3);
            pixels[at + 3] = static_cast<uint8_t>(x < width / 2 ? 255 : x * 4);
        }
    }

    const std::filesystem::path file = directory.path() / "rgba.png";
    writeRgbaPng(file, width, height, pixels);
    const ImageData decoded = loadRgbaImage(file);
    CHECK(decoded.width == width);
    CHECK(decoded.height == height);
    CHECK(decoded.rgba.size() == pixels.size());

    bool identical = decoded.rgba.size() == pixels.size();
    for (std::size_t i = 0; identical && i < pixels.size(); ++i) {
        identical = static_cast<uint8_t>(decoded.rgba[i]) == pixels[i];
    }
    CHECK(identical);

    // Transparency survives, which is what lets a thumbnail sit on the button
    // background instead of in a filled rectangle.
    const std::vector<uint8_t> transparent(16 * 16 * 4, 0);
    const std::filesystem::path clearFile = directory.path() / "clear.png";
    writeRgbaPng(clearFile, 16, 16, transparent);
    const ImageData clear = loadRgbaImage(clearFile);
    CHECK(clear.rgba[3] == static_cast<std::byte>(0));

    // Wrong buffer size is rejected rather than read out of bounds.
    const auto throws = [](auto&& call) {
        try {
            (void)call();
        } catch (const std::exception&) {
            return true;
        }
        return false;
    };
    CHECK(throws([] { return encodeRgbaPng(4, 4, std::vector<uint8_t>(4 * 4)); }));
    CHECK(throws([] { return encodeRgbaPng(4, 4, std::vector<uint8_t>(4 * 4 * 4 - 1)); }));
}

void testWriteIsAtomicallyReplaced()
{
    TEST("writeIsAtomicallyReplaced");
    const TemporaryDirectory directory;
    const std::filesystem::path file = directory.path() / "map.png";

    const std::vector<uint8_t> first(8 * 8, 10);
    writeGrayscalePng(file, 8, 8, first);
    CHECK(std::filesystem::exists(file));

    // Overwriting an existing map must succeed and fully replace it, not
    // append or leave the old content.
    const std::vector<uint8_t> second(16 * 16, 240);
    writeGrayscalePng(file, 16, 16, second);
    const ImageData reloaded = loadRgbaImage(file);
    CHECK(reloaded.width == 16);
    CHECK(reloaded.height == 16);
    CHECK(reloaded.rgba[0] == static_cast<std::byte>(240));

    // No temporary left behind.
    CHECK(!std::filesystem::exists(
        std::filesystem::path(file).concat(".tmp")));
}

} // namespace

int main()
{
    testSignatureAndStructure();
    testFlatImagesRoundTrip();
    testGradientRoundTrips();
    testRandomNoiseRoundTrips();
    testLongRunsRoundTripAndCompress();
    testSplatLikeContentCompressesWell();
    testEdgeSizesRoundTrip();
    testInvalidInputIsRejected();
    testRgbaRoundTrips();
    testWriteIsAtomicallyReplaced();

    if (failures == 0) {
        std::cout << "PngWriterTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "PngWriterTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
