#include "engine/CrashDiagnostics.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <string>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#endif

namespace sokoban::crash {
namespace {

std::filesystem::path dumpDirectory;

#ifdef _WIN32
std::filesystem::path unhandledDumpPath;

[[nodiscard]] bool writeDump(
    const std::filesystem::path& path,
    EXCEPTION_POINTERS* exceptionPointers) noexcept
{
    const HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    MINIDUMP_EXCEPTION_INFORMATION exceptionInformation {};
    exceptionInformation.ThreadId = GetCurrentThreadId();
    exceptionInformation.ExceptionPointers = exceptionPointers;
    exceptionInformation.ClientPointers = FALSE;
    const BOOL written = MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        file,
        static_cast<MINIDUMP_TYPE>(
            MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
        exceptionPointers ? &exceptionInformation : nullptr,
        nullptr,
        nullptr);
    CloseHandle(file);
    return written != FALSE;
}

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionPointers)
{
    if (!unhandledDumpPath.empty()) {
        (void)writeDump(unhandledDumpPath, exceptionPointers);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void terminateHandler()
{
    if (!unhandledDumpPath.empty()) {
        (void)writeDump(unhandledDumpPath, nullptr);
    }
    std::abort();
}
#endif

} // namespace

void install(const std::filesystem::path& directory) noexcept
{
    try {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return;
        }
        dumpDirectory = directory;
#ifdef _WIN32
        unhandledDumpPath = dumpDirectory / "unhandled.dmp";
        SetUnhandledExceptionFilter(unhandledExceptionFilter);
        std::set_terminate(terminateHandler);
#endif
    } catch (...) {
        // Crash reporting must never make startup less reliable.
    }
}

std::optional<std::filesystem::path> writeMinidump() noexcept
{
#ifdef _WIN32
    try {
        if (dumpDirectory.empty()) {
            return std::nullopt;
        }
        const auto milliseconds = std::chrono::duration_cast<
            std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const std::filesystem::path path = dumpDirectory /
            ("fatal-" + std::to_string(GetCurrentProcessId()) + '-' +
                std::to_string(milliseconds) + ".dmp");
        return writeDump(path, nullptr) ? std::optional(path) : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
#else
    return std::nullopt;
#endif
}

std::string fatalErrorMessage(
    std::string_view cause,
    const std::filesystem::path& logPath,
    const std::optional<std::filesystem::path>& dumpPath)
{
    std::string message = "Sokoban 3D encountered a fatal error and must close.\n\n";
    message += cause.empty() ? "No additional error detail was available." : cause;
    if (!logPath.empty()) {
        message += "\n\nDiagnostic log:\n" + logPath.string();
    }
    if (dumpPath) {
        message += "\n\nCrash dump:\n" + dumpPath->string();
    }
    message += "\n\nPlease restart the game. If this repeats, include these files in a support report.";
    return message;
}

void showFatalErrorDialog(
    std::string_view cause,
    const std::filesystem::path& logPath,
    const std::optional<std::filesystem::path>& dumpPath) noexcept
{
    try {
        const std::string message = fatalErrorMessage(cause, logPath, dumpPath);
#ifdef _WIN32
        (void)MessageBoxA(
            nullptr, message.c_str(), "Sokoban 3D - Fatal error",
            MB_OK | MB_ICONERROR | MB_TASKMODAL);
#else
        (void)SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Sokoban 3D - Fatal error",
            message.c_str(), nullptr);
#endif
    } catch (...) {
    }
}

} // namespace sokoban::crash
