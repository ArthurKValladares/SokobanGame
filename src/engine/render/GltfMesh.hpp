#pragma once

#include "engine/Math.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace sokoban {

struct MeshVertex {
    Vec3 position {};
    Vec3 normal {};
    Vec2 uv {};
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
};

struct GltfMeshLoadOptions {
    bool preserveAspectRatio = false;
    bool rotateHalfTurn = false;
};

[[nodiscard]] MeshData loadGltfMesh(
    const std::filesystem::path& path,
    GltfMeshLoadOptions options = {});

} // namespace sokoban
