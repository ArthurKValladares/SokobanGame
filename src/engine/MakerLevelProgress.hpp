#pragma once

#include "engine/OverworldMap.hpp"

#include <algorithm>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

// Progress belongs to one overworld playtest. Multiple selectors for the
// same saved level share its completion, while each flag has its own links.
class MakerLevelProgress {
public:
    [[nodiscard]] static const OverworldSelectorRuntime* findSelector(
        std::span<const OverworldSelectorRuntime> selectors,
        uint32_t runtimeId)
    {
        // Composition emits selectors in ascending runtime ID order.
        const auto found = std::ranges::lower_bound(
            selectors, runtimeId, {}, &OverworldSelectorRuntime::runtimeId);
        return found != selectors.end() && found->runtimeId == runtimeId
            ? &*found
            : nullptr;
    }

    [[nodiscard]] std::optional<uint32_t> unsolvedPrerequisite(
        const OverworldSelectorRuntime& selector,
        std::span<const OverworldSelectorRuntime> selectors,
        const std::function<std::optional<std::string_view>(
            const OverworldSelectorRuntime&)>& resolve = {}) const
    {
        for (uint32_t id : selector.prerequisites) {
            const auto* prerequisite = findSelector(selectors, id);
            if (!prerequisite) return id;
            const std::optional<std::string_view> key = resolve
                ? resolve(*prerequisite)
                : (prerequisite->draftId ? prerequisite->draftId
                                         : prerequisite->draftName);
            if (!key || !solved(*key)) return id;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool solved(std::string_view name) const
    {
        return std::ranges::find(completedLevels_, name) !=
            completedLevels_.end();
    }

    void recordCompletion(std::string_view name)
    {
        if (!solved(name)) completedLevels_.emplace_back(name);
    }

private:
    std::vector<std::string> completedLevels_;
};

} // namespace sokoban
