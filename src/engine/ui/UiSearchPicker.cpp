#include "engine/ui/UiSearchPicker.hpp"

#include "engine/ui/UiControls.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace sokoban {
namespace {

constexpr UiControlId cancelId = 1;
constexpr UiControlId submitId = 2;
constexpr UiControlId alternateId = 3;
constexpr UiControlId firstRowId = 4;
constexpr float rowHeight = 36.0f;
constexpr float rowPitch = 41.0f;
constexpr Vec4 ink { 0.94f, 0.95f, 0.90f, 1.0f };
constexpr Vec4 mutedInk { 0.68f, 0.75f, 0.72f, 1.0f };
constexpr Vec4 errorInk { 1.0f, 0.66f, 0.57f, 1.0f };

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
        else if (matches.size() > 1 && matches.front().empty()) selection_ = 1;
    }

    const auto submit = layout.footerButton(126.0f);
    const auto cancel = layout.footerButton(96.0f, 136.0f);
    auto alternate = layout.footerButton(96.0f);
    alternate.position.x = layout.content.position.x;
    const float listTop = searchBounds_.position.y + searchBounds_.size.y + 12.0f * scale;
    const float listBottom = layout.content.position.y + layout.content.size.y -
        (options.error.empty() ? 0.0f : 42.0f * scale);
    const UiRect list { { layout.content.position.x, listTop },
        { layout.content.size.x - 14.0f * scale, std::max(0.0f, listBottom - listTop) } };
    const bool hasMatches = !matches.empty();
    const bool canAlternate = !options.alternateLabel.empty() && hasMatches;
    const auto lastRow = firstRowId + matches.size() - 1;
    const auto rowBounds = [&](std::size_t index) -> UiRect {
        return { list.position + Vec2 { 0.0f, (static_cast<float>(index) * rowPitch - scroll_.offset()) * scale },
            { list.size.x, rowHeight * scale } };
    };
    const auto revealSelection = [&] {
        const float top = static_cast<float>(selection_) * rowPitch;
        scroll_.reveal(top, top + rowHeight);
    };
    scroll_.setExtents(list.size.y / scale, static_cast<float>(matches.size()) * rowPitch);
    UiNavigation navigation = input.navigation;
    if (ui.mousePressPosition() || !searchEdit_.composition().text.empty()) navigation = {};
    if (input.interactive && hasMatches) {
        selection_ = std::min(selection_, matches.size() - 1);
        if (focus_ == 0 || focus_ > lastRow) {
            focus_ = firstRowId + selection_;
            revealSelection();
        }
        scroll_.scrollWheel(ui, list, input.wheelY, input.wheelPosition);
        const bool focusMoved = navigation.previous != navigation.next;
        if (focusMoved) {
            focus_ = navigation.next ? (focus_ == lastRow ? cancelId : focus_ + 1)
                                     : (focus_ == cancelId ? lastRow : focus_ - 1);
            if (focus_ == alternateId && !canAlternate) focus_ = navigation.next ? firstRowId : submitId;
        }
        if ((focusMoved || navigation.activate) && focus_ >= firstRowId) {
            selection_ = static_cast<std::size_t>(focus_ - firstRowId);
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
        FrameArray<UiControlRegion> regions(ui.frameArena(), endVisible - firstVisible + 3);
        ui.pushClip(list);
        for (std::size_t index = firstVisible; index < endVisible; ++index) {
            regions.push_back({ firstRowId + index, ui.visibleRect(rowBounds(index)) });
        }
        ui.popClip();
        regions.push_back({ cancelId, cancel });
        regions.push_back({ submitId, submit, hasMatches });
        if (!options.alternateLabel.empty()) regions.push_back({ alternateId, alternate, canAlternate });
        interaction = ui.resolveControls(options.scope, regions, focus_, navigation);
        if (!hasMatches || interaction.pressed != 0 || interaction.activated != 0) {
            // A pointer scroll alone must not change the chosen row.
            focus_ = interaction.focused;
        }
        if (interaction.activated >= firstRowId) {
            selection_ = static_cast<std::size_t>(interaction.activated - firstRowId);
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
        (void)uiControls::button(ui, interaction, firstRowId + index, row, "", tone);
        ui.pushClip(row);
        ui.text(row.position + Vec2 { 10.0f, 6.0f } * scale,
            matches[index].empty() ? options.emptyChoiceLabel : matches[index], ink, 17.0f * scale);
        ui.popClip();
    }
    ui.popClip();
    if (!options.error.empty()) {
        const float errorSize = 15.0f * scale;
        const UiRect error { { layout.content.position.x, listBottom + 11.0f * scale },
            { layout.content.size.x, 38.0f * scale } };
        const auto lines = wrapTextLines(options.error, error.size.x,
            [&](std::string_view line) { return ui.measureText(line, errorSize).x; });
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
    if (!options.alternateLabel.empty()) {
        (void)uiControls::button(ui, alternate, options.alternateLabel, {
            .focused = interaction.focused == alternateId,
            .enabled = canAlternate, .contentScale = 0.76f * scale,
        });
    }
    if (interaction.activated == cancelId) return { .command = Command::Cancel };
    if (canAlternate && interaction.activated == alternateId) return { Command::Alternate, matches[selection_] };
    const bool acceptedRow = interaction.activated >= firstRowId && interaction.released == 0;
    if (hasMatches && (interaction.activated == submitId || acceptedRow)) {
        return { Command::Choose, matches[selection_] };
    }
    return {};
}

} // namespace sokoban
