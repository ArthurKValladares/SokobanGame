#include "TestAssetRoot.hpp"
#include "TestHarness.hpp"

#include "engine/render/GltfMesh.hpp"
#include "engine/render/GpuSkinning.hpp"
#include "engine/render/ImageData.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace {

void testRobotGeometryAndDirectionCues()
{
    TEST("robot geometry and direction cues");
    const auto mesh = sokoban::loadGltfMesh(
        testAssetRoot() / "custom/models/robot.glb",
        { .preserveSourceScale = true });
    CHECK(!mesh.vertices.empty());
    CHECK(!mesh.indices.empty());
    CHECK(mesh.indices.size() % 3 == 0);
    CHECK(mesh.materials.size() >= 8);
    for (const auto& vertex : mesh.vertices) {
        CHECK(std::isfinite(vertex.position.x));
        CHECK(std::isfinite(vertex.position.y));
        CHECK(std::isfinite(vertex.position.z));
        CHECK(vertex.position.x >= 0.0f && vertex.position.x <= 1.0f);
        CHECK(vertex.position.y >= 0.0f && vertex.position.y <= 1.0f);
        CHECK(vertex.position.z >= 0.0f && vertex.position.z <= 1.005f);
        CHECK(vertex.materialIndex < mesh.materials.size());
        CHECK(std::isfinite(vertex.normal.x));
        CHECK(std::isfinite(vertex.normal.y));
        CHECK(std::isfinite(vertex.normal.z));
    }
    for (const auto index : mesh.indices) {
        CHECK(index < mesh.vertices.size());
    }
    // Source models face south; render-frame orientation rotates the bright
    // leading lanterns to match the robot's current movement direction.
    const auto hasVertex = [&](uint32_t material, auto predicate) {
        return std::ranges::any_of(mesh.vertices, [&](const auto& vertex) {
            return vertex.materialIndex == material && predicate(vertex.position);
        });
    };
    CHECK(hasVertex(5, [](auto p) { return p.y > 0.99f && p.z < 0.8f; }));
    CHECK(hasVertex(6, [](auto p) { return p.y < 0.01f && p.z < 0.8f; }));
    CHECK(hasVertex(4, [](auto p) { return p.z > 1.0f && p.y > 0.7f; }));
    CHECK(hasVertex(4, [](auto p) { return p.z > 1.0f && p.y < 0.3f; }));
    CHECK(std::ranges::any_of(mesh.vertices, [](const auto& vertex) {
        return std::abs(vertex.position.z - 1.0f) < 0.0001f && vertex.normal.z > 0.9f;
    }));
    CHECK(hasVertex(2, [](auto p) { return p.z < 0.06f && p.x < 0.25f; }));
    CHECK(hasVertex(2, [](auto p) { return p.z < 0.06f && p.x > 0.75f; }));
    if (mesh.materials.size() >= 8) {
        const auto& front = mesh.materials[5];
        const auto& rear = mesh.materials[6];
        CHECK(front.baseColorFactor.x > front.baseColorFactor.y);
        CHECK(front.baseColorFactor.y > front.baseColorFactor.z);
        CHECK(front.emissiveFactor.y > 0.3f);
        CHECK(front.emissiveFactor.y > rear.emissiveFactor.y * 3.0f);
        CHECK(rear.baseColorFactor.x > rear.baseColorFactor.y);
        CHECK(rear.emissiveFactor.y < 0.1f);
    }
}

void testRobotDriveRigUsesDistanceAndMovesOnlyItsMechanism()
{
    TEST("robot drive rig follows distance and keeps the deck fixed");
    using namespace sokoban;
    const auto path = testAssetRoot() / "custom/models/robot.glb";
    const auto rig = loadGltfSkinnedMesh(path, { .preserveSourceScale = true });
    const auto drive = loadGltfAnimationClip(path, 0);
    CHECK(drive.name == "Drive");
    CHECK(std::abs(drive.durationSeconds - 2.0f) < 0.0001f);
    CHECK(rig.jointNodeIndices.size() == 57);
    CHECK(rig.jointNodeIndices.size() <= maxSkinJoints);
    CHECK(rig.nodes.size() <= maxSkeletonNodes);
    const auto layout = inspectGpuSkinnedMeshLayout(rig);
    CHECK(layout.vertexCount == rig.vertices.size());
    CHECK(layout.indexCount == rig.indices.size());
    CHECK(layout.uploadBytes() > 0);

    const auto start = skinGltfMesh(rig, drive, 0.0f);
    const auto bind = loadGltfMesh(path, { .preserveSourceScale = true });
    const auto moving = skinGltfMesh(rig, drive, 0.137f);
    const auto end = skinGltfMesh(rig, drive, 2.0f, AnimationPlaybackMode::Clamp);
    CHECK(start.vertices.size() == rig.vertices.size());
    bool shoeMoved = false;
    bool rollerMoved = false;
    bool cogMoved = false;
    const auto distanceSquared = [](Vec3 a, Vec3 b) {
        const auto difference = a - b;
        return dot(difference, difference);
    };
    for (std::size_t index = 0; index < rig.vertices.size(); ++index) {
        const auto& vertex = rig.vertices[index];
        CHECK(distanceSquared(start.vertices[index].position, bind.vertices[index].position) < 0.0000001f);
        CHECK(vertex.weights[0] == 1.0f);
        CHECK(vertex.weights[1] == 0.0f && vertex.weights[2] == 0.0f && vertex.weights[3] == 0.0f);
        CHECK(vertex.joints[0] < rig.jointNodeIndices.size());
        const auto& node = rig.nodes[rig.jointNodeIndices[vertex.joints[0]]];
        const float travelled = distanceSquared(start.vertices[index].position, moving.vertices[index].position);
        if (node.name == "RobotBody") {
            CHECK(travelled < 0.0000001f);
        } else if (node.name.starts_with("RobotShoe")) {
            shoeMoved = shoeMoved || travelled > 0.0001f;
        } else if (node.name.starts_with("RobotRoller")) {
            rollerMoved = rollerMoved || travelled > 0.0001f;
        } else if (node.name.starts_with("RobotCog")) {
            cogMoved = cogMoved || travelled > 0.0001f;
        }
        CHECK(distanceSquared(start.vertices[index].position, end.vertices[index].position) < 0.0000001f);
    }
    CHECK(shoeMoved && rollerMoved && cogMoved);

    // Every moving shoe travels at one tile per clip second on the straight
    // lower run; its rigid bind pose and joint path agree at phase zero.
    const auto pose0 = sampleGltfSkinPose(rig, drive, 0.0f);
    const auto poseQuarter = sampleGltfSkinPose(rig, drive, 0.25f);
    const auto shoe = std::ranges::find(rig.nodes, std::string("RobotShoeL00"), &SkeletonNode::name);
    CHECK(shoe != rig.nodes.end());
    if (shoe != rig.nodes.end()) {
        const auto nodeIndex = static_cast<std::size_t>(shoe - rig.nodes.begin());
        // Source glTF Z=-worldY, so the reverse-moving lower run adds +0.25.
        CHECK(std::abs(poseQuarter.nodeMatrices[nodeIndex].values[14] -
                pose0.nodeMatrices[nodeIndex].values[14] - 0.25f) < 0.0001f);
    }
    for (float time : { 0.0f, 0.031f, 0.25f, 0.5f, 0.731f, 1.0f, 1.337f, 1.75f, 1.99f }) {
        const auto mesh = skinGltfMesh(rig, drive, time);
        for (const auto& vertex : mesh.vertices) {
            CHECK(vertex.position.x >= -0.00001f && vertex.position.x <= 1.00001f);
            CHECK(vertex.position.y >= -0.00001f && vertex.position.y <= 1.00001f);
            CHECK(vertex.position.z >= -0.00001f && vertex.position.z <= 1.00501f);
        }
    }
}

void testRobotPaletteThumbnails()
{
    TEST("robot palette thumbnails");
    constexpr std::array<const char*, 4> names {
        "tile_robot_north.png", "tile_robot_east.png",
        "tile_robot_south.png", "tile_robot_west.png",
    };
    std::array<sokoban::ImageData, 4> images;
    for (std::size_t index = 0; index < names.size(); ++index) {
        images[index] = sokoban::loadRgbaImage(
            testAssetRoot() / "custom/thumbnails" / names[index]);
        CHECK(images[index].width >= 64);
        CHECK(images[index].height >= 64);
        CHECK(images[index].rgba.size() ==
            static_cast<std::size_t>(images[index].width) * images[index].height * 4);
        for (std::size_t previous = 0; previous < index; ++previous) {
            // Shared models still need distinct views for the four headings.
            CHECK(images[index].rgba != images[previous].rgba);
        }
    }
}

} // namespace

int main()
{
    testRobotGeometryAndDirectionCues();
    testRobotDriveRigUsesDistanceAndMovesOnlyItsMechanism();
    testRobotPaletteThumbnails();
    if (failures == 0) {
        std::cout << "RobotAssetTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "RobotAssetTests: " << failures << " of " << checks << " checks failed\n";
    return 1;
}
