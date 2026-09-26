#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace sokoban {

// Polls source files for edits made outside the game: a level saved from a
// text editor, a texture re-exported from an image editor, a manifest field
// changed by hand. Debug developer builds use it to reload those without a
// rebuild and restart (see Application::serviceSourceWatcher).
//
// Polling rather than OS notifications: it is portable, the watched set is a
// few hundred files, and a stat every half second is cheap. A file counts as
// changed when its size or modification time differs from the last poll.
class SourceWatcher {
public:
    // Watches one file (it may not exist yet).
    void watchFile(const std::filesystem::path& path);
    // Watches every regular file under `root` whose extension is listed
    // (".scr", ".json"), including files created later.
    void watchTree(
        const std::filesystem::path& root,
        std::vector<std::string> extensions);
    // Replaces the set of individually watched files, keeping the stamps of
    // files that stay watched.
    void setWatchedFiles(const std::vector<std::filesystem::path>& paths);

    // Files created or modified since the previous poll. The first poll only
    // records stamps. Deleted files are not reported.
    [[nodiscard]] std::vector<std::filesystem::path> poll();

    [[nodiscard]] std::size_t watchedCount() const { return stamps_.size(); }

private:
    struct Stamp {
        std::filesystem::file_time_type modified {};
        std::uintmax_t size = 0;
        bool exists = false;

        bool operator==(const Stamp&) const = default;
    };
    struct Tree {
        std::filesystem::path root;
        std::vector<std::string> extensions;
    };

    [[nodiscard]] static Stamp stampOf(const std::filesystem::path& path);
    [[nodiscard]] std::vector<std::filesystem::path> currentPaths() const;

    std::vector<std::filesystem::path> files_;
    std::vector<Tree> trees_;
    std::map<std::filesystem::path, Stamp> stamps_;
    bool primed_ = false;
};

} // namespace sokoban
