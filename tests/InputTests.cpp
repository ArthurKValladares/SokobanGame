#include "engine/Input.hpp"

#include <SDL3/SDL.h>

#include <iostream>

namespace {

int failures = 0;
int checks = 0;

void checkImpl(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

SDL_Event keyEvent(Uint32 type, SDL_Scancode scancode)
{
    SDL_Event event {};
    event.type = type;
    event.key.scancode = scancode;
    return event;
}

SDL_Event gamepadButtonEvent(Uint32 type, SDL_GamepadButton button)
{
    SDL_Event event {};
    event.type = type;
    event.gbutton.which = 42;
    event.gbutton.button = static_cast<Uint8>(button);
    return event;
}

SDL_Event gamepadAxisEvent(SDL_GamepadAxis axis, Sint16 value)
{
    SDL_Event event {};
    event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    event.gaxis.which = 42;
    event.gaxis.axis = static_cast<Uint8>(axis);
    event.gaxis.value = value;
    return event;
}

void testDefaultKeyboardBindings()
{
    sokoban::InputState input(false);
    CHECK(input.invalidBindingCount() == 0);
    CHECK(input.keyBoundToAction(SDL_SCANCODE_W, sokoban::InputAction::MoveUp));

    input.beginFrame();
    input.handleEvent(keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W));
    CHECK(input.actionPressed(sokoban::InputAction::MoveUp));
    CHECK(input.actionDown(sokoban::InputAction::MoveUp));
    CHECK(input.activeDevice() == sokoban::ActiveInputDevice::KeyboardMouse);

    input.beginFrame();
    CHECK(!input.actionPressed(sokoban::InputAction::MoveUp));
    CHECK(input.actionDown(sokoban::InputAction::MoveUp));
    input.handleEvent(keyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_W));
    CHECK(!input.actionDown(sokoban::InputAction::MoveUp));
}

void testMenuConfirmBindings()
{
    sokoban::InputState input(false);
    input.beginFrame();
    input.handleEvent(keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_RETURN));
    CHECK(input.actionPressed(sokoban::InputAction::MenuConfirm));

    input.beginFrame();
    input.handleEvent(keyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_RETURN));
    input.handleEvent(gamepadButtonEvent(
        SDL_EVENT_GAMEPAD_BUTTON_DOWN,
        SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK(input.actionPressed(sokoban::InputAction::MenuConfirm));
}

void testKeyboardRemapping()
{
    sokoban::InputBindings bindings = sokoban::defaultInputBindings();
    bindings.forAction(sokoban::InputAction::Undo) = {
        sokoban::KeyboardBinding { "Backspace" },
    };
    sokoban::InputState input(false);
    input.setBindings(bindings);

    input.beginFrame();
    input.handleEvent(keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_Z));
    CHECK(!input.actionPressed(sokoban::InputAction::Undo));
    input.handleEvent(keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_BACKSPACE));
    CHECK(input.actionPressed(sokoban::InputAction::Undo));
}

void testGamepadButtonsAndRemapping()
{
    sokoban::InputState input(false);
    input.beginFrame();
    input.handleEvent(gamepadButtonEvent(
        SDL_EVENT_GAMEPAD_BUTTON_DOWN,
        SDL_GAMEPAD_BUTTON_WEST));
    CHECK(input.actionPressed(sokoban::InputAction::Undo));
    CHECK(input.actionDown(sokoban::InputAction::Undo));
    CHECK(input.activeDevice() == sokoban::ActiveInputDevice::Gamepad);

    input.beginFrame();
    CHECK(!input.actionPressed(sokoban::InputAction::Undo));
    input.handleEvent(gamepadButtonEvent(
        SDL_EVENT_GAMEPAD_BUTTON_UP,
        SDL_GAMEPAD_BUTTON_WEST));
    CHECK(!input.actionDown(sokoban::InputAction::Undo));

    sokoban::InputBindings remapped = sokoban::defaultInputBindings();
    remapped.forAction(sokoban::InputAction::Undo) = {
        sokoban::GamepadButtonBinding { "south" },
    };
    input.setBindings(remapped);
    input.beginFrame();
    input.handleEvent(gamepadButtonEvent(
        SDL_EVENT_GAMEPAD_BUTTON_DOWN,
        SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK(input.actionPressed(sokoban::InputAction::Undo));
}

void testStickThresholdAndPressEdges()
{
    sokoban::InputState input(false);
    input.beginFrame();
    input.handleEvent(gamepadAxisEvent(SDL_GAMEPAD_AXIS_LEFTX, -12000));
    CHECK(!input.actionDown(sokoban::InputAction::MoveLeft));
    input.handleEvent(gamepadAxisEvent(SDL_GAMEPAD_AXIS_LEFTX, -22000));
    CHECK(input.actionPressed(sokoban::InputAction::MoveLeft));
    CHECK(input.actionDown(sokoban::InputAction::MoveLeft));

    input.beginFrame();
    CHECK(!input.actionPressed(sokoban::InputAction::MoveLeft));
    CHECK(input.actionDown(sokoban::InputAction::MoveLeft));
    input.handleEvent(gamepadAxisEvent(SDL_GAMEPAD_AXIS_LEFTX, 0));
    CHECK(!input.actionDown(sokoban::InputAction::MoveLeft));

    input.beginFrame();
    input.handleEvent(gamepadAxisEvent(SDL_GAMEPAD_AXIS_LEFTX, 24000));
    CHECK(input.actionPressed(sokoban::InputAction::MoveRight));
    CHECK(input.actionDown(sokoban::InputAction::MoveRight));
}

void testInvalidBindingIsDiagnosed()
{
    sokoban::InputBindings bindings = sokoban::defaultInputBindings();
    bindings.forAction(sokoban::InputAction::Restart) = {
        sokoban::GamepadButtonBinding { "not-a-button" },
    };
    sokoban::InputState input(false);
    input.setBindings(bindings);
    CHECK(input.invalidBindingCount() == 1);
    CHECK(!input.actionDown(sokoban::InputAction::Restart));
}

void testBindingCaptureCandidates()
{
    const std::optional<sokoban::InputBinding> keyboard =
        sokoban::InputState::bindingCandidate(
            keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_BACKSPACE));
    CHECK(keyboard.has_value());
    CHECK(std::get<sokoban::KeyboardBinding>(*keyboard).scancode == "Backspace");

    const std::optional<sokoban::InputBinding> button =
        sokoban::InputState::bindingCandidate(gamepadButtonEvent(
            SDL_EVENT_GAMEPAD_BUTTON_DOWN,
            SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK(button.has_value());
    CHECK(std::get<sokoban::GamepadButtonBinding>(*button).button == "south");

    CHECK(!sokoban::InputState::bindingCandidate(
        gamepadAxisEvent(SDL_GAMEPAD_AXIS_LEFTY, -12000)).has_value());
    const std::optional<sokoban::InputBinding> axis =
        sokoban::InputState::bindingCandidate(
            gamepadAxisEvent(SDL_GAMEPAD_AXIS_LEFTY, -30000));
    CHECK(axis.has_value());
    const auto& axisBinding = std::get<sokoban::GamepadAxisBinding>(*axis);
    CHECK(axisBinding.axis == "lefty");
    CHECK(axisBinding.direction == sokoban::AxisDirection::Negative);
}

void testFocusLossClearsHeldActions()
{
    sokoban::InputState input(false);
    input.beginFrame();
    input.handleEvent(keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W));
    CHECK(input.actionDown(sokoban::InputAction::MoveUp));

    SDL_Event focusLost {};
    focusLost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    input.handleEvent(focusLost);
    CHECK(!input.actionDown(sokoban::InputAction::MoveUp));
    CHECK(!input.actionPressed(sokoban::InputAction::MoveUp));
}

} // namespace

int main()
{
    testDefaultKeyboardBindings();
    testMenuConfirmBindings();
    testKeyboardRemapping();
    testGamepadButtonsAndRemapping();
    testStickThresholdAndPressEdges();
    testInvalidBindingIsDiagnosed();
    testBindingCaptureCandidates();
    testFocusLossClearsHeldActions();

    if (failures == 0) {
        std::cout << "InputTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "InputTests: " << failures << " of " << checks << " checks failed\n";
    return 1;
}
