#pragma once

#include "engine/render/GltfMesh.hpp"
#include "engine/render/RenderTypes.hpp"

#include <cstdint>
#include <string_view>

namespace sokoban {

enum class ModelGeometry {
    Static,
    Skinned,
};

enum class ModelMaterialMode : uint32_t {
    Untextured = 0,
    SingleTexture = 1,
    PrimitiveTextureIndex = 2,
};

struct ModelAssetDefinition {
    RenderModel model = RenderModel::Cube;
    std::string_view path;
    ModelGeometry geometry = ModelGeometry::Static;
    GltfMeshLoadOptions loadOptions {};
    ModelMaterialMode materialMode = ModelMaterialMode::Untextured;
    uint32_t textureIndex = 0;
};

struct AnimationAssetDefinition {
    RenderAnimation animation = RenderAnimation::None;
    std::string_view path;
    uint32_t animationNumber = 0;
};

struct TextureAssetDefinition {
    std::string_view name;
    std::string_view path;
};

[[nodiscard]] constexpr float shaderValue(ModelMaterialMode mode)
{
    return static_cast<float>(mode);
}

} // namespace sokoban
