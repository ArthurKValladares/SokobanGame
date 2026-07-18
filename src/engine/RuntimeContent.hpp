#pragma once

#include <filesystem>

namespace sokoban {

// Returns the validated read-only content root installed beside the current
// executable. SDL must be initialized before this is called.
[[nodiscard]] std::filesystem::path runtimeContentRoot();

} // namespace sokoban
