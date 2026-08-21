#pragma once

#include "engine/InputBindings.hpp"
#include "engine/Math.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace sokoban {

class UiContext;

// Compact, wordless contextual prompt: a key/button cap above a geometric
// down arrow. The arrow tip is the world-projected point above the actor.
class SelectorPrompt {
public:
    [[nodiscard]] static std::optional<std::string> bindingLabel(
        const InputBindings& bindings,
        BindingDeviceClass activeDevice);

    static void draw(
        UiContext& ui,
        Vec2 arrowTip,
        std::string_view bindingLabel);
};

} // namespace sokoban
