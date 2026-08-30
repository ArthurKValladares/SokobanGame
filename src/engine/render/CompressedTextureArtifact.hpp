#pragma once

#include "engine/TextureSource.hpp"
#include "engine/render/ImageData.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace sokoban {

enum class CompressedTextureFormat : uint32_t {
    Bc7Unorm = 145,
    Bc7Srgb = 146,
};

struct CompressedTextureMip {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::byte> bytes;
};

struct CompressedTextureArtifact {
    CompressedTextureFormat format = CompressedTextureFormat::Bc7Srgb;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<CompressedTextureMip> mips;

    [[nodiscard]] uint64_t residentBytes() const;
};

// The stable identity digest keeps source URIs out of package paths. Sampling
// and colour-space interpretation are part of the digest, so incompatible
// views of the same source bytes never share a Vulkan image.
[[nodiscard]] std::filesystem::path compressedTextureArtifactPath(
    const TextureSourceIdentity& identity);

// Creates a little-endian KTX 2.0 file containing native BC7 blocks. The mip
// pyramid is complete only when the authored minification filter uses mips.
[[nodiscard]] std::vector<std::byte> buildBc7Ktx2(
    const ImageData& source,
    const TextureInterpretation& interpretation);

[[nodiscard]] CompressedTextureArtifact parseBc7Ktx2(
    std::span<const std::byte> bytes,
    const std::filesystem::path& diagnosticPath = {});
[[nodiscard]] CompressedTextureArtifact loadBc7Ktx2(
    const std::filesystem::path& path);

} // namespace sokoban
