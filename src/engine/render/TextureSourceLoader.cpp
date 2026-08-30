#include "engine/render/TextureSourceLoader.hpp"

#include "engine/render/GltfMesh.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace sokoban {
namespace {

int base64Value(unsigned char character)
{
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }
    if (character == '+') {
        return 62;
    }
    if (character == '/') {
        return 63;
    }
    return -1;
}

std::vector<std::byte> decodeBase64(std::string_view encoded)
{
    if (encoded.empty() || encoded.size() % 4 != 0) {
        throw std::runtime_error("Invalid base64 image data URI length");
    }
    std::vector<std::byte> result;
    result.reserve((encoded.size() / 4) * 3);
    for (std::size_t offset = 0; offset < encoded.size(); offset += 4) {
        std::array<int, 4> values {};
        std::size_t padding = 0;
        for (std::size_t lane = 0; lane < values.size(); ++lane) {
            const unsigned char character =
                static_cast<unsigned char>(encoded[offset + lane]);
            if (character == '=') {
                ++padding;
                values[lane] = 0;
            } else {
                if (padding != 0) {
                    throw std::runtime_error(
                        "Invalid base64 image data URI padding");
                }
                values[lane] = base64Value(character);
                if (values[lane] < 0) {
                    throw std::runtime_error(
                        "Invalid character in base64 image data URI");
                }
            }
        }
        if (padding > 2 || (padding != 0 && offset + 4 != encoded.size())) {
            throw std::runtime_error("Invalid base64 image data URI padding");
        }
        const uint32_t packed =
            (static_cast<uint32_t>(values[0]) << 18U) |
            (static_cast<uint32_t>(values[1]) << 12U) |
            (static_cast<uint32_t>(values[2]) << 6U) |
            static_cast<uint32_t>(values[3]);
        result.push_back(static_cast<std::byte>(packed >> 16U));
        if (padding < 2) {
            result.push_back(static_cast<std::byte>(packed >> 8U));
        }
        if (padding == 0) {
            result.push_back(static_cast<std::byte>(packed));
        }
    }
    return result;
}

std::vector<std::byte> dataUriBytes(const std::string& uri)
{
    const std::size_t comma = uri.find(',');
    if (comma == std::string::npos ||
        (!uri.starts_with("data:image/png;base64,") &&
            !uri.starts_with("data:image/jpeg;base64,"))) {
        throw std::runtime_error("Unsupported image data URI");
    }
    return decodeBase64(std::string_view(uri).substr(comma + 1));
}

} // namespace

ImageData loadRgbaTextureSource(
    const std::filesystem::path& assetRoot,
    const TextureSource& source)
{
    return std::visit(
        [&assetRoot](const auto& typed) -> ImageData {
            using Source = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Source, ExternalTextureSource>) {
                return loadRgbaImage(assetRoot / typed.path);
            } else if constexpr (
                std::is_same_v<Source, GltfBufferViewTextureSource>) {
                const std::vector<std::byte> bytes =
                    loadGltfBufferViewBytes(
                        assetRoot / typed.document,
                        typed.bufferViewIndex);
                return loadRgbaImage(
                    bytes,
                    typed.document.string() + " buffer view " +
                        std::to_string(typed.bufferViewIndex));
            } else {
                const std::vector<std::byte> bytes = dataUriBytes(typed.uri);
                return loadRgbaImage(bytes, "glTF image data URI");
            }
        },
        source);
}

PreparedTextureSource loadPreparedTextureSource(
    const std::filesystem::path& assetRoot,
    const TextureSourceIdentity& identity,
    bool supportsBc7)
{
    if (supportsBc7) {
        const std::filesystem::path artifactPath =
            assetRoot / compressedTextureArtifactPath(identity);
        std::error_code error;
        if (std::filesystem::is_regular_file(artifactPath, error)) {
            CompressedTextureArtifact artifact = loadBc7Ktx2(artifactPath);
            const CompressedTextureFormat expected =
                identity.interpretation.colorSpace == TextureColorSpace::Srgb
                ? CompressedTextureFormat::Bc7Srgb
                : CompressedTextureFormat::Bc7Unorm;
            if (artifact.format != expected) {
                throw std::runtime_error(
                    "Compressed texture artifact has the wrong colour space: " +
                    artifactPath.string());
            }
            return artifact;
        }
    }
    return loadRgbaTextureSource(assetRoot, identity.source);
}

} // namespace sokoban
