#pragma once

#include "engine/Input.hpp"
#include "engine/InputBindings.hpp"
#include "engine/render/RenderTypes.hpp"
#include "engine/ui/Ui.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace sokoban {

class AssetManifest;

enum class InputPromptTheme {
    Keyboard,
    Generic,
    Xbox,
    PlayStation,
    NintendoSwitch,
    NintendoGameCube,
    SteamDeck,
    Count,
};

struct InputPromptGlyph {
    RenderTexture texture = noTexture;
    UiRect uvRect {};
    float aspectRatio = 1.0f;
};

// Resolves stable SDL bindings to presentation-only Kenney atlas regions.
// Saved bindings remain controller-agnostic; the active SDL gamepad type and
// its reported face-button labels choose what the player sees.
class InputPromptCatalog {
public:
    InputPromptCatalog(
        const std::filesystem::path& assetRoot,
        const AssetManifest& manifest);

    [[nodiscard]] InputPromptTheme themeForGamepad(
        const GamepadPresentation& gamepad) const;
    [[nodiscard]] std::optional<InputPromptGlyph> glyphForBinding(
        const InputBinding& binding,
        const GamepadPresentation& gamepad = {}) const;

private:
    struct Atlas {
        RenderTexture texture = noTexture;
        std::unordered_map<std::string, UiRect> regions;
        std::unordered_map<std::string, float> aspectRatios;
    };

    std::array<Atlas, static_cast<std::size_t>(InputPromptTheme::Count)>
        atlases_;

    [[nodiscard]] const Atlas& atlas(InputPromptTheme theme) const;
    [[nodiscard]] std::optional<InputPromptGlyph> find(
        InputPromptTheme theme,
        std::string_view name) const;
};

void drawInputPromptGlyph(
    UiContext& ui,
    UiRect bounds,
    const InputPromptGlyph& glyph,
    Vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f });

} // namespace sokoban
