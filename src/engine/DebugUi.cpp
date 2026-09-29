#include "engine/DebugUi.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace sokoban {
namespace {

struct DebugTab {
    std::string name;
    DebugUi::DrawCallback callback;
    bool open = true;
};

std::vector<DebugTab>& debugTabs()
{
    static std::vector<DebugTab> tabs;
    return tabs;
}

std::vector<DebugTab>& debugMenus()
{
    static std::vector<DebugTab> menus;
    return menus;
}

enum class DebugUiTheme {
    Dark,
    Light,
    Classic,
    HighContrast,
    Dracula,
    Nord,
    CatppuccinMocha,
    GruvboxDark,
    SolarizedDark,
    SolarizedLight,
    TokyoNight,
    RosePine,
    OneDark,
    Everforest,
};

struct DebugUiThemeDefinition {
    DebugUiTheme theme;
    const char* label;
    const char* key;
    const char* description;
    bool builtIn;
};

constexpr std::array debugUiThemes {
    DebugUiThemeDefinition {
        DebugUiTheme::Dark,
        "Dear ImGui Dark",
        "Dark",
        "Dear ImGui's default dark palette.",
        true,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::Light,
        "Dear ImGui Light",
        "Light",
        "Dear ImGui's default light palette.",
        true,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::Classic,
        "Dear ImGui Classic",
        "Classic",
        "Dear ImGui's original high-contrast palette.",
        true,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::HighContrast,
        "High Contrast",
        "HighContrast",
        "Near-black surfaces, bright text, and strong focus indicators.",
        false,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::Dracula,
        "Dracula",
        "Dracula",
        "Deep violet surfaces with bright purple and cyan accents.",
        false,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::Nord,
        "Nord",
        "Nord",
        "An arctic blue palette with muted, low-glare contrast.",
        false,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::CatppuccinMocha,
        "Catppuccin Mocha",
        "CatppuccinMocha",
        "A warm dark palette with pastel blue and mauve accents.",
        false,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::GruvboxDark,
        "Gruvbox Dark",
        "GruvboxDark",
        "Warm retro neutrals with earthy blue and aqua accents.",
        false,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::SolarizedDark,
        "Solarized Dark",
        "SolarizedDark",
        "Low-glare blue-green surfaces with balanced contrast.",
        false,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::SolarizedLight,
        "Solarized Light",
        "SolarizedLight",
        "A warm light theme using Solarized's symmetric palette.",
        false,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::TokyoNight,
        "Tokyo Night",
        "TokyoNight",
        "Midnight navy surfaces with vivid blue and violet accents.",
        false,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::RosePine,
        "Rose Pine",
        "RosePine",
        "Soft ink surfaces with rose, foam, and iris accents.",
        false,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::OneDark,
        "One Dark",
        "OneDark",
        "Graphite surfaces with crisp blue and purple accents.",
        false,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::Everforest,
        "Everforest",
        "Everforest",
        "Muted forest greens and warm, comfortable foregrounds.",
        false,
    },
};

struct DebugUiAppearanceState {
    ImGuiStyle baseStyle;
    float scale = 1.0f;
    DebugUiTheme theme = DebugUiTheme::Dark;
    bool baseStyleCaptured = false;
};

DebugUiAppearanceState& debugUiAppearanceState()
{
    static DebugUiAppearanceState state;
    return state;
}

struct DebugUiWorkspaceState {
    bool gameViewportOpen = true;
    bool gameplayFullWindow = false;
    bool resetLayout = false;
    std::array<char, 64> layoutName {};
    std::vector<std::string> layouts;
    std::string activeLayout;
    std::string status;
};

DebugUiWorkspaceState& workspaceState()
{
    static DebugUiWorkspaceState state;
    return state;
}

ImVec4 color(unsigned int rgb, float alpha = 1.0f)
{
    return {
        static_cast<float>((rgb >> 16U) & 0xffU) / 255.0f,
        static_cast<float>((rgb >> 8U) & 0xffU) / 255.0f,
        static_cast<float>(rgb & 0xffU) / 255.0f,
        alpha,
    };
}

ImVec4 withAlpha(ImVec4 value, float alpha)
{
    value.w = alpha;
    return value;
}

struct DebugUiPalette {
    ImVec4 text;
    ImVec4 textDisabled;
    ImVec4 window;
    ImVec4 child;
    ImVec4 popup;
    ImVec4 border;
    ImVec4 surface;
    ImVec4 surfaceHovered;
    ImVec4 surfaceActive;
    ImVec4 accent;
    ImVec4 accentHovered;
    ImVec4 accentActive;
    ImVec4 warning;
    ImVec4 positive;
};

void applyPalette(ImGuiStyle& style, const DebugUiPalette& palette)
{
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = palette.text;
    colors[ImGuiCol_TextDisabled] = palette.textDisabled;
    colors[ImGuiCol_WindowBg] = palette.window;
    colors[ImGuiCol_ChildBg] = palette.child;
    colors[ImGuiCol_PopupBg] = palette.popup;
    colors[ImGuiCol_Border] = palette.border;
    colors[ImGuiCol_BorderShadow] = withAlpha(palette.window, 0.0f);
    colors[ImGuiCol_FrameBg] = palette.surface;
    colors[ImGuiCol_FrameBgHovered] = palette.surfaceHovered;
    colors[ImGuiCol_FrameBgActive] = palette.surfaceActive;
    colors[ImGuiCol_TitleBg] = palette.child;
    colors[ImGuiCol_TitleBgActive] = palette.surface;
    colors[ImGuiCol_TitleBgCollapsed] = palette.child;
    colors[ImGuiCol_MenuBarBg] = palette.child;
    colors[ImGuiCol_ScrollbarBg] = palette.child;
    colors[ImGuiCol_ScrollbarGrab] = palette.surface;
    colors[ImGuiCol_ScrollbarGrabHovered] = palette.surfaceHovered;
    colors[ImGuiCol_ScrollbarGrabActive] = palette.surfaceActive;
    colors[ImGuiCol_CheckMark] = palette.accent;
    colors[ImGuiCol_CheckboxSelectedBg] = palette.accent;
    colors[ImGuiCol_SliderGrab] = palette.accent;
    colors[ImGuiCol_SliderGrabActive] = palette.accentActive;
    colors[ImGuiCol_Button] = palette.surface;
    colors[ImGuiCol_ButtonHovered] = palette.surfaceHovered;
    colors[ImGuiCol_ButtonActive] = palette.surfaceActive;
    colors[ImGuiCol_Header] = palette.surface;
    colors[ImGuiCol_HeaderHovered] = palette.surfaceHovered;
    colors[ImGuiCol_HeaderActive] = palette.surfaceActive;
    colors[ImGuiCol_Separator] = palette.border;
    colors[ImGuiCol_SeparatorHovered] = palette.accentHovered;
    colors[ImGuiCol_SeparatorActive] = palette.accentActive;
    colors[ImGuiCol_ResizeGrip] = withAlpha(palette.accent, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = withAlpha(palette.accentHovered, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = palette.accentActive;
    colors[ImGuiCol_InputTextCursor] = palette.accent;
    colors[ImGuiCol_TabHovered] = palette.surfaceHovered;
    colors[ImGuiCol_Tab] = palette.surface;
    colors[ImGuiCol_TabSelected] = palette.surfaceActive;
    colors[ImGuiCol_TabSelectedOverline] = palette.accent;
    colors[ImGuiCol_TabDimmed] = palette.child;
    colors[ImGuiCol_TabDimmedSelected] = palette.surface;
    colors[ImGuiCol_TabDimmedSelectedOverline] = palette.textDisabled;
    colors[ImGuiCol_DockingPreview] = withAlpha(palette.accent, 0.70f);
    colors[ImGuiCol_DockingEmptyBg] = palette.window;
    colors[ImGuiCol_PlotLines] = palette.textDisabled;
    colors[ImGuiCol_PlotLinesHovered] = palette.accentHovered;
    colors[ImGuiCol_PlotHistogram] = palette.positive;
    colors[ImGuiCol_PlotHistogramHovered] = palette.warning;
    colors[ImGuiCol_TableHeaderBg] = palette.surface;
    colors[ImGuiCol_TableBorderStrong] = palette.border;
    colors[ImGuiCol_TableBorderLight] = withAlpha(palette.border, 0.55f);
    colors[ImGuiCol_TableRowBg] = withAlpha(palette.window, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = withAlpha(palette.surface, 0.35f);
    colors[ImGuiCol_TextLink] = palette.accent;
    colors[ImGuiCol_TextSelectedBg] = withAlpha(palette.accent, 0.35f);
    colors[ImGuiCol_TreeLines] = palette.border;
    colors[ImGuiCol_DragDropTarget] = palette.warning;
    colors[ImGuiCol_DragDropTargetBg] = withAlpha(palette.warning, 0.15f);
    colors[ImGuiCol_UnsavedMarker] = palette.warning;
    colors[ImGuiCol_NavCursor] = palette.accent;
    colors[ImGuiCol_NavWindowingHighlight] = withAlpha(palette.text, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = color(0x000000, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = color(0x000000, 0.35f);
}

void applyThemeColors(ImGuiStyle& style, DebugUiTheme theme)
{
    switch (theme) {
    case DebugUiTheme::Dark:
        ImGui::StyleColorsDark(&style);
        return;
    case DebugUiTheme::Light:
        ImGui::StyleColorsLight(&style);
        return;
    case DebugUiTheme::Classic:
        ImGui::StyleColorsClassic(&style);
        return;
    case DebugUiTheme::HighContrast:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xffffff),
            .textDisabled = color(0xb0b0b0),
            .window = color(0x000000),
            .child = color(0x080808),
            .popup = color(0x101010),
            .border = color(0xffffff),
            .surface = color(0x161616),
            .surfaceHovered = color(0x004e7a),
            .surfaceActive = color(0x007acc),
            .accent = color(0x00c8ff),
            .accentHovered = color(0xffffff),
            .accentActive = color(0xffd400),
            .warning = color(0xffd400),
            .positive = color(0x53ff7a),
        });
        return;
    case DebugUiTheme::Dracula:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xf8f8f2),
            .textDisabled = color(0x6272a4),
            .window = color(0x282a36),
            .child = color(0x21222c),
            .popup = color(0x343746),
            .border = color(0x44475a),
            .surface = color(0x343746),
            .surfaceHovered = color(0x44475a),
            .surfaceActive = color(0x6272a4),
            .accent = color(0xbd93f9),
            .accentHovered = color(0xff79c6),
            .accentActive = color(0x8be9fd),
            .warning = color(0xffb86c),
            .positive = color(0x50fa7b),
        });
        return;
    case DebugUiTheme::Nord:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xd8dee9),
            .textDisabled = color(0x7b88a1),
            .window = color(0x2e3440),
            .child = color(0x272c36),
            .popup = color(0x3b4252),
            .border = color(0x4c566a),
            .surface = color(0x3b4252),
            .surfaceHovered = color(0x434c5e),
            .surfaceActive = color(0x4c566a),
            .accent = color(0x88c0d0),
            .accentHovered = color(0x81a1c1),
            .accentActive = color(0x5e81ac),
            .warning = color(0xebcb8b),
            .positive = color(0xa3be8c),
        });
        return;
    case DebugUiTheme::CatppuccinMocha:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xcdd6f4),
            .textDisabled = color(0x7f849c),
            .window = color(0x1e1e2e),
            .child = color(0x181825),
            .popup = color(0x313244),
            .border = color(0x45475a),
            .surface = color(0x313244),
            .surfaceHovered = color(0x45475a),
            .surfaceActive = color(0x585b70),
            .accent = color(0x89b4fa),
            .accentHovered = color(0xb4befe),
            .accentActive = color(0xcba6f7),
            .warning = color(0xfab387),
            .positive = color(0xa6e3a1),
        });
        return;
    case DebugUiTheme::GruvboxDark:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xebdbb2),
            .textDisabled = color(0x928374),
            .window = color(0x282828),
            .child = color(0x1d2021),
            .popup = color(0x3c3836),
            .border = color(0x665c54),
            .surface = color(0x3c3836),
            .surfaceHovered = color(0x504945),
            .surfaceActive = color(0x665c54),
            .accent = color(0x83a598),
            .accentHovered = color(0x8ec07c),
            .accentActive = color(0xd3869b),
            .warning = color(0xfabd2f),
            .positive = color(0xb8bb26),
        });
        return;
    case DebugUiTheme::SolarizedDark:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0x93a1a1),
            .textDisabled = color(0x586e75),
            .window = color(0x002b36),
            .child = color(0x00212b),
            .popup = color(0x073642),
            .border = color(0x586e75),
            .surface = color(0x073642),
            .surfaceHovered = color(0x174b57),
            .surfaceActive = color(0x586e75),
            .accent = color(0x268bd2),
            .accentHovered = color(0x2aa198),
            .accentActive = color(0x6c71c4),
            .warning = color(0xb58900),
            .positive = color(0x859900),
        });
        return;
    case DebugUiTheme::SolarizedLight:
        ImGui::StyleColorsLight(&style);
        applyPalette(style, {
            .text = color(0x586e75),
            .textDisabled = color(0x93a1a1),
            .window = color(0xfdf6e3),
            .child = color(0xeee8d5),
            .popup = color(0xfffbeb),
            .border = color(0x93a1a1),
            .surface = color(0xeee8d5),
            .surfaceHovered = color(0xd8d2c1),
            .surfaceActive = color(0x93a1a1),
            .accent = color(0x268bd2),
            .accentHovered = color(0x2aa198),
            .accentActive = color(0x6c71c4),
            .warning = color(0xb58900),
            .positive = color(0x859900),
        });
        return;
    case DebugUiTheme::TokyoNight:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xc0caf5),
            .textDisabled = color(0x545c7e),
            .window = color(0x1a1b26),
            .child = color(0x16161e),
            .popup = color(0x24283b),
            .border = color(0x414868),
            .surface = color(0x24283b),
            .surfaceHovered = color(0x292e42),
            .surfaceActive = color(0x414868),
            .accent = color(0x7aa2f7),
            .accentHovered = color(0x2ac3de),
            .accentActive = color(0xbb9af7),
            .warning = color(0xe0af68),
            .positive = color(0x9ece6a),
        });
        return;
    case DebugUiTheme::RosePine:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xe0def4),
            .textDisabled = color(0x6e6a86),
            .window = color(0x191724),
            .child = color(0x13111d),
            .popup = color(0x1f1d2e),
            .border = color(0x403d52),
            .surface = color(0x1f1d2e),
            .surfaceHovered = color(0x26233a),
            .surfaceActive = color(0x403d52),
            .accent = color(0xc4a7e7),
            .accentHovered = color(0x9ccfd8),
            .accentActive = color(0x31748f),
            .warning = color(0xf6c177),
            .positive = color(0x9ccfd8),
        });
        return;
    case DebugUiTheme::OneDark:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xabb2bf),
            .textDisabled = color(0x5c6370),
            .window = color(0x282c34),
            .child = color(0x21252b),
            .popup = color(0x2c323c),
            .border = color(0x4b5263),
            .surface = color(0x2c323c),
            .surfaceHovered = color(0x3e4451),
            .surfaceActive = color(0x4b5263),
            .accent = color(0x61afef),
            .accentHovered = color(0x56b6c2),
            .accentActive = color(0xc678dd),
            .warning = color(0xe5c07b),
            .positive = color(0x98c379),
        });
        return;
    case DebugUiTheme::Everforest:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xd3c6aa),
            .textDisabled = color(0x7a8478),
            .window = color(0x2d353b),
            .child = color(0x232a2e),
            .popup = color(0x3d484d),
            .border = color(0x4f585e),
            .surface = color(0x343f44),
            .surfaceHovered = color(0x3d484d),
            .surfaceActive = color(0x475258),
            .accent = color(0x7fbbb3),
            .accentHovered = color(0x83c092),
            .accentActive = color(0xd699b6),
            .warning = color(0xdbbc7f),
            .positive = color(0xa7c080),
        });
        return;
    }
}

const char* themeKey(DebugUiTheme theme)
{
    for (const DebugUiThemeDefinition& definition : debugUiThemes) {
        if (definition.theme == theme) {
            return definition.key;
        }
    }
    return "Dark";
}

std::optional<DebugUiTheme> themeFromKey(std::string_view key)
{
    for (const DebugUiThemeDefinition& definition : debugUiThemes) {
        if (key == definition.key) {
            return definition.theme;
        }
    }
    return std::nullopt;
}

void applyDebugUiAppearance()
{
    DebugUiAppearanceState& state = debugUiAppearanceState();
    if (!state.baseStyleCaptured) {
        state.baseStyle = ImGui::GetStyle();
        state.baseStyleCaptured = true;
    }

    state.scale = std::clamp(state.scale, 1.0f, 3.0f);
    ImGuiStyle appearance = state.baseStyle;
    applyThemeColors(appearance, state.theme);
    appearance.ScaleAllSizes(state.scale);
    appearance.FontScaleMain = state.baseStyle.FontScaleMain * state.scale;
    ImGui::GetStyle() = appearance;
}

std::filesystem::path layoutDirectory()
{
    const char* iniFilename = ImGui::GetIO().IniFilename;
    const std::filesystem::path iniPath =
        iniFilename && *iniFilename
        ? std::filesystem::path(iniFilename)
        : std::filesystem::path("imgui.ini");
    return iniPath.parent_path() / "imgui-layouts";
}

std::string normalizedLayoutName(const char* input)
{
    std::string name = input ? input : "";
    const auto valid = [](unsigned char value) {
        return std::isalnum(value) || value == ' ' || value == '-' ||
            value == '_';
    };
    std::replace_if(name.begin(), name.end(), [&](char value) {
        return !valid(static_cast<unsigned char>(value));
    }, '_');
    const auto content = [](unsigned char value) {
        return !std::isspace(value);
    };
    const auto first = std::find_if(name.begin(), name.end(), content);
    const auto last = std::find_if(name.rbegin(), name.rend(), content).base();
    if (first >= last) {
        return {};
    }
    name = std::string(first, last);
    if (name.size() > 48) {
        name.resize(48);
    }
    return name;
}

std::filesystem::path layoutPath(std::string_view name)
{
    return layoutDirectory() / (std::string(name) + ".ini");
}

void refreshLayouts()
{
    DebugUiWorkspaceState& state = workspaceState();
    state.layouts.clear();
    std::error_code error;
    const std::filesystem::path directory = layoutDirectory();
    if (!std::filesystem::exists(directory, error)) {
        return;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory, error)) {
        if (error) {
            break;
        }
        if (entry.is_regular_file(error) &&
            entry.path().extension() == ".ini") {
            state.layouts.push_back(entry.path().stem().string());
        }
    }
    std::sort(state.layouts.begin(), state.layouts.end());
}

void saveLayout(const char* requestedName)
{
    DebugUiWorkspaceState& state = workspaceState();
    const std::string name = normalizedLayoutName(requestedName);
    if (name.empty()) {
        state.status = "Enter a layout name first.";
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(layoutDirectory(), error);
    if (error) {
        state.status = "Could not create the layout directory.";
        return;
    }
    const std::filesystem::path path = layoutPath(name);
    ImGui::SaveIniSettingsToDisk(path.string().c_str());
    state.activeLayout = name;
    state.status = "Saved layout '" + name + "'.";
    refreshLayouts();
}

void loadLayout(std::string_view name)
{
    DebugUiWorkspaceState& state = workspaceState();
    const std::filesystem::path path = layoutPath(name);
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        state.status = "The selected layout no longer exists.";
        refreshLayouts();
        return;
    }
    ImGui::ClearIniSettings();
    ImGui::LoadIniSettingsFromDisk(path.string().c_str());
    state.activeLayout = std::string(name);
    state.status = "Loaded layout '" + std::string(name) + "'.";
}

void deleteLayout(std::string_view name)
{
    DebugUiWorkspaceState& state = workspaceState();
    std::error_code error;
    const bool removed = std::filesystem::remove(layoutPath(name), error);
    if (removed && !error) {
        if (state.activeLayout == name) {
            state.activeLayout.clear();
        }
        state.status = "Deleted layout '" + std::string(name) + "'.";
    } else {
        state.status = "Could not delete layout '" + std::string(name) + "'.";
    }
    refreshLayouts();
}

void buildDefaultLayout(ImGuiID dockspaceId)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(
        dockspaceId,
        ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID toolsNode = 0;
    ImGuiID gameNode = dockspaceId;
    ImGui::DockBuilderSplitNode(
        gameNode,
        ImGuiDir_Right,
        0.32f,
        &toolsNode,
        &gameNode);
    ImGui::DockBuilderDockWindow("Game Viewport", gameNode);
    for (const DebugTab& tab : debugTabs()) {
        ImGui::DockBuilderDockWindow(tab.name.c_str(), toolsNode);
    }
    ImGui::DockBuilderFinish(dockspaceId);
}

void drawScaleControl()
{
    DebugUiAppearanceState& appearance = debugUiAppearanceState();
    float requestedScale = appearance.scale;
    ImGui::SetNextItemWidth(140.0f * appearance.scale);
    bool scaleChanged = ImGui::SliderFloat(
        "UI Scale",
        &requestedScale,
        1.0f,
        3.0f,
        "%.2fx",
        ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::MenuItem("Reset UI Scale")) {
        requestedScale = 1.0f;
        scaleChanged = true;
    }
    if (scaleChanged) {
        appearance.scale = requestedScale;
        applyDebugUiAppearance();
        ImGui::MarkIniSettingsDirty();
    }
}

void drawThemeMenu()
{
    DebugUiAppearanceState& appearance = debugUiAppearanceState();
    bool reachedCustomThemes = false;
    for (const DebugUiThemeDefinition& definition : debugUiThemes) {
        if (!definition.builtIn && !reachedCustomThemes) {
            ImGui::Separator();
            reachedCustomThemes = true;
        }
        const bool selected = appearance.theme == definition.theme;
        if (ImGui::MenuItem(definition.label, nullptr, selected) &&
            !selected) {
            appearance.theme = definition.theme;
            applyDebugUiAppearance();
            ImGui::MarkIniSettingsDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", definition.description);
        }
    }
}

void drawWorkspaceMenu()
{
    DebugUiWorkspaceState& state = workspaceState();
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Layouts")) {
            ImGui::SetNextItemWidth(220.0f);
            const bool submitted = ImGui::InputText(
                "##LayoutName",
                state.layoutName.data(),
                state.layoutName.size(),
                ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Save") || submitted) {
                saveLayout(state.layoutName.data());
            }

            if (ImGui::BeginMenu("Load")) {
                if (state.layouts.empty()) {
                    ImGui::TextDisabled("No saved layouts");
                }
                for (const std::string& layout : state.layouts) {
                    if (ImGui::MenuItem(
                            layout.c_str(),
                            nullptr,
                            layout == state.activeLayout)) {
                        loadLayout(layout);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Delete")) {
                std::string deleteRequest;
                if (state.layouts.empty()) {
                    ImGui::TextDisabled("No saved layouts");
                }
                for (const std::string& layout : state.layouts) {
                    if (ImGui::MenuItem(layout.c_str())) {
                        deleteRequest = layout;
                        break;
                    }
                }
                ImGui::EndMenu();
                if (!deleteRequest.empty()) {
                    deleteLayout(deleteRequest);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset to Default")) {
                state.resetLayout = true;
                state.activeLayout.clear();
                state.status = "Restored the default layout.";
            }
            if (!state.status.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("%s", state.status.c_str());
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Panels")) {
            ImGui::MenuItem("Game Viewport", nullptr, &state.gameViewportOpen);
            for (DebugTab& tab : debugTabs()) {
                ImGui::MenuItem(tab.name.c_str(), nullptr, &tab.open);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            drawScaleControl();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Themes")) {
            drawThemeMenu();
            ImGui::EndMenu();
        }
        if (ImGui::Button("Full Game View (F11)")) {
            state.gameplayFullWindow = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Hide the debug workspace and fill this window with the "
                "gameplay view. Press F11 to return.");
        }
        for (const DebugTab& menu : debugMenus()) {
            if (ImGui::BeginMenu(menu.name.c_str())) {
                menu.callback();
                ImGui::EndMenu();
            }
        }
        ImGui::EndMainMenuBar();
    }
}

DebugUi::DrawResult drawGameViewport(DebugUi::GameViewport viewport)
{
    DebugUi::DrawResult result;
    DebugUiWorkspaceState& state = workspaceState();
    if (!state.gameViewportOpen) {
        return result;
    }
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("Game Viewport", &state.gameViewportOpen, flags)) {
        result.viewportFocused = ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows);
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (viewport.texture == 0 || viewport.width == 0 ||
            viewport.height == 0) {
            ImGui::TextDisabled("The game render target is not available.");
        } else if (available.x > 0.0f && available.y > 0.0f) {
            const float aspect = static_cast<float>(viewport.width) /
                static_cast<float>(viewport.height);
            ImVec2 size { available.x, available.x / aspect };
            if (size.y > available.y) {
                size = { available.y * aspect, available.y };
            }
            const ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::SetCursorPos({
                cursor.x + std::max(0.0f, (available.x - size.x) * 0.5f),
                cursor.y + std::max(0.0f, (available.y - size.y) * 0.5f),
            });
            ImGui::Image(viewport.texture, size);
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            result.viewportX = minimum.x;
            result.viewportY = minimum.y;
            result.viewportWidth = maximum.x - minimum.x;
            result.viewportHeight = maximum.y - minimum.y;
            result.viewportHovered = ImGui::IsItemHovered();
        }
    }
    ImGui::End();
    return result;
}

void resetDebugUiAppearanceSettings(
    ImGuiContext*, ImGuiSettingsHandler*)
{
    DebugUiAppearanceState& state = debugUiAppearanceState();
    state.scale = 1.0f;
    state.theme = DebugUiTheme::Dark;
}

void* openDebugUiAppearanceSettings(
    ImGuiContext*, ImGuiSettingsHandler*, const char* name)
{
    return std::strcmp(name, "Settings") == 0
        ? &debugUiAppearanceState()
        : nullptr;
}

void readDebugUiAppearanceSetting(
    ImGuiContext*,
    ImGuiSettingsHandler*,
    void* entry,
    const char* line)
{
    auto& state = *static_cast<DebugUiAppearanceState*>(entry);
    constexpr char scalePrefix[] = "Scale=";
    if (std::strncmp(line, scalePrefix, sizeof(scalePrefix) - 1) == 0) {
        char* end = nullptr;
        const float scale = std::strtof(
            line + sizeof(scalePrefix) - 1,
            &end);
        if (end != line + sizeof(scalePrefix) - 1 && *end == '\0') {
            state.scale = std::clamp(scale, 1.0f, 3.0f);
        }
        return;
    }

    constexpr char themePrefix[] = "Theme=";
    if (std::strncmp(line, themePrefix, sizeof(themePrefix) - 1) == 0) {
        if (const std::optional<DebugUiTheme> theme =
                themeFromKey(line + sizeof(themePrefix) - 1)) {
            state.theme = *theme;
        }
    }
}

void applyDebugUiAppearanceSettings(
    ImGuiContext*, ImGuiSettingsHandler*)
{
    applyDebugUiAppearance();
}

void writeDebugUiAppearanceSettings(
    ImGuiContext*,
    ImGuiSettingsHandler*,
    ImGuiTextBuffer* output)
{
    output->appendf(
        "[DebugUi][Settings]\nScale=%.3f\nTheme=%s\n\n",
        debugUiAppearanceState().scale,
        themeKey(debugUiAppearanceState().theme));
}

} // namespace

void DebugUi::initialize()
{
    DebugUiAppearanceState& state = debugUiAppearanceState();
    state.baseStyle = ImGui::GetStyle();
    state.baseStyleCaptured = true;
    state.scale = 1.0f;
    state.theme = DebugUiTheme::Dark;
    workspaceState().gameplayFullWindow = false;
    refreshLayouts();

    if (ImGui::FindSettingsHandler("DebugUi")) {
        return;
    }
    ImGuiSettingsHandler handler;
    handler.TypeName = "DebugUi";
    handler.TypeHash = ImHashStr("DebugUi");
    handler.ReadInitFn = resetDebugUiAppearanceSettings;
    handler.ReadOpenFn = openDebugUiAppearanceSettings;
    handler.ReadLineFn = readDebugUiAppearanceSetting;
    handler.ApplyAllFn = applyDebugUiAppearanceSettings;
    handler.WriteAllFn = writeDebugUiAppearanceSettings;
    ImGui::AddSettingsHandler(&handler);
}

void DebugUi::addTab(std::string name, DrawCallback callback)
{
    debugTabs().push_back({
        .name = std::move(name),
        .callback = std::move(callback),
        .open = true,
    });
}

void DebugUi::addMenu(std::string name, DrawCallback callback)
{
    debugMenus().push_back({
        .name = std::move(name),
        .callback = std::move(callback),
    });
}

void DebugUi::clearTabs()
{
    debugTabs().clear();
    debugMenus().clear();
}

DebugUi::DrawResult DebugUi::draw(GameViewport gameViewport)
{
    DebugUiAppearanceState& appearance = debugUiAppearanceState();
    if (!appearance.baseStyleCaptured) {
        applyDebugUiAppearance();
    }

    DebugUiWorkspaceState& workspace = workspaceState();
    if (ImGui::Shortcut(
            ImGuiKey_F11,
            ImGuiInputFlags_RouteGlobal |
                ImGuiInputFlags_RouteOverFocused |
                ImGuiInputFlags_RouteOverActive)) {
        workspace.gameplayFullWindow = !workspace.gameplayFullWindow;
    }
    if (workspace.gameplayFullWindow) {
        return { .gameplayFullWindow = true };
    }

    drawWorkspaceMenu();
    if (workspace.gameplayFullWindow) {
        return { .gameplayFullWindow = true };
    }
    const ImGuiID dockspaceId = ImGui::GetID("SokobanDockSpace");
    const bool dockspaceExisted =
        ImGui::DockBuilderGetNode(dockspaceId) != nullptr;
    ImGui::DockSpaceOverViewport(
        dockspaceId,
        ImGui::GetMainViewport(),
        ImGuiDockNodeFlags_None);
    if (workspace.resetLayout || !dockspaceExisted) {
        buildDefaultLayout(dockspaceId);
        workspace.resetLayout = false;
    }

    const DrawResult result = drawGameViewport(gameViewport);
    // A tab added after imgui.ini was written has no saved placement and
    // would open as a floating window over the game. Dock it beside the
    // first tab that has a dock node instead, which is where the default
    // layout puts every tab.
    // Read the placement from the loaded settings as well as from live
    // windows: on the first frame no window exists yet, and that is exactly
    // when a new tab must be told where to go.
    ImGuiID toolsDockId = 0;
    for (const DebugTab& tab : debugTabs()) {
        if (const ImGuiWindow* window =
                ImGui::FindWindowByName(tab.name.c_str());
            window != nullptr && window->DockId != 0) {
            toolsDockId = window->DockId;
            break;
        }
        if (const ImGuiWindowSettings* settings =
                ImGui::FindWindowSettingsByID(ImHashStr(tab.name.c_str()));
            settings != nullptr && settings->DockId != 0) {
            toolsDockId = settings->DockId;
            break;
        }
    }
    for (DebugTab& tab : debugTabs()) {
        if (!tab.open) {
            continue;
        }
        if (toolsDockId != 0 &&
            ImGui::FindWindowSettingsByID(ImHashStr(tab.name.c_str())) ==
                nullptr &&
            ImGui::FindWindowByName(tab.name.c_str()) == nullptr) {
            ImGui::SetNextWindowDockID(toolsDockId, ImGuiCond_FirstUseEver);
        }
        if (ImGui::Begin(tab.name.c_str(), &tab.open)) {
            tab.callback();
        }
        ImGui::End();
    }
    return result;
}

} // namespace sokoban

#endif
