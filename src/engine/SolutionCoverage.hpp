#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace sokoban::solution {

struct CoverageReport {
    std::size_t screens = 0;
    std::size_t replayed = 0;
    std::size_t drafts = 0;
    std::vector<std::string> errors;
    std::vector<std::string> notes;

    [[nodiscard]] bool passed() const { return errors.empty(); }
};

// Every current puzzle requires a matching, passing recording by default.
// coverage.json may exempt a draft only with its current digest and a reason;
// changing or removing that screen makes the exemption fail until reviewed.
// This audit is read-only and never searches, re-records, or repairs content.
[[nodiscard]] CoverageReport auditCoverage(
    const std::filesystem::path& levelsRoot,
    const std::filesystem::path& solutionsDir);

} // namespace sokoban::solution
