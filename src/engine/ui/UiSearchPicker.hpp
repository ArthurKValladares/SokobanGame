#pragma once

#include "engine/ui/UiDialog.hpp"
#include "engine/ui/UiScroll.hpp"
#include "engine/ui/UiTextEdit.hpp"
#include "engine/ui/UiTextField.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sokoban {

struct UiSearchPickerOptions {
    std::string_view scope;
    std::string_view title;
    std::string_view submitLabel;
    std::span<const std::string> choices;
    std::string_view currentChoice;
    std::string_view emptyChoiceLabel;
    std::string_view description;
    std::string_view placeholder;
    std::string_view emptyMessage;
    std::string_view noMatchesMessage;
    std::string_view error;
    std::string_view rowRenameLabel;
    std::string_view rowDeleteLabel;
    bool selectEmptyChoiceInitially = false;
};

struct UiSearchPickerInput {
    UiNavigation navigation;
    std::optional<Vec2> wheelPosition;
    float wheelY = 0.0f;
    bool extendSelection = false;
    bool interactive = true;
    int rowActionStep = 0;
};

struct UiSearchPickerResult {
    enum class Command : uint8_t { None, Cancel, Choose, Rename, Delete };
    Command command = Command::None;
    // Valid for the lifetime of the choices supplied to draw(). An empty
    // value can be explicitly chosen when emptyChoiceLabel is supplied.
    std::string_view choice;
};

class UiSearchPicker {
public:
    void open(std::string_view initialChoice = {});
    void focusSearch();
    void blurSearch();
    [[nodiscard]] bool searchFocused() const { return searchFocused_; }
    [[nodiscard]] UiTextEdit* activeTextEdit() { return searchFocused_ ? &searchEdit_ : nullptr; }
    [[nodiscard]] UiRect caret() const { return caret_; }
    [[nodiscard]] bool handleSearchPress(UiContext& ui, Vec2 position, bool extendSelection);
    [[nodiscard]] UiSearchPickerResult draw(UiContext& ui, const UiDialogLayout& layout,
        const UiSearchPickerOptions& options, const UiSearchPickerInput& input);

private:
    UiTextField searchField_;
    UiTextEdit searchEdit_ { 128 };
    UiScroll scroll_;
    UiRect searchBounds_;
    UiRect caret_;
    float searchTextSize_ = 0.0f;
    UiControlId focus_ = 0;
    std::size_t selection_ = 0;
    std::string lastQuery_;
    std::string initialChoice_;
    bool initializeSelection_ = true;
    bool searchFocused_ = false;
    bool searchPressHandled_ = false;
};

} // namespace sokoban
