#include "TestHarness.hpp"
#include "ScopedTestDirectory.hpp"

#include "engine/SourceWatcher.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void write(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

// Content edits within one timestamp tick still change the size here; a
// same-size edit needs the clock to move, so the test moves it explicitly.
void touchLater(const std::filesystem::path& path)
{
    std::filesystem::last_write_time(
        path,
        std::filesystem::last_write_time(path) + std::chrono::seconds(2));
}

bool contains(
    const std::vector<std::filesystem::path>& paths,
    const std::filesystem::path& path)
{
    return std::ranges::find(paths, path.lexically_normal()) != paths.end();
}

void testWatchedFilesAndTrees()
{
    TEST("watchedFilesAndTrees");
    ScopedTestDirectory directory;
    const std::filesystem::path root = directory.path();
    const std::filesystem::path manifest = root / "assets" / "manifest.json";
    const std::filesystem::path screen = root / "levels" / "level0" / "screen0.scr";
    const std::filesystem::path notes = root / "levels" / "notes.txt";
    write(manifest, "{}");
    write(screen, "@layer 0\n.\n");
    write(notes, "ignored");

    sokoban::SourceWatcher watcher;
    watcher.watchFile(manifest);
    watcher.watchFile(root / "assets" / "later.png");
    watcher.watchTree(root / "levels", { ".scr", ".json" });
    CHECK(watcher.poll().empty());
    CHECK(watcher.poll().empty());

    write(screen, "@layer 0\n..\n");
    std::vector<std::filesystem::path> changed = watcher.poll();
    CHECK(changed.size() == 1);
    CHECK(contains(changed, screen));
    CHECK(watcher.poll().empty());

    touchLater(manifest);
    CHECK(contains(watcher.poll(), manifest));

    // New files inside a watched tree, and watched files that appear, count.
    const std::filesystem::path added = root / "levels" / "level1" / "screen0.scr";
    write(added, "@layer 0\n.\n");
    write(root / "assets" / "later.png", "png");
    changed = watcher.poll();
    CHECK(contains(changed, added));
    CHECK(contains(changed, root / "assets" / "later.png"));

    // Other extensions and deletions are not reported.
    write(notes, "still ignored, longer");
    std::filesystem::remove(screen);
    CHECK(watcher.poll().empty());

    watcher.setWatchedFiles({ root / "assets" / "later.png" });
    touchLater(manifest);
    CHECK(watcher.poll().empty());
}

} // namespace

int main()
{
    testWatchedFilesAndTrees();
    if (failures == 0) {
        std::cout << "SourceWatcherTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "SourceWatcherTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
