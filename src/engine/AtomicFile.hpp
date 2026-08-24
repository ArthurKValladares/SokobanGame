#pragma once

#include <filesystem>
#include <string_view>

#ifdef SOKOBAN_ENABLE_TEST_HOOKS
#include <cstdint>
#include <system_error>
#endif

namespace sokoban::atomicFile {

// Durably replaces destination with a fully written same-directory temporary
// file. The temporary file is synchronized before installation; the installed
// file and its containing directory are then synchronized as supported by the
// host platform. Callers must close the temporary file before calling this.
void replace(
    const std::filesystem::path& destination,
    const std::filesystem::path& temporary);

void write(
    const std::filesystem::path& destination,
    std::string_view contents);

#ifdef SOKOBAN_ENABLE_TEST_HOOKS
// Causes one later write to fail after `successfulWritesBeforeFailure` writes
// have completed. This test-only seam models OS errors deterministically,
// including permission denial and a full volume.
void failWriteAfterForTesting(
    std::uint32_t successfulWritesBeforeFailure,
    std::errc error);
#endif

} // namespace sokoban::atomicFile
