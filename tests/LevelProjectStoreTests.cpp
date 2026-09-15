#include "TestHarness.hpp"

#include "engine/LevelProjectStore.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct TemporaryProject {
    TemporaryProject()
    {
        root = std::filesystem::temp_directory_path() /
            ("sokoban_level_project_store_tests_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        project = root / "project";
        runtime = root / "runtime";
        std::filesystem::create_directories(project / "level0");
        std::filesystem::create_directories(runtime);
        std::filesystem::copy_file(
            std::filesystem::path(SOKOBAN_TEST_SOURCE_DIR) /
                "levels/level0/screen0.scr",
            project / "level0/screen0.scr");
        write(project / "notes.txt", "original-note");
        write(runtime / "old-runtime.txt", "old-runtime");
    }

    ~TemporaryProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    static void write(const std::filesystem::path& path, std::string_view value)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << value;
    }

    static std::string read(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(stream), {} };
    }

    std::filesystem::path root;
    std::filesystem::path project;
    std::filesystem::path runtime;
};

void checkNoWorkingTrees(const TemporaryProject& project)
{
    CHECK(!std::filesystem::exists(project.project.string() + ".editor-stage"));
    CHECK(!std::filesystem::exists(project.project.string() + ".editor-backup"));
    CHECK(!std::filesystem::exists(project.runtime.string() + ".editor-stage"));
    CHECK(!std::filesystem::exists(project.runtime.string() + ".editor-backup"));
}

void testSuccessfulTransactionCommitsProjectAndRuntimeMirror()
{
    TemporaryProject project;
    const auto result = sokoban::LevelProjectStore::transact(
        project.project,
        project.runtime,
        [](const std::filesystem::path& stage) {
            TemporaryProject::write(stage / "notes.txt", "changed-note");
        });

    CHECK(result.succeeded);
    CHECK(result.originalsPreserved);
    CHECK(result.message.empty());
    CHECK(TemporaryProject::read(project.project / "notes.txt") == "changed-note");
    CHECK(std::filesystem::exists(project.runtime / "level0/screen0.scr"));
    CHECK(!std::filesystem::exists(project.runtime / "old-runtime.txt"));
    CHECK(!std::filesystem::exists(project.runtime / "notes.txt"));
    checkNoWorkingTrees(project);
}

void testRejectedMutationPreservesBothOriginalTrees()
{
    TemporaryProject project;
    const std::string originalScreen =
        TemporaryProject::read(project.project / "level0/screen0.scr");
    const auto result = sokoban::LevelProjectStore::transact(
        project.project,
        project.runtime,
        [](const std::filesystem::path& stage) {
            TemporaryProject::write(
                stage / "level0/screen0.scr", "@layer 0\n????\n");
        });

    CHECK(!result.succeeded);
    CHECK(result.originalsPreserved);
    CHECK(!result.message.empty());
    CHECK(TemporaryProject::read(project.project / "level0/screen0.scr") ==
        originalScreen);
    CHECK(TemporaryProject::read(project.project / "notes.txt") ==
        "original-note");
    CHECK(TemporaryProject::read(project.runtime / "old-runtime.txt") ==
        "old-runtime");
    checkNoWorkingTrees(project);
}

void testThrowingMutationIsContainedAndCleanedUp()
{
    TemporaryProject project;
    const auto result = sokoban::LevelProjectStore::transact(
        project.project,
        std::nullopt,
        [](const std::filesystem::path&) {
            throw std::runtime_error("mutation failed deliberately");
        });

    CHECK(!result.succeeded);
    CHECK(result.originalsPreserved);
    CHECK(result.message.find("mutation failed deliberately") !=
        std::string::npos);
    CHECK(TemporaryProject::read(project.project / "notes.txt") ==
        "original-note");
    CHECK(!std::filesystem::exists(
        project.project.string() + ".editor-stage"));
}

void testInterruptedBackupIsRecoveredBeforeNextTransaction()
{
    TemporaryProject project;
    const auto backup = std::filesystem::path(
        project.project.string() + ".editor-backup");
    std::filesystem::rename(project.project, backup);
    std::filesystem::create_directories(
        project.project.string() + ".editor-stage");

    const auto result = sokoban::LevelProjectStore::transact(
        project.project,
        std::nullopt,
        [](const std::filesystem::path&) {});

    CHECK(result.succeeded);
    CHECK(TemporaryProject::read(project.project / "notes.txt") ==
        "original-note");
    CHECK(!std::filesystem::exists(backup));
    CHECK(!std::filesystem::exists(
        project.project.string() + ".editor-stage"));
}

void testRuntimeIndexFailureRollsBackCompanionManifests()
{
    TemporaryProject project;
    const std::filesystem::path runtimeAssets = project.root / "runtime-assets";
    const std::filesystem::path runtimeLevels = runtimeAssets / "levels";
    const std::filesystem::path sourceManifest = project.root / "manifest.json";
    const std::filesystem::path runtimeManifest =
        runtimeAssets / "manifest.json";
    std::filesystem::create_directories(runtimeLevels / "level0");
    std::filesystem::copy_file(
        project.project / "level0/screen0.scr",
        runtimeLevels / "level0/screen0.scr");
    const std::string originalManifest = R"json({
      "format": 1,
      "textures": [{ "name": "GroundSplatMap0_0", "path": "maps/old.png" }],
      "models": [{ "name": "Hero", "path": "hero.glb", "geometry": "skinned", "role": "player" }],
      "animations": [
        { "name": "Idle", "path": "hero.glb", "role": "player-idle" },
        { "name": "Move", "path": "hero.glb", "role": "player-move" },
        { "name": "Push", "path": "hero.glb", "role": "player-push" },
        { "name": "Death", "path": "hero.glb", "role": "player-death" },
        { "name": "DeadIdle", "path": "hero.glb", "role": "player-dead-idle" }
      ]
    })json";
    TemporaryProject::write(sourceManifest, originalManifest);
    TemporaryProject::write(runtimeManifest, originalManifest);
    TemporaryProject::write(
        runtimeAssets / "content.index",
        "format 1\ngame-version transaction-test\n");

    const auto result = sokoban::LevelProjectStore::transact(
        project.project,
        runtimeLevels,
        [](const std::filesystem::path& stage) {
            TemporaryProject::write(stage / "notes.txt", "changed-note");
        },
        sokoban::LevelProjectStore::ManifestTransaction {
            .sourcePath = sourceManifest,
            .runtimePath = runtimeManifest,
            .mutation = [&](const std::filesystem::path& stagedManifest) {
                std::string changed = originalManifest;
                changed.replace(changed.find("maps/old.png"),
                    std::string("maps/old.png").size(), "maps/new.png");
                TemporaryProject::write(stagedManifest, changed);
                std::filesystem::remove(runtimeAssets / "content.index");
                std::filesystem::create_directory(
                    runtimeAssets / "content.index");
                TemporaryProject::write(
                    runtimeAssets / "content.index/obstruction", "x");
            },
        });

    CHECK(!result.succeeded);
    CHECK(result.originalsPreserved);
    CHECK(TemporaryProject::read(project.project / "notes.txt") ==
        "original-note");
    CHECK(TemporaryProject::read(sourceManifest) == originalManifest);
    CHECK(TemporaryProject::read(runtimeManifest) == originalManifest);
    CHECK(std::filesystem::exists(runtimeLevels / "level0/screen0.scr"));
    CHECK(!std::filesystem::exists(
        sourceManifest.string() + ".editor-stage"));
    CHECK(!std::filesystem::exists(
        sourceManifest.string() + ".editor-backup"));
}

} // namespace

int main()
{
    testSuccessfulTransactionCommitsProjectAndRuntimeMirror();
    testRejectedMutationPreservesBothOriginalTrees();
    testThrowingMutationIsContainedAndCleanedUp();
    testInterruptedBackupIsRecoveredBeforeNextTransaction();
    testRuntimeIndexFailureRollsBackCompanionManifests();

    if (failures != 0) {
        std::cerr << "LevelProjectStoreTests: " << failures
                  << " failure(s) of " << checks << " checks\n";
        return 1;
    }
    std::cout << "LevelProjectStoreTests: " << checks << " checks passed\n";
    return 0;
}
