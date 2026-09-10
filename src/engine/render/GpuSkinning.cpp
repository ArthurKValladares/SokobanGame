#include "engine/render/GpuSkinning.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace sokoban {
namespace {

uint32_t checkedCount(uint64_t count, const char* label)
{
    if (count > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(
            std::string("Skinned mesh exceeds GPU ") + label + " limit");
    }
    return static_cast<uint32_t>(count);
}

} // namespace

uint64_t PackedGpuSkinnedMesh::uploadBytes() const
{
    return static_cast<uint64_t>(vertices.size()) * sizeof(GpuSkinnedVertex) +
        static_cast<uint64_t>(indices.size()) * sizeof(uint32_t);
}

uint64_t PackedGpuSkinnedMesh::allocatedBytes() const
{
    return static_cast<uint64_t>(vertices.capacity()) *
            sizeof(GpuSkinnedVertex) +
        static_cast<uint64_t>(indices.capacity()) * sizeof(uint32_t);
}

uint32_t PackedGpuSkinnedMesh::allocationCount() const
{
    return static_cast<uint32_t>(vertices.capacity() != 0) +
        static_cast<uint32_t>(indices.capacity() != 0);
}

GpuSkinnedMeshLayout inspectGpuSkinnedMeshLayout(
    const SkinnedMeshData& mesh)
{
    if (mesh.jointNodeIndices.size() > maxSkinJoints ||
        mesh.nodes.size() > maxSkeletonNodes) {
        throw std::runtime_error("Skinned mesh exceeds GPU palette limits");
    }
    for (const uint32_t index : mesh.indices) {
        if (index >= mesh.vertices.size()) {
            throw std::runtime_error("Skinned mesh contains an invalid vertex index");
        }
    }

    uint64_t vertexCount = mesh.vertices.size();
    uint64_t indexCount = mesh.indices.size();
    for (const SkinnedAttachment& attachment : mesh.attachments) {
        if (attachment.nodeIndex >= mesh.nodes.size()) {
            throw std::runtime_error("Skinned attachment references an invalid node");
        }
        for (const uint32_t index : attachment.mesh.indices) {
            if (index >= attachment.mesh.vertices.size()) {
                throw std::runtime_error(
                    "Skinned attachment contains an invalid vertex index");
            }
        }
        vertexCount += attachment.mesh.vertices.size();
        indexCount += attachment.mesh.indices.size();
    }

    const uint32_t checkedVertices = checkedCount(vertexCount, "vertex-count");
    const uint32_t checkedIndices = checkedCount(indexCount, "index-count");
    return {
        .vertexCount = checkedVertices,
        .indexCount = checkedIndices,
        .vertexBytes = vertexCount * sizeof(GpuSkinnedVertex),
        .indexBytes = indexCount * sizeof(uint32_t),
    };
}

PackedGpuSkinnedMesh packGpuSkinnedMesh(
    const SkinnedMeshData& mesh)
{
    const GpuSkinnedMeshLayout layout = inspectGpuSkinnedMeshLayout(mesh);
    PackedGpuSkinnedMesh result;
    result.vertices.reserve(layout.vertexCount);
    result.indices.reserve(layout.indexCount);

    for (const SkinnedVertex& vertex : mesh.vertices) {
        result.vertices.push_back({
            .position = vertex.position,
            .normal = vertex.normal,
            .tangent = vertex.tangent,
            .uv = vertex.uv,
            .uv1 = vertex.uv1,
            .materialIndex = vertex.materialIndex,
            .joints = vertex.joints,
            .weights = vertex.weights,
        });
    }
    for (const SkinnedAttachment& attachment : mesh.attachments) {
        for (const MeshVertex& vertex : attachment.mesh.vertices) {
            result.vertices.push_back({
                .position = { vertex.position.x, vertex.position.z, -vertex.position.y },
                .normal = { vertex.normal.x, vertex.normal.z, -vertex.normal.y },
                // Same axis swap as the normal, and the handedness rides
                // along untouched: swapping axes this way is a rotation.
                .tangent = { vertex.tangent.x, vertex.tangent.z,
                    -vertex.tangent.y, vertex.tangent.w },
                .uv = vertex.uv,
                .uv1 = vertex.uv1,
                .materialIndex = vertex.materialIndex,
                .attachmentNodeIndex = attachment.nodeIndex,
            });
        }
    }
    result.indices.insert(
        result.indices.end(), mesh.indices.begin(), mesh.indices.end());
    uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());
    for (const SkinnedAttachment& attachment : mesh.attachments) {
        for (const uint32_t index : attachment.mesh.indices) {
            result.indices.push_back(baseVertex + index);
        }
        baseVertex += static_cast<uint32_t>(attachment.mesh.vertices.size());
    }
    return result;
}

GpuSkinningInstance makeGpuSkinningInstance(
    const SkinnedMeshData& mesh,
    const SkinnedPoseMatrices& pose)
{
    if (pose.jointMatrices.size() != mesh.jointNodeIndices.size() ||
        pose.nodeMatrices.size() != mesh.nodes.size() ||
        pose.jointMatrices.size() > maxSkinJoints ||
        pose.nodeMatrices.size() > maxSkeletonNodes) {
        throw std::runtime_error("Invalid GPU skinning palette");
    }
    GpuSkinningInstance result;
    const Mat4 identityMatrix = mat4Identity;
    result.palette.fill(identityMatrix);
    for (uint32_t index = 0; index < pose.jointMatrices.size(); ++index) {
        result.palette[index] = pose.jointMatrices[index];
    }
    for (uint32_t index = 0; index < pose.nodeMatrices.size(); ++index) {
        result.palette[maxSkinJoints + index] = pose.nodeMatrices[index];
    }
    const GltfSourceTransform sourceTransform = makeGltfSourceTransform(
        mesh.sourceMinimum,
        mesh.sourceMaximum,
        {
            .preserveAspectRatio = mesh.preserveAspectRatio,
            .preserveSourceScale = mesh.preserveSourceScale,
            .rotateHalfTurn = mesh.rotateHalfTurn,
        });
    result.modelFromSource = sourceTransform.modelFromSource;
    result.normalFromSource = sourceTransform.normalFromSource;
    return result;
}

} // namespace sokoban
