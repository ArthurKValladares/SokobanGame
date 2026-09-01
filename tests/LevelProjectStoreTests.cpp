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

} // namespace

int main()
{
    testSuccessfulTransactionCommitsProjectAndRuntimeMirror();
    testRejectedMutationPreservesBothOriginalTrees();
    testThrowingMutationIsContainedAndCleanedUp();
    testInterruptedBackupIsRecoveredBeforeNextTransaction();

    if (failures != 0) {
        std::cerr << "LevelProjectStoreTests: " << failures
                  << " failure(s) of " << checks << " checks\n";
        return 1;
    }
    std::cout << "LevelProjectStoreTests: " << checks << " checks passed\n";
    return 0;
}
