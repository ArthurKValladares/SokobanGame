#pragma once

#include "engine/ui/UiControls.hpp"
#include "engine/ui/UiScroll.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

// Build inspector rows each frame. The view owns its scroll state and maps the
// inspector's stable control IDs to edits in its own model.
class UiPropertyInspector {
public:
    struct Action {
        UiControlId id;
        std::string_view label;
        uiControls::ButtonTone tone = uiControls::ButtonTone::Normal;
        bool enabled = true;
    };

    struct Input {
        UiNavigation navigation;
        float wheelY = 0.0f;
        std::optional<Vec2> wheelPosition;
        bool interactive = true;
    };

    UiPropertyInspector(
        UiContext& ui,
        UiRect viewport,
        float scale,
        UiScroll& scroll);

    void heading(std::string_view text);
    void note(std::string_view text);
    void action(Action action);
    void actionRow(std::span<const Action> actions);

    // Preceding controls share this scope, so keyboard focus can enter the
    // inspector from the rest of the view without resolving input twice.
    [[nodiscard]] UiInteraction resolve(
        std::string_view scope,
        std::string_view scrollbarId,
        std::span<const UiControlRegion> precedingControls,
        UiControlId& focus,
        Input input);
    [[nodiscard]] UiControlId draw(const UiInteraction& interaction) const;

private:
    struct Row {
        UiRect rect;
        std::string label;
        UiControlId id = 0;
        uiControls::ButtonTone tone = uiControls::ButtonTone::Normal;
        bool enabled = true;
        bool heading = false;
    };

    void addText(std::string_view text, bool heading);
    [[nodiscard]] UiRect displayRect(UiRect logical) const;

    UiContext& ui_;
    UiRect viewport_;
    float scale_;
    UiScroll& scroll_;
    float height_ = 0.0f;
    std::vector<Row> rows_;
};

} // namespace sokoban
