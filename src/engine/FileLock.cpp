#include "engine/FileLock.hpp"

#include "engine/PathText.hpp"

#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace sokoban {

FileLock::FileLock(const std::filesystem::path& path, std::string_view purpose)
{
    const std::string name(purpose);
    if (std::filesystem::is_symlink(path)) {
        throw std::runtime_error("invalid " + name + " lock: " + pathText(path));
    }
    std::filesystem::create_directories(path.parent_path());
#ifdef _WIN32
    handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        throw std::system_error(static_cast<int>(GetLastError()),
            std::system_category(), "cannot open " + name + " lock");
    }
    OVERLAPPED offset {};
    if (!LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &offset)) {
        const auto error = GetLastError();
        CloseHandle(handle_);
        throw std::system_error(static_cast<int>(error),
            std::system_category(), "cannot lock " + name);
    }
#else
    handle_ = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (handle_ == -1) {
        throw std::system_error(errno, std::generic_category(),
            "cannot open " + name + " lock");
    }
    int result;
    do {
        result = flock(handle_, LOCK_EX);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
        const int error = errno;
        close(handle_);
        throw std::system_error(error, std::generic_category(), "cannot lock " + name);
    }
#endif
}

FileLock::~FileLock()
{
#ifdef _WIN32
    CloseHandle(handle_);
#else
    close(handle_);
#endif
}

} // namespace sokoban
