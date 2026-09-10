#include "TestHarness.hpp"

#include "engine/render/GpuSkinning.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>

namespace {

using namespace sokoban;

Vec3 gpuSkinnedPosition(
    const GpuSkinningInstance& instance,
    const SkinnedVertex& vertex)
{
    Vec3 sourcePosition {};
    for (std::size_t influence = 0; influence < vertex.weights.size();
         ++influence) {
        const float weight = vertex.weights[influence];
        const uint16_t joint = vertex.joints[influence];
        if (weight <= 0.0f || joint >= maxSkinJoints) {
            continue;
        }
        const Vec3 transformed = transformPoint(
            instance.palette[joint], vertex.position);
        sourcePosition.x += transformed.x * weight;
        sourcePosition.y += transformed.y * weight;
        sourcePosition.z += transformed.z * weight;
    }
    return transformPoint(instance.modelFromSource, sourcePosition);
}

Vec3 gpuSkinnedNormal(
    const GpuSkinningInstance& instance,
    const SkinnedVertex& vertex)
{
    Vec3 sourceNormal {};
    for (std::size_t influence = 0; influence < vertex.weights.size();
         ++influence) {
        const float weight = vertex.weights[influence];
        const uint16_t joint = vertex.joints[influence];
        if (weight <= 0.0f || joint >= maxSkinJoints) {
            continue;
        }
        sourceNormal +=
            transformVector(instance.palette[joint], vertex.normal) * weight;
    }
    sourceNormal = normalizeOr(sourceNormal, vertex.normal);
    return normalizeOr(
        transformVector(instance.normalFromSource, sourceNormal),
        Vec3 { 0.0f, 0.0f, 1.0f });
}

Vec3 gpuSkinnedTangent(
    const GpuSkinningInstance& instance,
    const SkinnedVertex& vertex,
    Vec3 normal)
{
    Vec3 sourceTangent {};
    const Vec3 tangent {
        vertex.tangent.x, vertex.tangent.y, vertex.tangent.z };
    for (std::size_t influence = 0; influence < vertex.weights.size();
         ++influence) {
        const float weight = vertex.weights[influence];
        const uint16_t joint = vertex.joints[influence];
        if (weight <= 0.0f || joint >= maxSkinJoints) {
            continue;
        }
        sourceTangent +=
            transformVector(instance.palette[joint], tangent) * weight;
    }
    Vec3 transformed =
        transformVector(instance.modelFromSource, sourceTangent);
    return normalizeOr(transformed - normal * dot(normal, transformed), Vec3 {});
}

bool near(Vec3 left, Vec3 right)
{
    return std::abs(left.x - right.x) < 0.0001f &&
        std::abs(left.y - right.y) < 0.0001f &&
        std::abs(left.z - right.z) < 0.0001f;
}

void testModelInstanceDrawReadiness()
{
    TEST("modelInstanceDrawReadiness");

    // Static models do not publish animation poses.
    CHECK(modelInstanceReadyForDraw(true, false, false));
    CHECK(!modelInstanceReadyForDraw(false, false, true));

    // Every skinned model is withheld until its current instance pose exists;
    // this is independent of which actor or animation owns that instance.
    CHECK(!modelInstanceReadyForDraw(true, true, false));
    CHECK(modelInstanceReadyForDraw(true, true, true));
    CHECK(!modelInstanceReadyForDraw(false, true, true));
}

void testPaletteAndAttachmentEncoding()
{
    TEST("paletteAndAttachmentEncoding");
    SkinnedMeshData mesh;
    mesh.nodes = { SkeletonNode { .name = "root" } };
    mesh.jointNodeIndices = { 0 };
    mesh.inverseBindMatrices = { mat4Identity };
    mesh.sourceMinimum = { 0.0f, 0.0f, 0.0f };
    mesh.sourceMaximum = { 2.0f, 4.0f, 6.0f };
    mesh.vertices = {
        SkinnedVertex {
            .position = { 1.0f, 2.0f, 3.0f },
            .normal = { 0.0f, 1.0f, 0.0f },
            .joints = { 0, 0, 0, 0 },
            .weights = { 1.0f, 0.0f, 0.0f, 0.0f },
        },
    };
    mesh.attachments = {
        SkinnedAttachment {
            .mesh = MeshData {
                .vertices = { MeshVertex { .position = { 2.0f, 3.0f, 4.0f } } },
                .indices = { 0 },
            },
            .nodeIndex = 0,
        },
    };

    const GpuSkinnedMeshLayout layout = inspectGpuSkinnedMeshLayout(mesh);
    CHECK(layout.vertexCount == 2);
    CHECK(layout.indexCount == 1);
    CHECK(layout.vertexBytes == 2 * sizeof(GpuSkinnedVertex));
    CHECK(layout.indexBytes == sizeof(uint32_t));
    const PackedGpuSkinnedMesh packed = packGpuSkinnedMesh(mesh);
    CHECK(packed.vertices.size() == 2);
    CHECK(packed.vertices[0].attachmentNodeIndex == UINT32_MAX);
    CHECK(packed.vertices[1].attachmentNodeIndex == 0);
    CHECK(std::abs(packed.vertices[1].position.x - 2.0f) < 0.0001f);
    CHECK(std::abs(packed.vertices[1].position.y - 4.0f) < 0.0001f);
    CHECK(std::abs(packed.vertices[1].position.z + 3.0f) < 0.0001f);
    CHECK(packed.indices.size() == 1);
    CHECK(packed.indices[0] == 1);
    CHECK(packed.uploadBytes() == layout.uploadBytes());
    CHECK(packed.allocatedBytes() >= packed.uploadBytes());
    CHECK(packed.allocationCount() == 2);

    const GltfAnimationClip animation;
    const SkinnedPoseMatrices pose = sampleGltfSkinPose(mesh, animation, 0.0f);
    const GpuSkinningInstance instance = makeGpuSkinningInstance(mesh, pose);
    CHECK(instance.palette[0] == mat4Identity);
    CHECK(instance.palette[maxSkinJoints] == mat4Identity);
    CHECK(std::abs(instance.modelFromSource.values[0] - 0.5f) < 0.0001f);
    CHECK(std::abs(instance.modelFromSource.values[9] + 1.0f / 6.0f) < 0.0001f);
}

void testLayoutRejectsInvalidGeometryWithoutPacking()
{
    TEST("layoutRejectsInvalidGeometryWithoutPacking");
    SkinnedMeshData mesh;
    mesh.nodes = { SkeletonNode { .name = "root" } };
    mesh.vertices = { SkinnedVertex {} };
    mesh.indices = { 1 };
    checkThrows([&] {
        (void)inspectGpuSkinnedMeshLayout(mesh);
    }, "base mesh index outside vertex range");

    mesh.indices = { 0 };
    mesh.attachments = {
        SkinnedAttachment {
            .mesh = MeshData {
                .vertices = { MeshVertex {} },
                .indices = { 0 },
            },
            .nodeIndex = 1,
        },
    };
    checkThrows([&] {
        (void)inspectGpuSkinnedMeshLayout(mesh);
    }, "attachment node outside skeleton range");
}

void testNonuniformNormalizationPreservesSkinnedBasis()
{
    TEST("nonuniformNormalizationPreservesSkinnedBasis");
    const float inverseSqrt5 = 1.0f / std::sqrt(5.0f);
    const Vec3 normal { inverseSqrt5, 2.0f * inverseSqrt5, 0.0f };
    const Vec4 tangent {
        2.0f * inverseSqrt5, -inverseSqrt5, 0.0f, -1.0f };

    SkinnedMeshData mesh;
    mesh.nodes = { SkeletonNode { .name = "root" } };
    mesh.jointNodeIndices = { 0 };
    mesh.inverseBindMatrices = { mat4Identity };
    mesh.sourceMinimum = { 0.0f, 0.0f, 0.0f };
    mesh.sourceMaximum = { 4.0f, 2.0f, 1.0f };
    for (const Vec3 position : {
             Vec3 { 0.0f, 2.0f, 0.0f },
             Vec3 { 4.0f, 0.0f, 0.0f },
             Vec3 { 0.0f, 2.0f, 1.0f },
         }) {
        mesh.vertices.push_back({
            .position = position,
            .normal = normal,
            .tangent = tangent,
            .joints = { 0, 0, 0, 0 },
            .weights = { 1.0f, 0.0f, 0.0f, 0.0f },
        });
    }
    mesh.indices = { 0, 1, 2 };

    const GltfAnimationClip bindPose;
    const MeshData cpu = skinGltfMesh(mesh, bindPose, 0.0f);
    const GpuSkinningInstance gpu = makeGpuSkinningInstance(
        mesh, sampleGltfSkinPose(mesh, bindPose, 0.0f));
    const Vec3 edge = cpu.vertices[1].position - cpu.vertices[0].position;
    const float inverseSqrt2 = 1.0f / std::sqrt(2.0f);
    for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
        const Vec3 gpuNormal = gpuSkinnedNormal(gpu, mesh.vertices[index]);
        const Vec3 gpuTangent =
            gpuSkinnedTangent(gpu, mesh.vertices[index], gpuNormal);
        const Vec3 cpuTangent {
            cpu.vertices[index].tangent.x,
            cpu.vertices[index].tangent.y,
            cpu.vertices[index].tangent.z,
        };
        CHECK(near(gpuSkinnedPosition(gpu, mesh.vertices[index]),
            cpu.vertices[index].position));
        CHECK(near(gpuNormal, cpu.vertices[index].normal));
        CHECK(near(gpuTangent, cpuTangent));
        CHECK(std::abs(dot(cpu.vertices[index].normal, edge)) < 0.0001f);
        CHECK(std::abs(dot(cpu.vertices[index].normal, cpuTangent)) < 0.0001f);
        CHECK(std::abs(cpu.vertices[index].normal.x - inverseSqrt2) <
            0.0001f);
        CHECK(std::abs(cpu.vertices[index].normal.z - inverseSqrt2) <
            0.0001f);
        CHECK(cpu.vertices[index].tangent.w == -1.0f);
    }

    const GltfSourceTransform degenerate = makeGltfSourceTransform(
        { 2.0f, 3.0f, 4.0f }, { 2.0f, 3.0f, 4.0f });
    for (const float value : degenerate.modelFromSource.values) {
        CHECK(std::isfinite(value));
    }
    for (const float value : degenerate.normalFromSource.values) {
        CHECK(std::isfinite(value));
    }
}

void testRoguePaletteMatchesCpuSkinning()
{
    TEST("roguePaletteMatchesCpuSkinning");
    const std::filesystem::path assets = SOKOBAN_TEST_ASSET_DIR;
    const SkinnedMeshData mesh = loadGltfSkinnedMesh(
        assets / "KayKit Adventurers 2.0/Characters/gltf/Rogue.glb",
        {
            .preserveAspectRatio = true,
            .rotateHalfTurn = true,
        });
    const GltfAnimationClip animation = loadGltfAnimationClip(
        assets / "KayKit Adventurers 2.0/Animations/gltf/Rig_Medium/"
                 "Rig_Medium_MovementBasic.glb",
        7);
    const float timeSeconds = 0.37f;
    const MeshData cpuMesh = skinGltfMesh(mesh, animation, timeSeconds);
    const GpuSkinningInstance instance = makeGpuSkinningInstance(
        mesh, sampleGltfSkinPose(mesh, animation, timeSeconds));

    CHECK(cpuMesh.vertices.size() >= mesh.vertices.size());
    std::size_t mismatchedVertexCount = 0;
    std::size_t mismatchedNormalCount = 0;
    std::size_t mismatchedTangentCount = 0;
    for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
        if (!near(
                gpuSkinnedPosition(instance, mesh.vertices[index]),
                cpuMesh.vertices[index].position)) {
            ++mismatchedVertexCount;
        }
        const Vec3 gpuNormal = gpuSkinnedNormal(instance, mesh.vertices[index]);
        if (!near(gpuNormal, cpuMesh.vertices[index].normal)) {
            ++mismatchedNormalCount;
        }
        const Vec3 cpuTangent {
            cpuMesh.vertices[index].tangent.x,
            cpuMesh.vertices[index].tangent.y,
            cpuMesh.vertices[index].tangent.z,
        };
        if (!near(
                gpuSkinnedTangent(instance, mesh.vertices[index], gpuNormal),
                cpuTangent)) {
            ++mismatchedTangentCount;
        }
    }
    CHECK(mismatchedVertexCount == 0);
    CHECK(mismatchedNormalCount == 0);
    CHECK(mismatchedTangentCount == 0);
}

} // namespace

int main()
{
    testModelInstanceDrawReadiness();
    testPaletteAndAttachmentEncoding();
    testLayoutRejectsInvalidGeometryWithoutPacking();
    testNonuniformNormalizationPreservesSkinnedBasis();
    testRoguePaletteMatchesCpuSkinning();
    if (failures == 0) {
        std::cout << "GpuSkinningTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "GpuSkinningTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
