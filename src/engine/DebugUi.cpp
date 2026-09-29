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
    DeepBlue,
    BlackGreen,
    ForestGreen,
    Amethyst,
    Sapphire,
    Amber,
    CrimsonVesuvius,
    Cyberpunk,
    PaperAndInk,
    RoseQuartz,
    NuklearGray,
};

enum class DebugUiThemeGroup {
    BuiltIn,
    Curated,
    ImGuiGallery,
};

struct DebugUiThemeDefinition {
    DebugUiTheme theme;
    const char* label;
    const char* key;
    const char* description;
    DebugUiThemeGroup group;
};

constexpr std::array debugUiThemes {
    DebugUiThemeDefinition {
        DebugUiTheme::Dark,
        "Dear ImGui Dark",
        "Dark",
        "Dear ImGui's default dark palette.",
        DebugUiThemeGroup::BuiltIn,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::Light,
        "Dear ImGui Light",
        "Light",
        "Dear ImGui's default light palette.",
        DebugUiThemeGroup::BuiltIn,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::Classic,
        "Dear ImGui Classic",
        "Classic",
        "Dear ImGui's original high-contrast palette.",
        DebugUiThemeGroup::BuiltIn,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::HighContrast,
        "High Contrast",
        "HighContrast",
        "Near-black surfaces, bright text, and strong focus indicators.",
        DebugUiThemeGroup::Curated,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::Dracula,
        "Dracula",
        "Dracula",
        "Deep violet surfaces with bright purple and cyan accents.",
        DebugUiThemeGroup::Curated,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::Nord,
        "Nord",
        "Nord",
        "An arctic blue palette with muted, low-glare contrast.",
        DebugUiThemeGroup::Curated,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::CatppuccinMocha,
        "Catppuccin Mocha",
        "CatppuccinMocha",
        "A warm dark palette with pastel blue and mauve accents.",
        DebugUiThemeGroup::Curated,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::GruvboxDark,
        "Gruvbox Dark",
        "GruvboxDark",
        "Warm retro neutrals with earthy blue and aqua accents.",
        DebugUiThemeGroup::Curated,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::SolarizedDark,
        "Solarized Dark",
        "SolarizedDark",
        "Low-glare blue-green surfaces with balanced contrast.",
        DebugUiThemeGroup::Curated,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::SolarizedLight,
        "Solarized Light",
        "SolarizedLight",
        "A warm light theme using Solarized's symmetric palette.",
        DebugUiThemeGroup::Curated,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::TokyoNight,
        "Tokyo Night",
        "TokyoNight",
        "Midnight navy surfaces with vivid blue and violet accents.",
        DebugUiThemeGroup::Curated,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::RosePine,
        "Rose Pine",
        "RosePine",
        "Soft ink surfaces with rose, foam, and iris accents.",
        DebugUiThemeGroup::Curated,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::OneDark,
        "One Dark",
        "OneDark",
        "Graphite surfaces with crisp blue and purple accents.",
        DebugUiThemeGroup::Curated,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::Everforest,
        "Everforest",
        "Everforest",
        "Muted forest greens and warm, comfortable foregrounds.",
        DebugUiThemeGroup::Curated,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::DeepBlue,
        "Deep Blue",
        "DeepBlue",
        "An early ImGui gallery favorite with rounded windows and square scrollbars.",
        DebugUiThemeGroup::ImGuiGallery,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::BlackGreen,
        "Black & Green",
        "BlackGreen",
        "Compact near-black surfaces with vivid green controls.",
        DebugUiThemeGroup::ImGuiGallery,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::ForestGreen,
        "Forest Green",
        "ForestGreen",
        "Roomy dark green panels with softly rounded controls.",
        DebugUiThemeGroup::ImGuiGallery,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::Amethyst,
        "Amethyst",
        "Amethyst",
        "A dark violet interface with lilac highlights.",
        DebugUiThemeGroup::ImGuiGallery,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::Sapphire,
        "Sapphire",
        "Sapphire",
        "Deep blue surfaces with crisp sapphire focus states.",
        DebugUiThemeGroup::ImGuiGallery,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::Amber,
        "Amber",
        "Amber",
        "Charcoal and warm amber with compact, gently rounded widgets.",
        DebugUiThemeGroup::ImGuiGallery,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::CrimsonVesuvius,
        "Crimson Vesuvius",
        "CrimsonVesuvius",
        "A dense charcoal-and-crimson theme with restrained rounding.",
        DebugUiThemeGroup::ImGuiGallery,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::Cyberpunk,
        "Cyberpunk",
        "Cyberpunk",
        "Hard square edges, neon cyan, and magenta accents.",
        DebugUiThemeGroup::ImGuiGallery,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::PaperAndInk,
        "Paper & Ink",
        "PaperAndInk",
        "A bordered warm-paper theme with dark ink and blue accents.",
        DebugUiThemeGroup::ImGuiGallery,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::RoseQuartz,
        "Rose Quartz",
        "RoseQuartz",
        "Generous spacing, pill-like controls, and muted rose accents.",
        DebugUiThemeGroup::ImGuiGallery,
    },
    DebugUiThemeDefinition {
        DebugUiTheme::NuklearGray,
        "Nuklear Gray",
        "NuklearGray",
        "A compact industrial gray skin with subtle warm highlights.",
        DebugUiThemeGroup::ImGuiGallery,
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

struct DebugUiStyleProfile {
    ImVec2 windowPadding { 8.0f, 8.0f };
    ImVec2 framePadding { 5.0f, 3.0f };
    ImVec2 cellPadding { 6.0f, 4.0f };
    ImVec2 itemSpacing { 6.0f, 4.0f };
    ImVec2 itemInnerSpacing { 6.0f, 4.0f };
    float indentSpacing = 20.0f;
    float scrollbarSize = 13.0f;
    float grabMinSize = 10.0f;
    float windowRounding = 4.0f;
    float childRounding = 3.0f;
    float popupRounding = 3.0f;
    float frameRounding = 3.0f;
    float scrollbarRounding = 9.0f;
    float grabRounding = 3.0f;
    float tabRounding = 3.0f;
    float windowBorderSize = 1.0f;
    float childBorderSize = 1.0f;
    float popupBorderSize = 1.0f;
    float frameBorderSize = 0.0f;
    float tabBorderSize = 0.0f;
    float tabBarBorderSize = 1.0f;
    float tabBarOverlineSize = 2.0f;
    float dockingSeparatorSize = 1.0f;
    ImVec2 windowTitleAlign { 0.0f, 0.5f };
    ImGuiDir windowMenuButtonPosition = ImGuiDir_Left;
};

void applyStyleProfile(
    ImGuiStyle& style,
    const DebugUiStyleProfile& profile)
{
    style.WindowPadding = profile.windowPadding;
    style.FramePadding = profile.framePadding;
    style.CellPadding = profile.cellPadding;
    style.ItemSpacing = profile.itemSpacing;
    style.ItemInnerSpacing = profile.itemInnerSpacing;
    style.IndentSpacing = profile.indentSpacing;
    style.ScrollbarSize = profile.scrollbarSize;
    style.GrabMinSize = profile.grabMinSize;
    style.WindowRounding = profile.windowRounding;
    style.ChildRounding = profile.childRounding;
    style.PopupRounding = profile.popupRounding;
    style.FrameRounding = profile.frameRounding;
    style.ScrollbarRounding = profile.scrollbarRounding;
    style.GrabRounding = profile.grabRounding;
    style.TabRounding = profile.tabRounding;
    style.WindowBorderSize = profile.windowBorderSize;
    style.ChildBorderSize = profile.childBorderSize;
    style.PopupBorderSize = profile.popupBorderSize;
    style.FrameBorderSize = profile.frameBorderSize;
    style.TabBorderSize = profile.tabBorderSize;
    style.TabBarBorderSize = profile.tabBarBorderSize;
    style.TabBarOverlineSize = profile.tabBarOverlineSize;
    style.DockingSeparatorSize = profile.dockingSeparatorSize;
    style.WindowTitleAlign = profile.windowTitleAlign;
    style.WindowMenuButtonPosition = profile.windowMenuButtonPosition;
}

// Community themes are adapted to the current ImGui style fields from:
// https://github.com/ocornut/imgui/issues/707
void applyThemeGeometry(ImGuiStyle& style, DebugUiTheme theme)
{
    DebugUiStyleProfile profile;
    switch (theme) {
    case DebugUiTheme::Dark:
    case DebugUiTheme::Light:
    case DebugUiTheme::Classic:
        return;
    case DebugUiTheme::HighContrast:
        profile.windowPadding = { 8.0f, 8.0f };
        profile.framePadding = { 6.0f, 4.0f };
        profile.itemSpacing = { 8.0f, 5.0f };
        profile.windowRounding = 0.0f;
        profile.childRounding = 0.0f;
        profile.popupRounding = 0.0f;
        profile.frameRounding = 0.0f;
        profile.scrollbarRounding = 0.0f;
        profile.grabRounding = 0.0f;
        profile.tabRounding = 0.0f;
        profile.frameBorderSize = 1.0f;
        break;
    case DebugUiTheme::Dracula:
        profile.windowPadding = { 10.0f, 10.0f };
        profile.framePadding = { 6.0f, 4.0f };
        profile.itemSpacing = { 8.0f, 6.0f };
        profile.scrollbarSize = 14.0f;
        profile.grabMinSize = 12.0f;
        profile.windowRounding = 6.0f;
        profile.childRounding = 4.0f;
        profile.popupRounding = 4.0f;
        profile.frameRounding = 4.0f;
        profile.scrollbarRounding = 12.0f;
        profile.grabRounding = 4.0f;
        profile.tabRounding = 4.0f;
        profile.frameBorderSize = 1.0f;
        break;
    case DebugUiTheme::Nord:
        profile.windowRounding = 5.0f;
        profile.childRounding = 4.0f;
        profile.frameRounding = 3.0f;
        profile.popupRounding = 4.0f;
        profile.grabRounding = 3.0f;
        profile.tabRounding = 4.0f;
        break;
    case DebugUiTheme::CatppuccinMocha:
        profile.windowPadding = { 12.0f, 12.0f };
        profile.framePadding = { 6.0f, 4.0f };
        profile.itemSpacing = { 8.0f, 6.0f };
        profile.scrollbarSize = 14.0f;
        profile.grabMinSize = 12.0f;
        profile.windowRounding = 8.0f;
        profile.childRounding = 5.0f;
        profile.popupRounding = 5.0f;
        profile.frameRounding = 5.0f;
        profile.scrollbarRounding = 12.0f;
        profile.grabRounding = 5.0f;
        profile.tabRounding = 5.0f;
        break;
    case DebugUiTheme::GruvboxDark:
        profile.windowPadding = { 10.0f, 10.0f };
        profile.framePadding = { 6.0f, 4.0f };
        profile.itemSpacing = { 8.0f, 4.0f };
        profile.scrollbarSize = 14.0f;
        profile.grabMinSize = 12.0f;
        profile.windowRounding = 2.0f;
        profile.childRounding = 2.0f;
        profile.popupRounding = 2.0f;
        profile.frameRounding = 2.0f;
        profile.scrollbarRounding = 2.0f;
        profile.grabRounding = 2.0f;
        profile.tabRounding = 2.0f;
        profile.frameBorderSize = 1.0f;
        break;
    case DebugUiTheme::SolarizedDark:
    case DebugUiTheme::SolarizedLight:
        profile.windowPadding = { 9.0f, 9.0f };
        profile.framePadding = { 6.0f, 4.0f };
        profile.itemSpacing = { 7.0f, 5.0f };
        profile.windowRounding = 3.0f;
        profile.childRounding = 3.0f;
        profile.popupRounding = 3.0f;
        profile.frameRounding = 3.0f;
        profile.scrollbarRounding = 3.0f;
        profile.grabRounding = 3.0f;
        profile.tabRounding = 3.0f;
        break;
    case DebugUiTheme::TokyoNight:
        profile.windowPadding = { 9.0f, 9.0f };
        profile.framePadding = { 6.0f, 4.0f };
        profile.itemSpacing = { 8.0f, 5.0f };
        profile.windowRounding = 5.0f;
        profile.childRounding = 4.0f;
        profile.popupRounding = 4.0f;
        profile.frameRounding = 3.0f;
        profile.grabRounding = 3.0f;
        profile.tabRounding = 4.0f;
        break;
    case DebugUiTheme::RosePine:
        profile.windowPadding = { 11.0f, 11.0f };
        profile.framePadding = { 6.0f, 4.0f };
        profile.itemSpacing = { 8.0f, 6.0f };
        profile.scrollbarSize = 14.0f;
        profile.grabMinSize = 12.0f;
        profile.windowRounding = 8.0f;
        profile.childRounding = 5.0f;
        profile.popupRounding = 5.0f;
        profile.frameRounding = 5.0f;
        profile.scrollbarRounding = 12.0f;
        profile.grabRounding = 5.0f;
        profile.tabRounding = 5.0f;
        break;
    case DebugUiTheme::OneDark:
        profile.windowRounding = 3.0f;
        profile.childRounding = 3.0f;
        profile.popupRounding = 3.0f;
        profile.frameRounding = 3.0f;
        profile.scrollbarRounding = 3.0f;
        profile.grabRounding = 3.0f;
        profile.tabRounding = 3.0f;
        profile.frameBorderSize = 1.0f;
        profile.dockingSeparatorSize = 3.0f;
        break;
    case DebugUiTheme::Everforest:
        profile.windowPadding = { 10.0f, 10.0f };
        profile.framePadding = { 6.0f, 4.0f };
        profile.itemSpacing = { 8.0f, 6.0f };
        profile.windowRounding = 6.0f;
        profile.childRounding = 4.0f;
        profile.popupRounding = 4.0f;
        profile.frameRounding = 4.0f;
        profile.scrollbarRounding = 6.0f;
        profile.grabRounding = 4.0f;
        profile.tabRounding = 4.0f;
        break;
    case DebugUiTheme::DeepBlue:
        profile.windowRounding = 5.3f;
        profile.childRounding = 3.0f;
        profile.popupRounding = 3.0f;
        profile.frameRounding = 2.3f;
        profile.scrollbarRounding = 0.0f;
        profile.grabRounding = 2.3f;
        profile.tabRounding = 2.3f;
        break;
    case DebugUiTheme::BlackGreen:
        profile.scrollbarSize = 10.0f;
        profile.grabMinSize = 10.0f;
        profile.windowRounding = 4.0f;
        profile.childRounding = 4.0f;
        profile.popupRounding = 4.0f;
        profile.frameRounding = 4.0f;
        profile.scrollbarRounding = 4.0f;
        profile.grabRounding = 3.0f;
        profile.tabRounding = 4.0f;
        profile.windowMenuButtonPosition = ImGuiDir_Right;
        break;
    case DebugUiTheme::ForestGreen:
        profile.windowPadding = { 10.0f, 10.0f };
        profile.framePadding = { 6.0f, 4.0f };
        profile.itemSpacing = { 8.0f, 6.0f };
        profile.scrollbarSize = 14.0f;
        profile.grabMinSize = 12.0f;
        profile.windowRounding = 6.0f;
        profile.childRounding = 4.0f;
        profile.popupRounding = 4.0f;
        profile.frameRounding = 4.0f;
        profile.scrollbarRounding = 4.0f;
        profile.grabRounding = 4.0f;
        profile.tabRounding = 4.0f;
        profile.frameBorderSize = 1.0f;
        break;
    case DebugUiTheme::Amethyst:
        profile.windowPadding = { 11.0f, 10.0f };
        profile.framePadding = { 7.0f, 4.0f };
        profile.itemSpacing = { 8.0f, 6.0f };
        profile.windowRounding = 7.0f;
        profile.childRounding = 5.0f;
        profile.popupRounding = 5.0f;
        profile.frameRounding = 5.0f;
        profile.scrollbarRounding = 8.0f;
        profile.grabRounding = 5.0f;
        profile.tabRounding = 5.0f;
        break;
    case DebugUiTheme::Sapphire:
        profile.windowPadding = { 9.0f, 9.0f };
        profile.framePadding = { 6.0f, 4.0f };
        profile.itemSpacing = { 7.0f, 5.0f };
        profile.windowRounding = 5.0f;
        profile.childRounding = 3.0f;
        profile.popupRounding = 4.0f;
        profile.frameRounding = 3.0f;
        profile.scrollbarRounding = 6.0f;
        profile.grabRounding = 3.0f;
        profile.tabRounding = 3.0f;
        break;
    case DebugUiTheme::Amber:
        profile.windowPadding = { 8.0f, 8.0f };
        profile.framePadding = { 6.0f, 3.0f };
        profile.itemSpacing = { 7.0f, 4.0f };
        profile.windowRounding = 4.0f;
        profile.childRounding = 3.0f;
        profile.popupRounding = 3.0f;
        profile.frameRounding = 3.0f;
        profile.scrollbarRounding = 4.0f;
        profile.grabRounding = 3.0f;
        profile.tabRounding = 3.0f;
        break;
    case DebugUiTheme::CrimsonVesuvius:
        profile.windowPadding = { 10.0f, 10.0f };
        profile.framePadding = { 5.0f, 3.0f };
        profile.itemSpacing = { 8.0f, 4.0f };
        profile.scrollbarSize = 13.0f;
        profile.grabMinSize = 10.0f;
        profile.windowRounding = 3.0f;
        profile.childRounding = 2.0f;
        profile.popupRounding = 2.0f;
        profile.frameRounding = 2.0f;
        profile.scrollbarRounding = 12.0f;
        profile.grabRounding = 2.0f;
        profile.tabRounding = 3.0f;
        profile.frameBorderSize = 1.0f;
        break;
    case DebugUiTheme::Cyberpunk:
        profile.windowPadding = { 10.0f, 10.0f };
        profile.framePadding = { 6.0f, 4.0f };
        profile.itemSpacing = { 8.0f, 4.0f };
        profile.windowRounding = 0.0f;
        profile.childRounding = 0.0f;
        profile.popupRounding = 0.0f;
        profile.frameRounding = 0.0f;
        profile.scrollbarRounding = 0.0f;
        profile.grabRounding = 0.0f;
        profile.tabRounding = 0.0f;
        profile.frameBorderSize = 1.0f;
        break;
    case DebugUiTheme::PaperAndInk:
        profile.windowPadding = { 12.0f, 12.0f };
        profile.framePadding = { 6.0f, 4.0f };
        profile.itemSpacing = { 8.0f, 6.0f };
        profile.scrollbarSize = 14.0f;
        profile.grabMinSize = 12.0f;
        profile.windowRounding = 2.0f;
        profile.childRounding = 2.0f;
        profile.popupRounding = 2.0f;
        profile.frameRounding = 2.0f;
        profile.scrollbarRounding = 12.0f;
        profile.grabRounding = 2.0f;
        profile.tabRounding = 2.0f;
        profile.frameBorderSize = 1.0f;
        profile.tabBorderSize = 1.0f;
        break;
    case DebugUiTheme::RoseQuartz:
        profile.windowPadding = { 10.0f, 10.0f };
        profile.framePadding = { 6.0f, 4.0f };
        profile.itemSpacing = { 8.0f, 5.0f };
        profile.scrollbarSize = 14.0f;
        profile.grabMinSize = 12.0f;
        profile.windowRounding = 10.0f;
        profile.childRounding = 6.0f;
        profile.popupRounding = 6.0f;
        profile.frameRounding = 6.0f;
        profile.scrollbarRounding = 12.0f;
        profile.grabRounding = 6.0f;
        profile.tabRounding = 6.0f;
        break;
    case DebugUiTheme::NuklearGray:
        profile.windowPadding = { 8.0f, 8.0f };
        profile.framePadding = { 5.0f, 3.0f };
        profile.itemSpacing = { 6.0f, 4.0f };
        profile.windowRounding = 2.0f;
        profile.childRounding = 2.0f;
        profile.popupRounding = 2.0f;
        profile.frameRounding = 2.0f;
        profile.scrollbarRounding = 2.0f;
        profile.grabRounding = 2.0f;
        profile.tabRounding = 2.0f;
        profile.frameBorderSize = 1.0f;
        break;
    }
    applyStyleProfile(style, profile);
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
    case DebugUiTheme::DeepBlue:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xf2f5f8),
            .textDisabled = color(0x8294a6),
            .window = color(0x101722),
            .child = color(0x0b111a),
            .popup = color(0x162536),
            .border = color(0x2d4f6c),
            .surface = color(0x1b3044),
            .surfaceHovered = color(0x245477),
            .surfaceActive = color(0x3279a8),
            .accent = color(0x65b9eb),
            .accentHovered = color(0x8bd1ff),
            .accentActive = color(0x3d90c2),
            .warning = color(0xe6b450),
            .positive = color(0x69c796),
        });
        return;
    case DebugUiTheme::BlackGreen:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xe8f5eb),
            .textDisabled = color(0x68816f),
            .window = color(0x050806),
            .child = color(0x09100b),
            .popup = color(0x0d1710),
            .border = color(0x244d2d),
            .surface = color(0x102517),
            .surfaceHovered = color(0x173921),
            .surfaceActive = color(0x205c2d),
            .accent = color(0x46e36f),
            .accentHovered = color(0x75f293),
            .accentActive = color(0xb2ffc3),
            .warning = color(0xffd166),
            .positive = color(0x6dea8d),
        });
        return;
    case DebugUiTheme::ForestGreen:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xdce8d8),
            .textDisabled = color(0x71806f),
            .window = color(0x152019),
            .child = color(0x101712),
            .popup = color(0x1c2b21),
            .border = color(0x405544),
            .surface = color(0x24372a),
            .surfaceHovered = color(0x31513b),
            .surfaceActive = color(0x3f6b4c),
            .accent = color(0x72b982),
            .accentHovered = color(0x91cf9d),
            .accentActive = color(0xb2ddb5),
            .warning = color(0xd9ba73),
            .positive = color(0x80c78b),
        });
        return;
    case DebugUiTheme::Amethyst:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xeee8f7),
            .textDisabled = color(0x8b7d9d),
            .window = color(0x1e1727),
            .child = color(0x17111f),
            .popup = color(0x291f35),
            .border = color(0x58456d),
            .surface = color(0x332642),
            .surfaceHovered = color(0x49345e),
            .surfaceActive = color(0x62427d),
            .accent = color(0xbd82e6),
            .accentHovered = color(0xd4a6f2),
            .accentActive = color(0x9f62cc),
            .warning = color(0xf1bd6b),
            .positive = color(0x83cf9a),
        });
        return;
    case DebugUiTheme::Sapphire:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xe8f0fa),
            .textDisabled = color(0x788aa1),
            .window = color(0x111a27),
            .child = color(0x0c131d),
            .popup = color(0x192638),
            .border = color(0x385475),
            .surface = color(0x20334a),
            .surfaceHovered = color(0x294b70),
            .surfaceActive = color(0x32689d),
            .accent = color(0x4da3ff),
            .accentHovered = color(0x78bdff),
            .accentActive = color(0x267fd4),
            .warning = color(0xf0b35a),
            .positive = color(0x6dcc9a),
        });
        return;
    case DebugUiTheme::Amber:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xf0e6d2),
            .textDisabled = color(0x8d806b),
            .window = color(0x211c16),
            .child = color(0x191510),
            .popup = color(0x2b241b),
            .border = color(0x5c4b34),
            .surface = color(0x392f22),
            .surfaceHovered = color(0x554329),
            .surfaceActive = color(0x73552b),
            .accent = color(0xf0a83a),
            .accentHovered = color(0xffc45f),
            .accentActive = color(0xd88922),
            .warning = color(0xffd166),
            .positive = color(0x9bcf75),
        });
        return;
    case DebugUiTheme::CrimsonVesuvius:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xf2e9e9),
            .textDisabled = color(0x8b7375),
            .window = color(0x1d1718),
            .child = color(0x151112),
            .popup = color(0x291f20),
            .border = color(0x62373c),
            .surface = color(0x352326),
            .surfaceHovered = color(0x542d33),
            .surfaceActive = color(0x76343d),
            .accent = color(0xd94a5b),
            .accentHovered = color(0xef6a78),
            .accentActive = color(0xb92f42),
            .warning = color(0xe8a34a),
            .positive = color(0x79c98a),
        });
        return;
    case DebugUiTheme::Cyberpunk:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xeafcff),
            .textDisabled = color(0x637880),
            .window = color(0x080b10),
            .child = color(0x05070b),
            .popup = color(0x10151d),
            .border = color(0x174e5c),
            .surface = color(0x111d26),
            .surfaceHovered = color(0x123746),
            .surfaceActive = color(0x214f60),
            .accent = color(0x00e5ff),
            .accentHovered = color(0xff3cac),
            .accentActive = color(0xb967ff),
            .warning = color(0xffd319),
            .positive = color(0x38f89d),
        });
        return;
    case DebugUiTheme::PaperAndInk:
        ImGui::StyleColorsLight(&style);
        applyPalette(style, {
            .text = color(0x282620),
            .textDisabled = color(0x847f73),
            .window = color(0xf4efdf),
            .child = color(0xe9e2cf),
            .popup = color(0xfffbef),
            .border = color(0x6f6a5f),
            .surface = color(0xe2dac5),
            .surfaceHovered = color(0xcad8df),
            .surfaceActive = color(0xaec4d0),
            .accent = color(0x26658c),
            .accentHovered = color(0x347fa9),
            .accentActive = color(0x174d70),
            .warning = color(0xb56b22),
            .positive = color(0x4f7c4a),
        });
        return;
    case DebugUiTheme::RoseQuartz:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xf3e8ed),
            .textDisabled = color(0x9a7f8b),
            .window = color(0x241b22),
            .child = color(0x1b151a),
            .popup = color(0x30242d),
            .border = color(0x604552),
            .surface = color(0x3a2a34),
            .surfaceHovered = color(0x533845),
            .surfaceActive = color(0x714958),
            .accent = color(0xe59aaa),
            .accentHovered = color(0xf2b6c2),
            .accentActive = color(0xc8778b),
            .warning = color(0xe9b66d),
            .positive = color(0x8bc7a2),
        });
        return;
    case DebugUiTheme::NuklearGray:
        ImGui::StyleColorsDark(&style);
        applyPalette(style, {
            .text = color(0xe4e4e4),
            .textDisabled = color(0x858585),
            .window = color(0x252525),
            .child = color(0x1d1d1d),
            .popup = color(0x303030),
            .border = color(0x555555),
            .surface = color(0x3a3a3a),
            .surfaceHovered = color(0x505050),
            .surfaceActive = color(0x686868),
            .accent = color(0xd99a4e),
            .accentHovered = color(0xf0b96c),
            .accentActive = color(0xbc7a30),
            .warning = color(0xe5b65b),
            .positive = color(0x83b97b),
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
    applyThemeGeometry(appearance, state.theme);
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
    const auto drawGroup = [&](const char* label, DebugUiThemeGroup group) {
        if (!ImGui::BeginMenu(label)) {
            return;
        }

        for (const DebugUiThemeDefinition& definition : debugUiThemes) {
            if (definition.group != group) {
                continue;
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
        ImGui::EndMenu();
    };

    drawGroup("Built-in", DebugUiThemeGroup::BuiltIn);
    drawGroup("Curated", DebugUiThemeGroup::Curated);
    drawGroup("ImGui Gallery", DebugUiThemeGroup::ImGuiGallery);
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
