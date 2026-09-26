#include "engine/SourceWatcher.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

namespace sokoban {

void SourceWatcher::watchFile(const std::filesystem::path& path)
{
    const std::filesystem::path normalized = path.lexically_normal();
    if (std::ranges::find(files_, normalized) == files_.end()) {
        files_.push_back(normalized);
        if (primed_) {
            stamps_[normalized] = stampOf(normalized);
        }
    }
}

void SourceWatcher::watchTree(
    const std::filesystem::path& root,
    std::vector<std::string> extensions)
{
    trees_.push_back({ root.lexically_normal(), std::move(extensions) });
    if (primed_) {
        for (const std::filesystem::path& path : currentPaths()) {
            if (!stamps_.contains(path)) {
                stamps_[path] = stampOf(path);
            }
        }
    }
}

void SourceWatcher::setWatchedFiles(
    const std::vector<std::filesystem::path>& paths)
{
    files_.clear();
    for (const std::filesystem::path& path : paths) {
        watchFile(path);
    }
    // Forget stamps nothing watches any more.
    const std::vector<std::filesystem::path> current = currentPaths();
    std::erase_if(stamps_, [&](const auto& entry) {
        return std::ranges::find(current, entry.first) == current.end();
    });
}

SourceWatcher::Stamp SourceWatcher::stampOf(const std::filesystem::path& path)
{
    std::error_code error;
    Stamp stamp;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return stamp;
    }
    stamp.modified = std::filesystem::last_write_time(path, error);
    if (error) {
        return {};
    }
    stamp.size = std::filesystem::file_size(path, error);
    if (error) {
        return {};
    }
    stamp.exists = true;
    return stamp;
}

std::vector<std::filesystem::path> SourceWatcher::currentPaths() const
{
    std::vector<std::filesystem::path> paths = files_;
    for (const Tree& tree : trees_) {
        std::error_code error;
        if (!std::filesystem::is_directory(tree.root, error)) {
            continue;
        }
        for (std::filesystem::recursive_directory_iterator it(
                 tree.root,
                 std::filesystem::directory_options::skip_permission_denied,
                 error);
             !error && it != std::filesystem::recursive_directory_iterator();
             it.increment(error)) {
            if (!it->is_regular_file(error)) {
                continue;
            }
            const std::string extension = it->path().extension().string();
            if (std::ranges::find(tree.extensions, extension) !=
                tree.extensions.end()) {
                paths.push_back(it->path().lexically_normal());
            }
        }
    }
    std::ranges::sort(paths);
    const auto duplicates = std::ranges::unique(paths);
    paths.erase(duplicates.begin(), duplicates.end());
    return paths;
}

std::vector<std::filesystem::path> SourceWatcher::poll()
{
    std::vector<std::filesystem::path> changed;
    for (const std::filesystem::path& path : currentPaths()) {
        const Stamp stamp = stampOf(path);
        const auto found = stamps_.find(path);
        const bool differs = found == stamps_.end()
            ? stamp.exists
            : !(found->second == stamp);
        stamps_[path] = stamp;
        if (primed_ && differs && stamp.exists) {
            changed.push_back(path);
        }
    }
    primed_ = true;
    return changed;
}

} // namespace sokoban
