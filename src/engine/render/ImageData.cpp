#include "engine/render/ImageData.hpp"

#define STBI_FAILURE_USERMSG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sokoban {
namespace {

std::vector<stbi_uc> readImageFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Failed to open image: " + path.string());
    }

    const std::streamoff fileSize = stream.tellg();
    if (fileSize <= 0) {
        throw std::runtime_error("Image file is empty: " + path.string());
    }
    if (fileSize > std::numeric_limits<int>::max()) {
        throw std::runtime_error("Image file is too large to decode: " + path.string());
    }

    std::vector<stbi_uc> encoded(static_cast<size_t>(fileSize));
    stream.seekg(0, std::ios::beg);
    stream.read(
        reinterpret_cast<char*>(encoded.data()),
        static_cast<std::streamsize>(encoded.size()));
    if (!stream) {
        throw std::runtime_error("Failed to read image: " + path.string());
    }
    return encoded;
}

} // namespace

ImageData loadRgbaImage(const std::filesystem::path& path)
{
    const std::vector<stbi_uc> encoded = readImageFile(path);

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    using StbiPixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
    const StbiPixels pixels(
        stbi_load_from_memory(
            encoded.data(),
            static_cast<int>(encoded.size()),
            &width,
            &height,
            &sourceChannels,
            STBI_rgb_alpha),
        &stbi_image_free);
    if (!pixels) {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error(
            "Failed to decode image '" + path.string() + "': "
            + (reason != nullptr ? reason : "unknown decoder error"));
    }
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid image dimensions: " + path.string());
    }

    const size_t imageWidth = static_cast<size_t>(width);
    const size_t imageHeight = static_cast<size_t>(height);
    constexpr size_t rgbaChannels = 4;
    if (imageWidth > std::numeric_limits<size_t>::max() / rgbaChannels / imageHeight) {
        throw std::runtime_error("Decoded image is too large: " + path.string());
    }

    ImageData image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.rgba.resize(imageWidth * imageHeight * rgbaChannels);
    std::memcpy(image.rgba.data(), pixels.get(), image.rgba.size());
    return image;
}

} // namespace sokoban
