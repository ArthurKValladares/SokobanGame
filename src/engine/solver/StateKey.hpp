#pragma once

#include "engine/Rules.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace sokoban::solver::detail {

// Compact, lossless identity for every dynamic field that can affect rules.
// Kept separate from Solver.hpp because this is an implementation detail; it
// is named here so focused tests can guard against fields being omitted as
// GameState evolves.
class PackedStateKey {
public:
    [[nodiscard]] std::size_t wordCount() const { return words_.size(); }

    bool operator==(const PackedStateKey&) const = default;
    [[nodiscard]] bool operator<(const PackedStateKey& other) const
    {
        return words_ < other.words_;
    }

private:
    explicit PackedStateKey(std::vector<std::uint64_t> words)
        : words_(std::move(words))
    {
    }

    std::vector<std::uint64_t> words_;

    friend PackedStateKey makePackedStateKey(
        const GameState&, EntityId);
    friend struct PackedStateKeyHash;
};

struct PackedStateKeyHash {
    [[nodiscard]] std::size_t operator()(const PackedStateKey& key) const;
};

[[nodiscard]] PackedStateKey makePackedStateKey(
    const GameState& state, EntityId activeController);

} // namespace sokoban::solver::detail
