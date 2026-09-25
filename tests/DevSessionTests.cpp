#include "ScopedTestDirectory.hpp"
#include "TestHarness.hpp"

#include "engine/DevSession.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void testRoundTrip()
{
    TEST("a saved session reads back unchanged");
    ScopedTestDirectory temp("sokoban-dev-session");
    const std::filesystem::path path = temp.path() / "dev-session.json";
    const sokoban::DevSession saved {
        .resumeOnLaunch = false,
        .editorDocument = temp.path() / "levels/level 3/screen2.scr",
        .editingDocument = true,
        .activeLayer = 4,
        .tool = "decorations",
    };
    sokoban::saveDevSession(path, saved);
    const std::optional<sokoban::DevSession> loaded =
        sokoban::loadDevSession(path);
    CHECK(loaded.has_value());
    if (!loaded) {
        return;
    }
    CHECK(!loaded->resumeOnLaunch);
    CHECK(loaded->editorDocument == saved.editorDocument);
    CHECK(loaded->editingDocument);
    CHECK(loaded->activeLayer == 4);
    CHECK(loaded->tool == "decorations");
}

void testUnusableFilesStartNormally()
{
    TEST("missing, damaged, and future files are ignored");
    ScopedTestDirectory temp("sokoban-dev-session");
    CHECK(!sokoban::loadDevSession(temp.path() / "missing.json").has_value());

    const std::filesystem::path damaged = temp.path() / "damaged.json";
    std::ofstream(damaged) << "{ \"format\": 1, ";
    CHECK(!sokoban::loadDevSession(damaged).has_value());

    const std::filesystem::path future = temp.path() / "future.json";
    std::ofstream(future) << "{ \"format\": 2, \"resumeOnLaunch\": true }";
    CHECK(!sokoban::loadDevSession(future).has_value());

    const std::filesystem::path sparse = temp.path() / "sparse.json";
    std::ofstream(sparse) << "{ \"format\": 1 }";
    const auto defaults = sokoban::loadDevSession(sparse);
    CHECK(defaults.has_value() && defaults->resumeOnLaunch &&
        defaults->editorDocument.empty() && defaults->tool == "tiles");
}

} // namespace

int main()
{
    try {
        testRoundTrip();
        testUnusableFilesStartNormally();
    } catch (const std::exception& error) {
        std::cerr << "UNEXPECTED EXCEPTION: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " developer session checks failed\n";
        return 1;
    }
    std::cout << "All " << checks << " developer session checks passed\n";
    return 0;
}
