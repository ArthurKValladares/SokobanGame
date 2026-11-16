#pragma once

#include <string_view>

namespace sokoban {

// Scenario names accepted on the command line. Kept free of engine includes
// so CommandLineOptions.hpp, and the standalone test that parses with it,
// do not pull in the gameplay presentation headers.
[[nodiscard]] constexpr bool isPerformanceEffectScenario(std::string_view name)
{
    return name == "mirror-swap" || name == "witch-swap" ||
        name == "turret-volley" || name == "portals" ||
        name == "special-blocks" || name == "mixed-stress";
}

[[nodiscard]] constexpr bool isEvidenceEffectScenario(std::string_view name)
{
    return isPerformanceEffectScenario(name) || name == "rock-scratches";
}

} // namespace sokoban
