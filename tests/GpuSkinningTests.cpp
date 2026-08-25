#include "engine/render/GpuSkinning.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>

namespace {

using namespace sokoban;

int failures = 0;
int checks = 0;
const char* currentTest = "";

void checkImpl(bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] line " << line
                  << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

Mat4 identity()
{
    Mat4 result;
    result.values = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    return result;
}

Vec3 transformPoint(const Mat4& matrix, Vec3 point)
{
    return {
        matrix.values[0] * point.x + matrix.values[4] * point.y +
            matrix.values[8] * point.z + matrix.values[12],
        matrix.values[1] * point.x + matrix.values[5] * point.y +
            matrix.values[9] * point.z + matrix.values[13],
        matrix.values[2] * point.x + matrix.values[6] * point.y +
            matrix.values[10] * point.z + matrix.values[14],
    };
}

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

bool near(Vec3 left, Vec3 right)
{
    return std::abs(left.x - right.x) < 0.0001f &&
        std::abs(left.y - right.y) < 0.0001f &&
        std::abs(left.z - right.z) < 0.0001f;
}

void testPaletteAndAttachmentEncoding()
{
    TEST("paletteAndAttachmentEncoding");
    SkinnedMeshData mesh;
    mesh.nodes = { SkeletonNode { .name = "root" } };
    mesh.jointNodeIndices = { 0 };
    mesh.inverseBindMatrices = { identity() };
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

    const std::vector<GpuSkinnedVertex> vertices = makeGpuSkinnedVertices(mesh);
    CHECK(vertices.size() == 2);
    CHECK(vertices[0].attachmentNodeIndex == UINT32_MAX);
    CHECK(vertices[1].attachmentNodeIndex == 0);
    CHECK(std::abs(vertices[1].position.x - 2.0f) < 0.0001f);
    CHECK(std::abs(vertices[1].position.y - 4.0f) < 0.0001f);
    CHECK(std::abs(vertices[1].position.z + 3.0f) < 0.0001f);
    const std::vector<uint32_t> indices = makeGpuSkinnedIndices(mesh);
    CHECK(indices.size() == 1);
    CHECK(indices[0] == 1);

    const GltfAnimationClip animation;
    const SkinnedPoseMatrices pose = sampleGltfSkinPose(mesh, animation, 0.0f);
    const GpuSkinningInstance instance = makeGpuSkinningInstance(mesh, pose);
    CHECK(instance.palette[0].values == identity().values);
    CHECK(instance.palette[maxSkinJoints].values == identity().values);
    CHECK(std::abs(instance.modelFromSource.values[0] - 0.5f) < 0.0001f);
    CHECK(std::abs(instance.modelFromSource.values[9] + 1.0f / 6.0f) < 0.0001f);
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
    for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
        if (!near(
                gpuSkinnedPosition(instance, mesh.vertices[index]),
                cpuMesh.vertices[index].position)) {
            ++mismatchedVertexCount;
        }
    }
    CHECK(mismatchedVertexCount == 0);
}

} // namespace

int main()
{
    testPaletteAndAttachmentEncoding();
    testRoguePaletteMatchesCpuSkinning();
    if (failures == 0) {
        std::cout << "GpuSkinningTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "GpuSkinningTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
