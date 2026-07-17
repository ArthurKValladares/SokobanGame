// Headless tests for level/frame render asset planning.

#include "engine/Level.hpp"
#include "engine/render/RenderAssetRequirements.hpp"

#include <iostream>
#include <stdexcept>

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
        std::cerr << "FAIL [" << currentTest << "] line "
                  << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

void testLevelRequirementsIncludeDynamicAndStaticAssets()
{
    TEST("levelRequirementsIncludeDynamicAndStaticAssets");
    const Level level = Level::loadFromLayers({
        { "......." },
        { "C#W>RI " },
    }, "asset requirements");

    const RenderAssetRequirements requirements =
        renderAssetRequirementsForLevel(level);
    CHECK(requirements.contains(RenderModel::Rogue));
    CHECK(requirements.contains(RenderModel::BricksA));
    CHECK(requirements.contains(RenderModel::Water));
    CHECK(requirements.contains(RenderModel::Conveyor));
    CHECK(requirements.contains(RenderModel::Stone));
    CHECK(requirements.contains(RenderModel::Glass));
    CHECK(!requirements.contains(RenderModel::Cube));
    CHECK(requirements.contains(RenderAnimation::RogueIdle));
    CHECK(requirements.contains(RenderAnimation::RogueMovement));
    CHECK(requirements.contains(RenderAnimation::RoguePush));
    CHECK(requirements.modelCount() == 6);
    CHECK(requirements.animationCount() == 3);
}

void testFrameRequirementsOnlyContainReferencedAssets()
{
    TEST("frameRequirementsOnlyContainReferencedAssets");
    RenderFrameData frame;
    frame.tiles = {
        RenderFrameData::Tile { .model = RenderModel::Cube },
        RenderFrameData::Tile { .model = RenderModel::Stone },
        RenderFrameData::Tile {
            .model = RenderModel::Rogue,
            .animation = RenderAnimation::RogueMovement,
        },
    };

    const RenderAssetRequirements requirements =
        renderAssetRequirementsForFrame(frame);
    CHECK(requirements.contains(RenderModel::Stone));
    CHECK(requirements.contains(RenderModel::Rogue));
    CHECK(!requirements.contains(RenderModel::Water));
    CHECK(requirements.contains(RenderAnimation::RogueMovement));
    CHECK(!requirements.contains(RenderAnimation::RogueIdle));
    CHECK(requirements.modelCount() == 2);
    CHECK(requirements.animationCount() == 1);
}

void testMergeDeduplicatesRequirements()
{
    TEST("mergeDeduplicatesRequirements");
    RenderAssetRequirements first;
    first.requireModel(RenderModel::Stone);
    first.requireAnimation(RenderAnimation::RogueIdle);

    RenderAssetRequirements second;
    second.requireModel(RenderModel::Stone);
    second.requireModel(RenderModel::Water);
    second.requireAnimation(RenderAnimation::RoguePush);
    first.merge(second);

    CHECK(first.modelCount() == 2);
    CHECK(first.animationCount() == 2);
    CHECK(first.contains(RenderModel::Water));
    CHECK(first.contains(RenderAnimation::RoguePush));
}

void testProceduralAndSentinelSemantics()
{
    TEST("proceduralAndSentinelSemantics");
    RenderAssetRequirements requirements;
    requirements.requireModel(RenderModel::Cube);
    requirements.requireAnimation(RenderAnimation::None);
    CHECK(requirements.empty());

    bool modelThrew = false;
    try {
        requirements.requireModel(RenderModel::Count);
    } catch (const std::invalid_argument&) {
        modelThrew = true;
    }
    CHECK(modelThrew);

    bool animationThrew = false;
    try {
        requirements.requireAnimation(RenderAnimation::Count);
    } catch (const std::invalid_argument&) {
        animationThrew = true;
    }
    CHECK(animationThrew);
}

} // namespace

int main()
{
    testLevelRequirementsIncludeDynamicAndStaticAssets();
    testFrameRequirementsOnlyContainReferencedAssets();
    testMergeDeduplicatesRequirements();
    testProceduralAndSentinelSemantics();

    if (failures == 0) {
        std::cout << "AssetRequirementsTests: "
                  << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "AssetRequirementsTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
