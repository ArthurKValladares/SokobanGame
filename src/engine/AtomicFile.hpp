#pragma once

#include <filesystem>
#include <string_view>

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

} // namespace sokoban::atomicFile
