#include "engine/AtomicFile.hpp"

#include <cerrno>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sokoban::atomicFile {
namespace {

#ifdef SOKOBAN_ENABLE_TEST_HOOKS
struct WriteFailureForTesting {
    std::uint32_t successfulWritesBeforeFailure = 0;
    std::errc error {};
};

std::mutex writeFailureMutex;
std::optional<WriteFailureForTesting> writeFailureForTesting;

void maybeFailWriteForTesting(const std::filesystem::path& path)
{
    std::optional<std::errc> failure;
    {
        const std::scoped_lock lock(writeFailureMutex);
        if (!writeFailureForTesting) {
            return;
        }
        if (writeFailureForTesting->successfulWritesBeforeFailure != 0) {
            --writeFailureForTesting->successfulWritesBeforeFailure;
            return;
        }
        failure = writeFailureForTesting->error;
        writeFailureForTesting.reset();
    }
    throw std::system_error(
        std::make_error_code(*failure),
        "test-injected write failure " + path.string());
}
#else
void maybeFailWriteForTesting(const std::filesystem::path&)
{
}
#endif

#ifdef _WIN32

[[noreturn]] void throwWindowsError(
    std::string_view operation,
    const std::filesystem::path& path)
{
    throw std::system_error(
        static_cast<int>(GetLastError()),
        std::system_category(),
        std::string(operation) + " " + path.string());
}

void syncFile(const std::filesystem::path& path)
{
    HANDLE handle = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throwWindowsError("cannot open for synchronization", path);
    }

    if (!FlushFileBuffers(handle)) {
        const DWORD error = GetLastError();
        CloseHandle(handle);
        throw std::system_error(
            static_cast<int>(error),
            std::system_category(),
            "cannot synchronize " + path.string());
    }
    if (!CloseHandle(handle)) {
        throwWindowsError("cannot close synchronized file", path);
    }
}

void installReplacement(
    const std::filesystem::path& destination,
    const std::filesystem::path& temporary)
{
    if (!MoveFileExW(
            temporary.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throwWindowsError("cannot replace", destination);
    }
}

#else

[[noreturn]] void throwErrno(
    std::string_view operation,
    const std::filesystem::path& path,
    int error)
{
    throw std::system_error(
        error,
        std::generic_category(),
        std::string(operation) + " " + path.string());
}

void syncDescriptor(
    int descriptor,
    std::string_view description,
    const std::filesystem::path& path)
{
    if (fsync(descriptor) != 0) {
        const int error = errno;
        close(descriptor);
        throwErrno("cannot synchronize " + std::string(description), path, error);
    }
    if (close(descriptor) != 0) {
        throwErrno("cannot close " + std::string(description), path, errno);
    }
}

void syncFile(const std::filesystem::path& path)
{
    const int descriptor = open(path.c_str(), O_RDONLY);
    if (descriptor < 0) {
        throwErrno("cannot open for synchronization", path, errno);
    }
    syncDescriptor(descriptor, "file", path);
}

void syncDirectory(const std::filesystem::path& directory)
{
    const int descriptor = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    if (descriptor < 0) {
        throwErrno("cannot open directory for synchronization", directory, errno);
    }
    syncDescriptor(descriptor, "directory", directory);
}

void installReplacement(
    const std::filesystem::path& destination,
    const std::filesystem::path& temporary)
{
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        throw std::system_error(
            error,
            "cannot replace " + destination.string());
    }
}

#endif

std::filesystem::path containingDirectory(const std::filesystem::path& path)
{
    const std::filesystem::path directory = path.parent_path();
    return directory.empty() ? std::filesystem::path(".") : directory;
}

} // namespace

void replace(
    const std::filesystem::path& destination,
    const std::filesystem::path& temporary)
{
    syncFile(temporary);
    installReplacement(destination, temporary);
#ifdef _WIN32
    // MOVEFILE_WRITE_THROUGH makes the replacement itself synchronous. Flush
    // the newly named file as well so its data and metadata are acknowledged
    // before the save reports success.
    syncFile(destination);
#else
    syncDirectory(containingDirectory(destination));
#endif
}

void write(
    const std::filesystem::path& destination,
    std::string_view contents)
{
    const std::filesystem::path temporary = destination.string() + ".tmp";
    std::error_code cleanupError;
    std::filesystem::remove(temporary, cleanupError);
    if (cleanupError) {
        throw std::system_error(
            cleanupError,
            "cannot remove stale temporary file " + temporary.string());
    }

    try {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot open " + temporary.string());
        }
        maybeFailWriteForTesting(temporary);
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        stream.flush();
        if (!stream) {
            throw std::runtime_error("cannot flush " + temporary.string());
        }
        stream.close();
        if (!stream) {
            throw std::runtime_error("cannot close " + temporary.string());
        }
        replace(destination, temporary);
    } catch (...) {
        std::filesystem::remove(temporary, cleanupError);
        throw;
    }
}

#ifdef SOKOBAN_ENABLE_TEST_HOOKS
void failWriteAfterForTesting(
    std::uint32_t successfulWritesBeforeFailure,
    std::errc error)
{
    const std::scoped_lock lock(writeFailureMutex);
    writeFailureForTesting = WriteFailureForTesting {
        .successfulWritesBeforeFailure = successfulWritesBeforeFailure,
        .error = error,
    };
}
#endif

} // namespace sokoban::atomicFile
