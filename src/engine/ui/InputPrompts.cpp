#include "engine/ui/InputPrompts.hpp"

#include "engine/AssetManifest.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace sokoban {
namespace {

struct AtlasDefinition {
    InputPromptTheme theme;
    std::string_view textureName;
    std::string_view xmlPath;
};

constexpr std::array atlasDefinitions {
    AtlasDefinition { InputPromptTheme::Keyboard, "InputPromptsKeyboard",
        "kenney_input-prompts_1.5/Keyboard & Mouse/keyboard-&-mouse_sheet_default.xml" },
    AtlasDefinition { InputPromptTheme::Generic, "InputPromptsGeneric",
        "kenney_input-prompts_1.5/Generic/generic_sheet_default.xml" },
    AtlasDefinition { InputPromptTheme::Xbox, "InputPromptsXbox",
        "kenney_input-prompts_1.5/Xbox Series/xbox-series_sheet_default.xml" },
    AtlasDefinition { InputPromptTheme::PlayStation, "InputPromptsPlayStation",
        "kenney_input-prompts_1.5/PlayStation Series/playstation-series_sheet_default.xml" },
    AtlasDefinition { InputPromptTheme::NintendoSwitch, "InputPromptsSwitch",
        "kenney_input-prompts_1.5/Nintendo Switch/nintendo-switch_sheet_default.xml" },
    AtlasDefinition { InputPromptTheme::NintendoGameCube, "InputPromptsGameCube",
        "kenney_input-prompts_1.5/Nintendo Gamecube/nintendo-gamecube_sheet_default.xml" },
    AtlasDefinition { InputPromptTheme::SteamDeck, "InputPromptsSteamDeck",
        "kenney_input-prompts_1.5/Steam Deck/steam-deck_sheet_default.xml" },
};

std::optional<std::string> attribute(std::string_view line, std::string_view name)
{
    const std::string token = std::string(name) + "=\"";
    const std::size_t begin = line.find(token);
    if (begin == std::string_view::npos) return std::nullopt;
    const std::size_t valueBegin = begin + token.size();
    const std::size_t end = line.find('"', valueBegin);
    if (end == std::string_view::npos) return std::nullopt;
    return std::string(line.substr(valueBegin, end - valueBegin));
}

struct PixelRegion {
    std::string name;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

std::vector<PixelRegion> readRegions(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Could not open input prompt atlas: " + path.string());
    }
    std::vector<PixelRegion> result;
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("<SubTexture") == std::string::npos) continue;
        const auto name = attribute(line, "name");
        const auto x = attribute(line, "x");
        const auto y = attribute(line, "y");
        const auto width = attribute(line, "width");
        const auto height = attribute(line, "height");
        if (!name || !x || !y || !width || !height) {
            throw std::runtime_error("Malformed input prompt atlas entry: " + path.string());
        }
        result.push_back({
            .name = *name,
            .x = std::stoi(*x),
            .y = std::stoi(*y),
            .width = std::stoi(*width),
            .height = std::stoi(*height),
        });
    }
    if (result.empty()) {
        throw std::runtime_error("Input prompt atlas contains no regions: " + path.string());
    }
    return result;
}

std::string lowercase(std::string_view value)
{
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::string keyboardIconName(std::string_view scancode)
{
    std::string name = lowercase(scancode);
    std::ranges::replace(name, ' ', '_');
    if (name == "up") name = "arrow_up";
    else if (name == "down") name = "arrow_down";
    else if (name == "left") name = "arrow_left";
    else if (name == "right") name = "arrow_right";
    else if (name == "left_shift" || name == "right_shift") name = "shift";
    else if (name == "left_ctrl" || name == "right_ctrl") name = "ctrl";
    else if (name == "left_alt" || name == "right_alt") name = "alt";
    else if (name == "return2") name = "return";
    return "keyboard_" + name;
}

std::string faceSuffix(SDL_GamepadButtonLabel label)
{
    switch (label) {
    case SDL_GAMEPAD_BUTTON_LABEL_A: return "a";
    case SDL_GAMEPAD_BUTTON_LABEL_B: return "b";
    case SDL_GAMEPAD_BUTTON_LABEL_X: return "x";
    case SDL_GAMEPAD_BUTTON_LABEL_Y: return "y";
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "cross";
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "circle";
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "square";
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "triangle";
    default: return {};
    }
}

int faceIndex(std::string_view button)
{
    if (button == "south") return 0;
    if (button == "east") return 1;
    if (button == "west") return 2;
    if (button == "north") return 3;
    return -1;
}

std::string gamepadButtonIconName(
    std::string_view button,
    InputPromptTheme theme,
    const GamepadPresentation& gamepad)
{
    const int index = faceIndex(button);
    const std::string face = index >= 0
        ? faceSuffix(gamepad.faceButtonLabels[static_cast<std::size_t>(index)])
        : std::string {};
    switch (theme) {
    case InputPromptTheme::Xbox: {
        if (index >= 0) return "xbox_button_" + (face.empty() ? std::array { "a", "b", "x", "y" }[index] : face);
        if (button == "dpup") return "xbox_dpad_up";
        if (button == "dpdown") return "xbox_dpad_down";
        if (button == "dpleft") return "xbox_dpad_left";
        if (button == "dpright") return "xbox_dpad_right";
        if (button == "leftshoulder") return "xbox_lb";
        if (button == "rightshoulder") return "xbox_rb";
        if (button == "leftstick") return "xbox_stick_l_press";
        if (button == "rightstick") return "xbox_stick_r_press";
        if (button == "back") return "xbox_button_view";
        if (button == "start") return "xbox_button_menu";
        if (button == "guide") return "xbox_guide";
        break;
    }
    case InputPromptTheme::PlayStation: {
        static constexpr std::array fallback { "cross", "circle", "square", "triangle" };
        if (index >= 0) return "playstation_button_" + (face.empty() ? fallback[index] : face);
        if (button == "dpup") return "playstation_dpad_up";
        if (button == "dpdown") return "playstation_dpad_down";
        if (button == "dpleft") return "playstation_dpad_left";
        if (button == "dpright") return "playstation_dpad_right";
        if (button == "leftshoulder") return "playstation_trigger_l1";
        if (button == "rightshoulder") return "playstation_trigger_r1";
        if (button == "leftstick") return "playstation_button_l3";
        if (button == "rightstick") return "playstation_button_r3";
        if (button == "start") return gamepad.type == SDL_GAMEPAD_TYPE_PS5
            ? "playstation5_button_options" : "playstation4_button_options";
        if (button == "back") return gamepad.type == SDL_GAMEPAD_TYPE_PS5
            ? "playstation5_button_create" : "playstation4_button_share";
        break;
    }
    case InputPromptTheme::NintendoSwitch: {
        static constexpr std::array fallback { "b", "a", "y", "x" };
        if (index >= 0) return "switch_button_" + (face.empty() ? fallback[index] : face);
        if (button == "dpup") return "switch_dpad_up";
        if (button == "dpdown") return "switch_dpad_down";
        if (button == "dpleft") return "switch_dpad_left";
        if (button == "dpright") return "switch_dpad_right";
        if (button == "leftshoulder") return "switch_button_l";
        if (button == "rightshoulder") return "switch_button_r";
        if (button == "leftstick") return "switch_stick_l_press";
        if (button == "rightstick") return "switch_stick_r_press";
        if (button == "back") return "switch_button_minus";
        if (button == "start") return "switch_button_plus";
        if (button == "guide") return "switch_button_home";
        break;
    }
    case InputPromptTheme::NintendoGameCube: {
        static constexpr std::array fallback { "a", "x", "b", "y" };
        if (index >= 0) return "gamecube_button_" + (face.empty() ? fallback[index] : face);
        if (button == "dpup") return "gamecube_dpad_up";
        if (button == "dpdown") return "gamecube_dpad_down";
        if (button == "dpleft") return "gamecube_dpad_left";
        if (button == "dpright") return "gamecube_dpad_right";
        if (button == "leftshoulder") return "gamecube_trigger_l";
        if (button == "rightshoulder") return "gamecube_trigger_r";
        if (button == "start") return "gamecube_button_start";
        break;
    }
    case InputPromptTheme::SteamDeck: {
        static constexpr std::array fallback { "a", "b", "x", "y" };
        if (index >= 0) return "steamdeck_button_" + (face.empty() ? fallback[index] : face);
        if (button == "dpup") return "steamdeck_dpad_up";
        if (button == "dpdown") return "steamdeck_dpad_down";
        if (button == "dpleft") return "steamdeck_dpad_left";
        if (button == "dpright") return "steamdeck_dpad_right";
        if (button == "leftshoulder") return "steamdeck_button_l1";
        if (button == "rightshoulder") return "steamdeck_button_r1";
        if (button == "leftstick") return "steamdeck_stick_l_press";
        if (button == "rightstick") return "steamdeck_stick_r_press";
        if (button == "back") return "steamdeck_button_view";
        if (button == "start") return "steamdeck_button_options";
        if (button == "guide") return "steamdeck_button_guide";
        break;
    }
    case InputPromptTheme::Generic:
        return index >= 0 ? "generic_button_circle" : "generic_button";
    default: break;
    }
    return {};
}

std::string gamepadAxisIconName(
    const GamepadAxisBinding& axis,
    InputPromptTheme theme)
{
    if (theme == InputPromptTheme::Generic) {
        if (axis.axis == "leftx" || axis.axis == "rightx") {
            return axis.direction == AxisDirection::Negative
                ? "generic_stick_left" : "generic_stick_right";
        }
        if (axis.axis == "lefty" || axis.axis == "righty") {
            return axis.direction == AxisDirection::Negative
                ? "generic_stick_up" : "generic_stick_down";
        }
        return "generic_stick";
    }
    if (theme == InputPromptTheme::NintendoGameCube) {
        const std::string stick = axis.axis.starts_with("right")
            ? "gamecube_stick_c_" : "gamecube_stick_";
        if (axis.axis.ends_with('x')) return stick +
            (axis.direction == AxisDirection::Negative ? "left" : "right");
        if (axis.axis.ends_with('y')) return stick +
            (axis.direction == AxisDirection::Negative ? "up" : "down");
    }
    std::string prefix;
    switch (theme) {
    case InputPromptTheme::Xbox: prefix = "xbox_stick_"; break;
    case InputPromptTheme::PlayStation: prefix = "playstation_stick_"; break;
    case InputPromptTheme::NintendoSwitch: prefix = "switch_stick_"; break;
    case InputPromptTheme::SteamDeck: prefix = "steamdeck_stick_"; break;
    case InputPromptTheme::NintendoGameCube:
    case InputPromptTheme::Generic: break;
    default: return {};
    }
    if (axis.axis == "leftx") return prefix + "l_" +
        (axis.direction == AxisDirection::Negative ? "left" : "right");
    if (axis.axis == "lefty") return prefix + "l_" +
        (axis.direction == AxisDirection::Negative ? "up" : "down");
    if (axis.axis == "rightx") return prefix + "r_" +
        (axis.direction == AxisDirection::Negative ? "left" : "right");
    if (axis.axis == "righty") return prefix + "r_" +
        (axis.direction == AxisDirection::Negative ? "up" : "down");
    if (axis.axis == "lefttrigger") {
        if (theme == InputPromptTheme::Xbox) return "xbox_lt";
        if (theme == InputPromptTheme::PlayStation) return "playstation_trigger_l2";
        if (theme == InputPromptTheme::NintendoSwitch) return "switch_button_zl";
        if (theme == InputPromptTheme::SteamDeck) return "steamdeck_button_l2";
        if (theme == InputPromptTheme::NintendoGameCube) return "gamecube_trigger_l";
    }
    if (axis.axis == "righttrigger") {
        if (theme == InputPromptTheme::Xbox) return "xbox_rt";
        if (theme == InputPromptTheme::PlayStation) return "playstation_trigger_r2";
        if (theme == InputPromptTheme::NintendoSwitch) return "switch_button_zr";
        if (theme == InputPromptTheme::SteamDeck) return "steamdeck_button_r2";
        if (theme == InputPromptTheme::NintendoGameCube) return "gamecube_trigger_r";
    }
    return theme == InputPromptTheme::Generic ? "generic_stick" : std::string {};
}

} // namespace

InputPromptCatalog::InputPromptCatalog(
    const std::filesystem::path& assetRoot,
    const AssetManifest& manifest)
{
    for (const AtlasDefinition& definition : atlasDefinitions) {
        Atlas& target = atlases_[static_cast<std::size_t>(definition.theme)];
        target.texture = manifest.textureIdByName(definition.textureName);
        const std::vector<PixelRegion> regions = readRegions(assetRoot / definition.xmlPath);
        int atlasWidth = 0;
        int atlasHeight = 0;
        for (const PixelRegion& region : regions) {
            atlasWidth = std::max(atlasWidth, region.x + region.width);
            atlasHeight = std::max(atlasHeight, region.y + region.height);
        }
        for (const PixelRegion& region : regions) {
            // Kenney's XML coordinates use a bottom-left origin, while the
            // decoded PNG rows and UI UVs use a top-left origin.
            const int imageY = atlasHeight - region.y - region.height;
            // Sample inside each cell so linear filtering cannot pull a color
            // from the immediately adjacent glyph in Kenney's packed sheet.
            target.regions.emplace(region.name, UiRect {
                { (static_cast<float>(region.x) + 0.5f) / atlasWidth,
                    (static_cast<float>(imageY) + 0.5f) / atlasHeight },
                { (static_cast<float>(region.width) - 1.0f) / atlasWidth,
                    (static_cast<float>(region.height) - 1.0f) / atlasHeight },
            });
            target.aspectRatios.emplace(
                region.name,
                static_cast<float>(region.width) / region.height);
        }
    }
}

InputPromptTheme InputPromptCatalog::themeForGamepad(
    const GamepadPresentation& gamepad) const
{
    switch (gamepad.type) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE: return InputPromptTheme::Xbox;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return InputPromptTheme::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return InputPromptTheme::NintendoSwitch;
    case SDL_GAMEPAD_TYPE_GAMECUBE: return InputPromptTheme::NintendoGameCube;
    default: break;
    }
    const std::string name = lowercase(gamepad.name);
    if (name.find("steam deck") != std::string::npos) return InputPromptTheme::SteamDeck;
    if (name.find("xbox") != std::string::npos) return InputPromptTheme::Xbox;
    if (name.find("dualshock") != std::string::npos ||
        name.find("dualsense") != std::string::npos ||
        name.find("playstation") != std::string::npos) {
        return InputPromptTheme::PlayStation;
    }
    if (name.find("switch") != std::string::npos ||
        name.find("joy-con") != std::string::npos) {
        return InputPromptTheme::NintendoSwitch;
    }
    return InputPromptTheme::Generic;
}

std::optional<InputPromptGlyph> InputPromptCatalog::glyphForBinding(
    const InputBinding& binding,
    const GamepadPresentation& gamepad) const
{
    if (const auto* keyboard = std::get_if<KeyboardBinding>(&binding)) {
        return find(InputPromptTheme::Keyboard, keyboardIconName(keyboard->scancode));
    }
    const InputPromptTheme theme = themeForGamepad(gamepad);
    if (const auto* button = std::get_if<GamepadButtonBinding>(&binding)) {
        return find(theme, gamepadButtonIconName(button->button, theme, gamepad));
    }
    return find(theme, gamepadAxisIconName(std::get<GamepadAxisBinding>(binding), theme));
}

const InputPromptCatalog::Atlas& InputPromptCatalog::atlas(
    InputPromptTheme theme) const
{
    return atlases_[static_cast<std::size_t>(theme)];
}

std::optional<InputPromptGlyph> InputPromptCatalog::find(
    InputPromptTheme theme,
    std::string_view name) const
{
    if (name.empty()) return std::nullopt;
    const Atlas& source = atlas(theme);
    const auto region = source.regions.find(std::string(name));
    if (region == source.regions.end()) return std::nullopt;
    return InputPromptGlyph {
        .texture = source.texture,
        .uvRect = region->second,
        .aspectRatio = source.aspectRatios.at(region->first),
    };
}

void drawInputPromptGlyph(
    UiContext& ui,
    UiRect bounds,
    const InputPromptGlyph& glyph,
    Vec4 color)
{
    const float width = std::min(bounds.size.x, bounds.size.y * glyph.aspectRatio);
    const float height = std::min(bounds.size.y, bounds.size.x / glyph.aspectRatio);
    ui.textureImage({
        { bounds.position.x + (bounds.size.x - width) * 0.5f,
            bounds.position.y + (bounds.size.y - height) * 0.5f },
        { width, height },
    }, glyph.texture, glyph.uvRect, color);
}

} // namespace sokoban
