#pragma once

#include <filesystem>
#include <string_view>

namespace sokoban {

// Keep the lock file in place so concurrent openers synchronize on the same
// file. The OS releases the lock if its owner exits.
class FileLock {
public:
    FileLock(const std::filesystem::path& path, std::string_view purpose);
    ~FileLock();

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

private:
#ifdef _WIN32
    void* handle_;
#else
    int handle_;
#endif
};

} // namespace sokoban
