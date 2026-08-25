#include "engine/render/GpuSkinning.hpp"

#include <algorithm>
#include <stdexcept>

namespace sokoban {
namespace {

void negateOutputAxis(Mat4& matrix, uint32_t axis)
{
    for (uint32_t column = 0; column < 4; ++column) {
        matrix.values[column * 4 + axis] =
            -matrix.values[column * 4 + axis];
    }
}

Mat4 sourcePositionTransform(const SkinnedMeshData& mesh)
{
    const Vec3 extent {
        std::max(mesh.sourceMaximum.x - mesh.sourceMinimum.x, 0.000001f),
        std::max(mesh.sourceMaximum.y - mesh.sourceMinimum.y, 0.000001f),
        std::max(mesh.sourceMaximum.z - mesh.sourceMinimum.z, 0.000001f),
    };
    const float sourceHeight = extent.y;
    const Vec3 center {
        (mesh.sourceMinimum.x + mesh.sourceMaximum.x) * 0.5f,
        (mesh.sourceMinimum.y + mesh.sourceMaximum.y) * 0.5f,
        (mesh.sourceMinimum.z + mesh.sourceMaximum.z) * 0.5f,
    };
    Mat4 result = mat4Identity;
    if (mesh.preserveSourceScale) {
        result.values[5] = 0.0f;
        result.values[6] = 1.0f;
        result.values[9] = -1.0f;
        result.values[10] = 0.0f;
    } else if (mesh.preserveAspectRatio) {
        result.values[0] = 1.0f / sourceHeight;
        result.values[5] = 0.0f;
        result.values[6] = 1.0f / sourceHeight;
        result.values[9] = -1.0f / sourceHeight;
        result.values[10] = 0.0f;
        result.values[12] = 0.5f - center.x / sourceHeight;
        result.values[13] = 0.5f + center.z / sourceHeight;
        result.values[14] = -mesh.sourceMinimum.y / sourceHeight;
    } else {
        result.values[0] = 1.0f / extent.x;
        result.values[5] = 0.0f;
        result.values[6] = 1.0f / extent.y;
        result.values[9] = -1.0f / extent.z;
        result.values[10] = 0.0f;
        result.values[12] = -mesh.sourceMinimum.x / extent.x;
        result.values[13] = mesh.sourceMaximum.z / extent.z;
        result.values[14] = -mesh.sourceMinimum.y / extent.y;
    }
    if (mesh.rotateHalfTurn) {
        negateOutputAxis(result, 0);
        negateOutputAxis(result, 1);
        if (!mesh.preserveSourceScale) {
            // Normalized models rotate around the unit-square centre. The
            // axis negation above gives -x/-y; CPU skinning uses 1-x/1-y.
            // Restore that translation so the GPU path exactly matches the
            // established normalizedVertex convention.
            result.values[12] += 1.0f;
            result.values[13] += 1.0f;
        }
    }
    return result;
}

Mat4 sourceNormalTransform(const SkinnedMeshData& mesh)
{
    Mat4 result = mat4Identity;
    result.values[5] = 0.0f;
    result.values[6] = 1.0f;
    result.values[9] = -1.0f;
    result.values[10] = 0.0f;
    if (mesh.rotateHalfTurn) {
        negateOutputAxis(result, 0);
        negateOutputAxis(result, 1);
    }
    return result;
}

} // namespace

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
            .uv = vertex.uv,
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
                .uv = vertex.uv,
                .textureIndex = vertex.textureIndex,
                .materialFlags = vertex.materialFlags,
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
    result.modelFromSource = sourcePositionTransform(mesh);
    result.normalFromSource = sourceNormalTransform(mesh);
    return result;
}

} // namespace sokoban
