#include "engine/TextureSource.hpp"

#include <algorithm>
#include <cctype>
#include <type_traits>

namespace sokoban {
namespace {

void appendKeyPart(std::string& key, std::string_view part)
{
    key += std::to_string(part.size());
    key.push_back(':');
    key.append(part);
}

std::string pathKey(const std::filesystem::path& path)
{
    std::string key = path.generic_string();
#ifdef _WIN32
    std::ranges::transform(key, key.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
#endif
    return key;
}

} // namespace

std::string textureSourceIdentityKey(const TextureSourceIdentity& identity)
{
    std::string key;
    std::visit(
        [&key](const auto& source) {
            using Source = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<Source, ExternalTextureSource>) {
                key = "file:";
                appendKeyPart(key, pathKey(source.path));
            } else if constexpr (
                std::is_same_v<Source, GltfBufferViewTextureSource>) {
                key = "view:";
                appendKeyPart(key, pathKey(source.document));
                appendKeyPart(key, std::to_string(source.bufferViewIndex));
                appendKeyPart(key, source.mimeType);
            } else {
                key = "data:";
                appendKeyPart(key, source.uri);
            }
        },
        identity.source);
    key += identity.interpretation.colorSpace == TextureColorSpace::Srgb
        ? "|srgb"
        : "|linear";
    key += "|" + std::to_string(
        static_cast<uint32_t>(identity.interpretation.wrapU));
    key += "|" + std::to_string(
        static_cast<uint32_t>(identity.interpretation.wrapV));
    key += "|" + std::to_string(
        static_cast<uint32_t>(identity.interpretation.magFilter));
    key += "|" + std::to_string(
        static_cast<uint32_t>(identity.interpretation.minFilter));
    return key;
}

} // namespace sokoban
