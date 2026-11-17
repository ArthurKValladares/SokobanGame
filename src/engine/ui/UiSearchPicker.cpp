#include "engine/ui/UiSearchPicker.hpp"

#include "engine/ui/UiControls.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace sokoban {
namespace {

constexpr UiControlId cancelId = 1;
constexpr UiControlId submitId = 2;
constexpr UiControlId firstRowId = 3;
constexpr UiControlId controlsPerRow = 3;
constexpr float rowHeight = 36.0f;
constexpr float rowPitch = 41.0f;
constexpr float rowActionWidth = 64.0f;
constexpr float rowActionGap = 6.0f;
constexpr float rowActionEndInset = 5.0f;
constexpr Vec4 ink { 0.94f, 0.95f, 0.90f, 1.0f };
constexpr Vec4 mutedInk { 0.68f, 0.75f, 0.72f, 1.0f };
constexpr Vec4 errorInk { 1.0f, 0.66f, 0.57f, 1.0f };

enum class RowColumn : uint8_t { Choice, Rename, Delete };

constexpr UiControlId rowControl(std::size_t row, RowColumn column = RowColumn::Choice)
{
    return firstRowId + row * controlsPerRow + static_cast<UiControlId>(column);
}

constexpr std::size_t controlRow(UiControlId id)
{
    return static_cast<std::size_t>((id - firstRowId) / controlsPerRow);
}

constexpr RowColumn controlColumn(UiControlId id)
{
    return static_cast<RowColumn>((id - firstRowId) % controlsPerRow);
}

} // namespace

void UiSearchPicker::open(std::string_view initialChoice)
{
    *this = UiSearchPicker {};
    initialChoice_ = initialChoice;
    focusSearch();
}

void UiSearchPicker::focusSearch()
{
    searchFocused_ = true;
    focus_ = 0;
}

void UiSearchPicker::blurSearch()
{
    searchFocused_ = false;
    caret_ = {};
    searchEdit_.clearComposition();
}

bool UiSearchPicker::handleSearchPress(UiContext& ui, Vec2 position, bool extendSelection)
{
    if (searchBounds_.size.x <= 0.0f || searchBounds_.size.y <= 0.0f) return false;
    searchPressHandled_ = true;
    if (ui.contains(searchBounds_, position)) {
        focusSearch();
        searchField_.placeCaret(ui, searchBounds_, searchEdit_, searchTextSize_, position, extendSelection);
        return true;
    }
    const bool wasFocused = searchFocused_;
    blurSearch();
    return wasFocused;
}

UiSearchPickerResult UiSearchPicker::draw(UiContext& ui, const UiDialogLayout& layout,
    const UiSearchPickerOptions& options, const UiSearchPickerInput& input)
{
    using Command = UiSearchPickerResult::Command;
    using Tone = uiControls::ButtonTone;
    const float scale = layout.scale;
    if (scale <= 0.0f) return {};
    layout.draw(ui, options.title);
    if (!options.description.empty()) {
        ui.pushClip({ layout.content.position, { layout.content.size.x, 22.0f * scale } });
        ui.text(layout.content.position, options.description, mutedInk, 15.0f * scale);
        ui.popClip();
    }

    searchBounds_ = { layout.content.position + Vec2 { 0.0f, options.description.empty() ? 0.0f : 24.0f } * scale,
        { layout.content.size.x, 36.0f * scale } };
    searchTextSize_ = 17.0f * scale;
    const bool pressHandled = std::exchange(searchPressHandled_, false);
    if (input.interactive && !pressHandled) {
        if (const auto press = ui.mousePressPosition()) {
            if (ui.contains(searchBounds_, *press)) focusSearch();
            else blurSearch();
        }
    }
    caret_ = searchField_.draw(ui, options.scope, searchBounds_, searchEdit_, {
        .focused = input.interactive && searchFocused_,
        .enabled = input.interactive,
        .extendSelection = input.extendSelection,
        .pressAlreadyHandled = pressHandled,
        .placeholder = options.placeholder,
        .textSize = searchTextSize_,
    }).caret;

    const auto& query = searchEdit_.text();
    std::vector<std::string_view> matches;
    matches.reserve(options.choices.size() + 1);
    if (!options.emptyChoiceLabel.empty() && query.find_first_not_of(" \t") == std::string::npos) {
        matches.emplace_back();
    }
    for (const auto& choice : options.choices) {
        if (matchesSearchTerms(choice, query)) matches.push_back(choice);
    }

    if (lastQuery_ != query) {
        lastQuery_ = query;
        selection_ = 0;
        focus_ = 0;
        scroll_ = {};
        if (input.interactive) ui.cancelPointerInteraction();
    }
    if (std::exchange(initializeSelection_, false)) {
        const std::string_view current = initialChoice_.empty() ? options.currentChoice : initialChoice_;
        const auto selected = current.empty() ? matches.end() : std::ranges::find(matches, current);
        if (selected != matches.end()) selection_ = static_cast<std::size_t>(selected - matches.begin());
        else if (!options.selectEmptyChoiceInitially && matches.size() > 1 && matches.front().empty()) selection_ = 1;
    }

    const auto submit = layout.footerButton(126.0f);
    const auto cancel = layout.footerButton(96.0f, 136.0f);
    const float listTop = searchBounds_.position.y + searchBounds_.size.y + 12.0f * scale;
    const float listBottom = layout.content.position.y + layout.content.size.y -
        (options.error.empty() ? 0.0f : 42.0f * scale);
    const UiRect list { { layout.content.position.x, listTop },
        { layout.content.size.x - 14.0f * scale, std::max(0.0f, listBottom - listTop) } };
    const bool hasMatches = !matches.empty();
    const bool canRename = !options.rowRenameLabel.empty();
    const bool canDelete = !options.rowDeleteLabel.empty();
    const bool hasRowActions = canRename || canDelete;
    const auto availableColumn = [&](std::size_t index, RowColumn column) {
        return column == RowColumn::Choice ||
            (!matches[index].empty() && (column == RowColumn::Rename ? canRename : canDelete));
    };
    const auto rowBounds = [&](std::size_t index) -> UiRect {
        return { list.position + Vec2 { 0.0f, (static_cast<float>(index) * rowPitch - scroll_.offset()) * scale },
            { list.size.x, rowHeight * scale } };
    };
    const auto actionBounds = [&](UiRect row, RowColumn column) -> UiRect {
        const float right = row.position.x + row.size.x - rowActionEndInset * scale;
        const float offset = column == RowColumn::Rename && canDelete ? rowActionWidth + rowActionGap : 0.0f;
        return { { right - (rowActionWidth + offset) * scale, row.position.y + 4.0f * scale },
            { rowActionWidth * scale, (rowHeight - 8.0f) * scale } };
    };
    const auto revealSelection = [&] {
        const float top = static_cast<float>(selection_) * rowPitch;
        scroll_.reveal(top, top + rowHeight);
    };
    scroll_.setExtents(list.size.y / scale, static_cast<float>(matches.size()) * rowPitch);
    UiNavigation navigation = input.navigation;
    int actionStep = input.rowActionStep;
    if (ui.mousePressPosition() || !searchEdit_.composition().text.empty()) {
        navigation = {};
        actionStep = 0;
    }
    if (input.interactive && hasMatches) {
        selection_ = std::min(selection_, matches.size() - 1);
        if (focus_ == 0 || focus_ >= rowControl(matches.size())) {
            focus_ = rowControl(selection_);
            revealSelection();
        }
        scroll_.scrollWheel(ui, list, input.wheelY, input.wheelPosition);
        const auto previousFocus = focus_;
        if (hasRowActions && actionStep != 0 && focus_ >= firstRowId) {
            const auto row = controlRow(focus_);
            const int step = actionStep > 0 ? 1 : -1;
            for (int column = static_cast<int>(controlColumn(focus_)) + step;
                 column >= 0 && column < static_cast<int>(controlsPerRow); column += step) {
                const auto candidate = static_cast<RowColumn>(column);
                if (availableColumn(row, candidate)) {
                    focus_ = rowControl(row, candidate);
                    break;
                }
            }
        } else if (navigation.previous != navigation.next) {
            if (focus_ >= firstRowId) {
                const auto row = static_cast<std::ptrdiff_t>(controlRow(focus_));
                const auto next = row + (navigation.next ? 1 : -1);
                if (next < 0 || next >= static_cast<std::ptrdiff_t>(matches.size())) {
                    focus_ = navigation.next ? cancelId : submitId;
                } else {
                    const auto index = static_cast<std::size_t>(next);
                    const auto column = controlColumn(focus_);
                    focus_ = rowControl(index, availableColumn(index, column) ? column : RowColumn::Choice);
                }
            } else if (focus_ == cancelId) {
                focus_ = navigation.next ? submitId : rowControl(matches.size() - 1);
            } else {
                focus_ = navigation.next ? firstRowId : cancelId;
            }
        }
        if ((focus_ != previousFocus || navigation.activate) && focus_ >= firstRowId) {
            selection_ = controlRow(focus_);
            revealSelection();
        }
        navigation.previous = navigation.next = false;
        scroll_.drawScrollbar(ui, options.scope, {
            { list.position.x + list.size.x + 6.0f * scale, list.position.y }, { 8.0f * scale, list.size.y },
        });
    }
    if (!hasMatches && searchFocused_) navigation.activate = false;

    const auto firstVisible = static_cast<std::size_t>(scroll_.offset() / rowPitch);
    const auto endVisible = std::min(matches.size(),
        firstVisible + static_cast<std::size_t>(list.size.y / (rowPitch * scale)) + 2);
    UiInteraction interaction;
    if (input.interactive) {
        const std::size_t controlsPerVisibleRow = 1 + static_cast<std::size_t>(canRename) + static_cast<std::size_t>(canDelete);
        FrameArray<UiControlRegion> regions(ui.frameArena(), (endVisible - firstVisible) * controlsPerVisibleRow + 2);
        ui.pushClip(list);
        for (std::size_t index = firstVisible; index < endVisible; ++index) {
            const auto row = rowBounds(index);
            regions.push_back({ rowControl(index), ui.visibleRect(row) });
            for (const auto column : { RowColumn::Rename, RowColumn::Delete }) {
                if (availableColumn(index, column)) {
                    regions.push_back({ rowControl(index, column), ui.visibleRect(actionBounds(row, column)) });
                }
            }
        }
        ui.popClip();
        regions.push_back({ cancelId, cancel });
        regions.push_back({ submitId, submit, hasMatches });
        interaction = ui.resolveControls(options.scope, regions, focus_, navigation);
        if (!hasMatches || interaction.pressed != 0 || interaction.activated != 0) {
            // A pointer scroll alone must not change the chosen row.
            focus_ = interaction.focused;
        }
        if (interaction.activated >= firstRowId) {
            selection_ = controlRow(interaction.activated);
        }
    }

    ui.pushClip(list);
    if (!hasMatches && options.error.empty()) {
        ui.text(list.position, options.choices.empty() ? options.emptyMessage : options.noMatchesMessage,
            mutedInk, 16.0f * scale);
    }
    for (std::size_t index = firstVisible; index < endVisible; ++index) {
        const UiRect row = rowBounds(index);
        const Tone tone = index == selection_ ? Tone::Accent : Tone::Normal;
        (void)uiControls::button(ui, interaction, rowControl(index), row, "", tone);
        const Vec2 labelPosition = row.position + Vec2 { 10.0f, 6.0f } * scale;
        float labelRight = row.position.x + row.size.x;
        for (const auto column : { RowColumn::Rename, RowColumn::Delete }) {
            if (!availableColumn(index, column)) continue;
            const auto bounds = actionBounds(row, column);
            const bool rename = column == RowColumn::Rename;
            ui.pushClip(bounds);
            (void)uiControls::button(ui, interaction, rowControl(index, column), bounds, "",
                rename ? Tone::Normal : Tone::Danger);
            ui.centeredText(bounds, rename ? options.rowRenameLabel : options.rowDeleteLabel, ink, 15.0f * scale);
            ui.popClip();
            labelRight = std::min(labelRight, bounds.position.x - rowActionGap * scale);
        }
        ui.pushClip({ { labelPosition.x, row.position.y }, { std::max(0.0f, labelRight - labelPosition.x), row.size.y } });
        ui.text(labelPosition,
            matches[index].empty() ? options.emptyChoiceLabel : matches[index], ink, 17.0f * scale);
        ui.popClip();
    }
    ui.popClip();
    if (!options.error.empty()) {
        const float errorSize = 15.0f * scale;
        const UiRect error { { layout.content.position.x, listBottom + 11.0f * scale },
            { layout.content.size.x, 38.0f * scale } };
        const auto& lines = ui.wrapText(options.error, error.size.x, errorSize);
        ui.pushClip(error);
        for (std::size_t line = 0; line < std::min<std::size_t>(2, lines.size()); ++line) {
            ui.text(error.position + Vec2 { 0.0f, static_cast<float>(line) * errorSize * 1.25f },
                lines[line], errorInk, errorSize);
        }
        ui.popClip();
    }
    (void)uiControls::button(ui, cancel, "Cancel", {
        .focused = interaction.focused == cancelId, .contentScale = 0.76f * scale,
    });
    (void)uiControls::button(ui, submit, options.submitLabel, {
        .tone = Tone::Accent, .focused = interaction.focused == submitId,
        .enabled = hasMatches, .contentScale = 0.76f * scale,
    });
    if (interaction.activated == cancelId) return { .command = Command::Cancel };
    if (hasMatches && interaction.activated == submitId) return { Command::Choose, matches[selection_] };
    if (interaction.activated >= firstRowId) {
        switch (controlColumn(interaction.activated)) {
        case RowColumn::Choice: if (interaction.released == 0) return { Command::Choose, matches[selection_] }; break;
        case RowColumn::Rename: return { Command::Rename, matches[selection_] };
        case RowColumn::Delete: return { Command::Delete, matches[selection_] };
        }
    }
    return {};
}

} // namespace sokoban
