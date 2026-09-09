#include "ScopedTestDirectory.hpp"
#include "TestAssetRoot.hpp"
#include "TestHarness.hpp"

#include "engine/AnimationCatalog.hpp"
#include "engine/AnimationCatalogEditor.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/ContentPipeline.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void testProductionCatalogIsCompleteAndRoundTrips()
{
    const sokoban::AssetManifest manifest =
        sokoban::AssetManifest::loadFromFile(testAssetRoot() / "manifest.json");
    sokoban::AnimationCatalog catalog =
        sokoban::AnimationCatalog::loadFromFile(
            testAssetRoot() / "animation_catalog.json", manifest);

    CHECK_MESSAGE(
        sokoban::animationUseDefinitions().size() ==
            static_cast<std::size_t>(sokoban::AnimationUse::Count),
        "every enum value has a definition");
    CHECK_MESSAGE(
        catalog.animation(sokoban::AnimationUse::EnemyIdle) ==
            manifest.animationIdByName("RogueIdle"),
        "enemy idle can share the player idle clip");
    CHECK_MESSAGE(
        std::abs(catalog.clipDuration(
            manifest.animationIdByName("BarbarianAttack")) -
            1.3666667f) < 0.0001f,
        "source clip duration is catalogued");
    CHECK_MESSAGE(
        catalog.events(sokoban::AnimationUse::EnemyAttack).size() == 1 &&
            catalog.events(sokoban::AnimationUse::EnemyAttack)[0].id ==
                "attack-connected",
        "enemy attack event is authored");
    CHECK_MESSAGE(
        catalog.startGate(sokoban::AnimationUse::PlayerDeath).has_value() &&
            catalog.startGate(sokoban::AnimationUse::PlayerDeath)->sourceUse ==
                sokoban::AnimationUse::EnemyAttack,
        "player death listens to enemy attack event");

    const auto idle = manifest.animationIdByName("RogueIdle");
    catalog.setGlobalSpeed(idle, 1.5f);
    catalog.setUseSpeed(sokoban::AnimationUse::EnemyIdle, 0.5f);
    CHECK_MESSAGE(
        std::abs(catalog.effectiveSpeed(sokoban::AnimationUse::EnemyIdle) -
                 0.75f) < 0.0001f,
        "effective speed multiplies global and per-use controls");

    const std::string serialized = catalog.serialize(manifest);
    const sokoban::AnimationCatalog reparsed =
        sokoban::AnimationCatalog::parse(serialized, manifest);
    CHECK_MESSAGE(
        std::abs(reparsed.globalSpeed(idle) - 1.5f) < 0.0001f,
        "global speed round trips");
    CHECK_MESSAGE(
        std::abs(reparsed.useSpeed(sokoban::AnimationUse::EnemyIdle) - 0.5f) <
            0.0001f,
        "per-use speed round trips");

    catalog.setTimelineEvent(
        sokoban::AnimationUse::EnemyAttack, "second-impact", 0.75f);
    catalog.setStartGate(
        sokoban::AnimationUse::PlayerDeath,
        sokoban::AnimationCatalog::EventGate {
            .sourceUse = sokoban::AnimationUse::EnemyAttack,
            .eventId = "second-impact",
        });
    CHECK_MESSAGE(
        catalog.events(sokoban::AnimationUse::EnemyAttack).size() == 2,
        "timeline event can be added");
    catalog.updateTimelineEvent(
        sokoban::AnimationUse::EnemyAttack,
        "second-impact",
        "renamed-impact",
        0.8f);
    CHECK_MESSAGE(
        catalog.startGate(sokoban::AnimationUse::PlayerDeath).has_value() &&
            catalog.startGate(sokoban::AnimationUse::PlayerDeath)->eventId ==
                "renamed-impact",
        "renaming an event updates dependent gates transactionally");
    CHECK_MESSAGE(
        std::abs(catalog.eventSourceTime(
            sokoban::AnimationUse::EnemyAttack,
            "renamed-impact") -
            catalog.clipDuration(manifest.animationIdByName(
                "BarbarianAttack")) * 0.8f) < 0.0001f,
        "editing an event updates its timeline position");
    checkThrows(
        [&] {
            catalog.updateTimelineEvent(
                sokoban::AnimationUse::EnemyAttack,
                "renamed-impact",
                "attack-connected",
                0.5f);
        },
        "renaming an event to a duplicate is rejected");
    CHECK_MESSAGE(
        catalog.startGate(sokoban::AnimationUse::PlayerDeath).has_value() &&
            catalog.startGate(sokoban::AnimationUse::PlayerDeath)->eventId ==
                "renamed-impact",
        "failed event rename rolls back dependent gates");
    catalog.removeTimelineEvent(
        sokoban::AnimationUse::EnemyAttack, "renamed-impact");
    CHECK_MESSAGE(
        !catalog.startGate(sokoban::AnimationUse::PlayerDeath).has_value(),
        "removing an event clears dependent gates");

    checkThrows(
        [&] {
            catalog.setStartGate(
                sokoban::AnimationUse::EnemyAttack,
                sokoban::AnimationCatalog::EventGate {
                    .sourceUse = sokoban::AnimationUse::EnemyAttack,
                    .eventId = "attack-connected",
                });
        },
        "cyclic event dependency rejected");
}

void testCatalogRejectsCodeAndManifestDrift()
{
    const sokoban::AssetManifest manifest =
        sokoban::AssetManifest::loadFromFile(testAssetRoot() / "manifest.json");
    const sokoban::AnimationCatalog valid =
        sokoban::AnimationCatalog::loadFromFile(
            testAssetRoot() / "animation_catalog.json", manifest);
    const std::string text = valid.serialize(manifest);

    std::string unknownUse = text;
    const std::string known = "player.idle";
    const std::size_t knownAt = unknownUse.find(known);
    unknownUse.replace(knownAt, known.size(), "player.typo");
    checkThrows(
        [&] { (void)sokoban::AnimationCatalog::parse(unknownUse, manifest); },
        "unknown code use rejected");

    std::string duplicateUse = text;
    const std::string move = "player.move";
    const std::size_t moveAt = duplicateUse.find(move);
    duplicateUse.replace(moveAt, move.size(), known);
    checkThrows(
        [&] { (void)sokoban::AnimationCatalog::parse(duplicateUse, manifest); },
        "duplicate and missing code use rejected");

    std::string unknownAnimation = text;
    const std::string idle = "RogueIdle";
    const std::size_t idleAt = unknownAnimation.find(idle);
    unknownAnimation.replace(idleAt, idle.size(), "MissingClip");
    checkThrows(
        [&] {
            (void)sokoban::AnimationCatalog::parse(unknownAnimation, manifest);
        },
        "unknown manifest animation rejected");
}

void testEditorPersistsSourceAndRuntimeCopies()
{
    const sokoban::AssetManifest manifest =
        sokoban::AssetManifest::loadFromFile(testAssetRoot() / "manifest.json");
    const sokoban::AnimationCatalog initial =
        sokoban::AnimationCatalog::loadFromFile(
            testAssetRoot() / "animation_catalog.json", manifest);
    ScopedTestDirectory temporary("sokoban-animation-catalog");
    const std::filesystem::path source = temporary.path() / "source.json";
    const std::filesystem::path runtimeRoot = temporary.path() / "runtime";
    const std::filesystem::path runtime =
        runtimeRoot / "animation_catalog.json";
    std::filesystem::create_directories(runtimeRoot);
    std::filesystem::copy_file(
        testAssetRoot() / "manifest.json", runtimeRoot / "manifest.json");
    std::ofstream(runtimeRoot / "content.index", std::ios::binary)
        << "format 1\ngame-version editor-test\n";
    {
        std::ofstream stream(source, std::ios::binary);
        stream << initial.serialize(manifest);
    }

    sokoban::AnimationCatalogEditor editor;
    CHECK_MESSAGE(
        editor.initialize(source, runtime, manifest),
        "editor loads source catalog");
    const auto idle = manifest.animationIdByName("RogueIdle");
    editor.setGlobalSpeed(idle, 1.75f);
    editor.setUseSpeed(sokoban::AnimationUse::EnemyIdle, 0.6f);
    editor.setTimelineEvent(
        sokoban::AnimationUse::EnemyAttack, "recovery", 0.95f);
    CHECK_MESSAGE(editor.dirty(), "editing marks catalog dirty");
    CHECK_MESSAGE(editor.save(manifest), "editor saves mirrored catalogs");
    CHECK_MESSAGE(!editor.dirty(), "successful save clears dirty state");
    sokoban::validateContentPackage(runtimeRoot, "editor-test");

    const sokoban::AnimationCatalog savedSource =
        sokoban::AnimationCatalog::loadFromFile(source, manifest);
    const sokoban::AnimationCatalog savedRuntime =
        sokoban::AnimationCatalog::loadFromFile(runtime, manifest);
    CHECK_MESSAGE(
        std::abs(savedSource.globalSpeed(idle) - 1.75f) < 0.0001f,
        "source catalog persisted global speed");
    CHECK_MESSAGE(
        std::abs(savedRuntime.useSpeed(sokoban::AnimationUse::EnemyIdle) -
                 0.6f) < 0.0001f,
        "runtime catalog persisted per-use speed");
    CHECK_MESSAGE(
        savedSource.events(sokoban::AnimationUse::EnemyAttack).size() == 2,
        "source catalog persisted timeline event");
    CHECK_MESSAGE(
        savedRuntime.events(sokoban::AnimationUse::EnemyAttack).size() == 2,
        "runtime catalog persisted timeline event");
}

} // namespace

int main()
{
    testProductionCatalogIsCompleteAndRoundTrips();
    testCatalogRejectsCodeAndManifestDrift();
    testEditorPersistsSourceAndRuntimeCopies();

    if (failures != 0) {
        std::cerr << failures << " animation catalog checks failed\n";
        return 1;
    }
    std::cout << "Animation catalog checks passed\n";
    return 0;
}
