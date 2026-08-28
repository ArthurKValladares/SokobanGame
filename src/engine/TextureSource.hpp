#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>

namespace sokoban {

enum class TextureColorSpace {
    Srgb,
    Linear,
};

// A filesystem-backed image after its URI has been resolved relative to the
// declaring glTF and normalized against the explicit asset root. `path` is
// always relative to that root.
struct ExternalTextureSource {
    std::filesystem::path path;

    bool operator==(const ExternalTextureSource&) const = default;
};

// Image bytes addressed by a glTF buffer view. The document path is relative
// to the asset root, so a buffer-view index is never ambiguous across files.
struct GltfBufferViewTextureSource {
    std::filesystem::path document;
    uint32_t bufferViewIndex = 0;
    std::string mimeType;

    bool operator==(const GltfBufferViewTextureSource&) const = default;
};

// The complete URI is the source identity. Decoding and supported-media-type
// validation happen later; content discovery does not materialize a file.
struct DataUriTextureSource {
    std::string uri;

    bool operator==(const DataUriTextureSource&) const = default;
};

using TextureSource = std::variant<
    ExternalTextureSource,
    GltfBufferViewTextureSource,
    DataUriTextureSource>;

struct TextureInterpretation {
    TextureColorSpace colorSpace = TextureColorSpace::Srgb;

    bool operator==(const TextureInterpretation&) const = default;
};

// Source bytes and interpretation form the resource identity. In particular,
// the same bytes used as color and data remain distinct because Vulkan image
// formats cannot silently change their transfer function per material use.
struct TextureSourceIdentity {
    TextureSource source;
    TextureInterpretation interpretation;

    bool operator==(const TextureSourceIdentity&) const = default;
};

} // namespace sokoban
