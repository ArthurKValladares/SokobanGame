#pragma once

#include <filesystem>
#include <string_view>

namespace sokoban::atomicFile {

// Replaces destination with a fully written same-directory temporary file.
// On platforms that cannot rename over an existing file, the old destination
// is displaced and restored if the second rename fails.
void replace(
    const std::filesystem::path& destination,
    const std::filesystem::path& temporary);

void write(
    const std::filesystem::path& destination,
    std::string_view contents);

} // namespace sokoban::atomicFile
