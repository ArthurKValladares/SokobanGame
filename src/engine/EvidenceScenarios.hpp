#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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

// One key of an evidence run's scripted play: pressed on `frame`, counted
// from zero, and held down for `heldFrames` frames in all.
struct EvidenceMove {
    uint64_t frame = 0;
    // U, D, L or R for a direction, Z for undo.
    char key = 'R';
    uint64_t heldFrames = 1;

    bool operator==(const EvidenceMove&) const = default;
};

// "40:R,55:R*30,120:Z": frame, key, and optionally how many frames the key
// stays down. Nothing for a script that does not parse, or that is empty.
[[nodiscard]] inline std::optional<std::vector<EvidenceMove>> parseEvidenceMoves(
    std::string_view script)
{
    std::vector<EvidenceMove> moves;
    const auto number = [](std::string_view text, uint64_t& value) {
        const auto [end, error] =
            std::from_chars(text.data(), text.data() + text.size(), value);
        return !text.empty() && error == std::errc {} && end == text.data() + text.size();
    };
    while (!script.empty()) {
        const std::size_t comma = script.find(',');
        std::string_view entry = script.substr(0, comma);
        script = comma == std::string_view::npos
            ? std::string_view {} : script.substr(comma + 1);
        const std::size_t colon = entry.find(':');
        if (colon == std::string_view::npos || colon + 1 >= entry.size()) {
            return std::nullopt;
        }
        EvidenceMove move;
        if (!number(entry.substr(0, colon), move.frame)) {
            return std::nullopt;
        }
        move.key = entry[colon + 1];
        if (std::string_view("UDLRZ").find(move.key) == std::string_view::npos) {
            return std::nullopt;
        }
        const std::string_view held = entry.substr(colon + 2);
        if (!held.empty() && (held.front() != '*' ||
                !number(held.substr(1), move.heldFrames) || move.heldFrames == 0)) {
            return std::nullopt;
        }
        moves.push_back(move);
        if (comma != std::string_view::npos && script.empty()) {
            return std::nullopt;
        }
    }
    return moves.empty() ? std::nullopt : std::optional { std::move(moves) };
}

} // namespace sokoban
