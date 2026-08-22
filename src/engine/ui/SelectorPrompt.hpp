#pragma once

#include "engine/InputBindings.hpp"
#include "engine/Math.hpp"
#include "engine/ui/InputPrompts.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace sokoban {

class UiContext;
struct UiRect;

// Compact, wordless contextual prompt: a key/button cap above a geometric
// down arrow. The arrow tip is the world-projected point above the actor.
class SelectorPrompt {
public:
    [[nodiscard]] static std::optional<InputBinding> binding(
        const InputBindings& bindings,
        InputAction action,
        BindingDeviceClass activeDevice);
    [[nodiscard]] static std::optional<std::string> bindingLabel(
        const InputBindings& bindings,
        InputAction action,
        BindingDeviceClass activeDevice);

    static void draw(
        UiContext& ui,
        Vec2 arrowTip,
        std::string_view enterBindingLabel,
        std::string_view previewBindingLabel);
    static void draw(
        UiContext& ui,
        Vec2 arrowTip,
        const InputPromptGlyph& enterBinding,
        const InputPromptGlyph& previewBinding);
};

// The preview scene itself is rendered by Vulkan. This UI layer supplies the
// proportional inset shared with the renderer and softens its edge after the
// scene has been composited.
class ScreenPreviewOverlay {
public:
    static constexpr float scale = 0.75f;

    [[nodiscard]] static UiRect previewRect(Vec2 viewport);
    static void draw(UiContext& ui, Vec2 viewport);
};

} // namespace sokoban
