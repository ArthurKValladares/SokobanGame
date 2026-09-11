#include "engine/PlayerProfile.hpp"
#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/MenuKit.hpp"
#include "engine/ui/OptionsMenu.hpp"
#include "engine/ui/Ui.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const auto font = sokoban::FontAtlas::load(
        std::filesystem::path(argv[1]) / "assets/ui/Karla-Regular.ttf");
    sokoban::UiContext ui(font);
    const sokoban::OptionsMenuView view;
    {
        const sokoban::OptionsMenuState state {
            .open = true, .page = sokoban::OptionsMenuPage::Graphics};
        sokoban::UserSettings settings;
        settings.video.frameRateLimit = 60;
        ui.beginFrame({1280.0f, 720.0f}, {}, false, false);
        const auto intent = view.draw(ui, {1280.0f, 720.0f}, state, settings);
        if (intent) {
            const auto* choice = std::get_if<sokoban::options::intent::SelectChoice>(&*intent);
            const auto result = sokoban::reduceOptionsMenu(state, settings, *intent);
            const auto* changed = result.action
                ? std::get_if<sokoban::options::SettingsChanged>(&*result.action) : nullptr;
            std::cout << "idle_graphics_frame input_cap=60 emitted_choice="
                      << (choice ? choice->value : -1) << " persisted_cap="
                      << (changed ? changed->settings.video.frameRateLimit : -1) << '\n';
        }
        ui.endFrame();
    }
    for (const auto page : {sokoban::OptionsMenuPage::Graphics,
                            sokoban::OptionsMenuPage::Controls}) {
        for (const float height : {720.0f, 600.0f, 480.0f}) {
            const sokoban::OptionsMenuState state {.open = true, .page = page};
            ui.beginFrame({1280.0f, height}, {}, false, false);
            (void)view.draw(ui, {1280.0f, height}, state, {});
            ui.endFrame();
            float bottom = 0.0f;
            unsigned outside = 0;
            for (const auto& command : ui.drawData().commands) {
                bottom = std::max(bottom, command.rect.position.y + command.rect.size.y);
                if (command.rect.position.y < 0 ||
                    command.rect.position.y + command.rect.size.y > height) ++outside;
            }
            std::cout << "page=" << static_cast<int>(page) << " viewport_height=" << height
                      << " content_bottom=" << bottom << " commands_outside=" << outside << '\n';
        }
    }
    for (const double duration : {61.2, 214748365.0, 1e100}) {
        std::cout << "seconds=" << duration << " formatted="
                  << sokoban::menuKit::formatDuration(duration,
                         sokoban::menuKit::DurationStyle::MinutesSecondsTenths) << '\n';
    }
}
