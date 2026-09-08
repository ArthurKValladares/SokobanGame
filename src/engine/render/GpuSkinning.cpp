#include "engine/render/GpuSkinning.hpp"

#include <stdexcept>

namespace sokoban {

std::vector<GpuSkinnedVertex> makeGpuSkinnedVertices(const SkinnedMeshData& mesh)
{
    if (mesh.jointNodeIndices.size() > maxSkinJoints ||
        mesh.nodes.size() > maxSkeletonNodes) {
        throw std::runtime_error("Skinned mesh exceeds GPU palette limits");
    }

    std::vector<GpuSkinnedVertex> result;
    result.reserve(mesh.vertices.size());
    for (const SkinnedVertex& vertex : mesh.vertices) {
        result.push_back({
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
        if (attachment.nodeIndex >= mesh.nodes.size()) {
            throw std::runtime_error("Skinned attachment references an invalid node");
        }
        for (const MeshVertex& vertex : attachment.mesh.vertices) {
            result.push_back({
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
    return result;
}

std::vector<uint32_t> makeGpuSkinnedIndices(const SkinnedMeshData& mesh)
{
    std::vector<uint32_t> result = mesh.indices;
    uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());
    for (const SkinnedAttachment& attachment : mesh.attachments) {
        for (const uint32_t index : attachment.mesh.indices) {
            if (index >= attachment.mesh.vertices.size()) {
                throw std::runtime_error("Skinned attachment contains an invalid vertex index");
            }
            result.push_back(baseVertex + index);
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
