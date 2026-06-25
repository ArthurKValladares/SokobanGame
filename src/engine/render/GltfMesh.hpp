#pragma once

#include "engine/Math.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace sokoban {

struct MeshVertex {
    Vec3 position {};
    Vec3 normal {};
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
};

[[nodiscard]] MeshData loadGltfMesh(const std::filesystem::path& path);

} // namespace sokoban
