#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace sokoban::crash {

// Installs the platform's best-effort unhandled-crash reporter. Reports are
// intentionally local and live beside player data, where support can request
// them without requiring write access to the install directory.
void install(const std::filesystem::path& directory) noexcept;

// Captures the current process for a fatal error that was caught at the top
// level. On Windows this writes a minidump; unsupported platforms return none.
[[nodiscard]] std::optional<std::filesystem::path> writeMinidump() noexcept;

[[nodiscard]] std::string fatalErrorMessage(
    std::string_view cause,
    const std::filesystem::path& logPath,
    const std::optional<std::filesystem::path>& dumpPath);

// Must be safe to invoke after video teardown, including constructor failures.
void showFatalErrorDialog(
    std::string_view cause,
    const std::filesystem::path& logPath,
    const std::optional<std::filesystem::path>& dumpPath) noexcept;

} // namespace sokoban::crash
