#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

int main(int argc, char** argv)
{
    std::filesystem::path saveDirectory;
    std::string_view smokeFrames;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--smoke-frames" && index + 1 < argc) {
            smokeFrames = argv[++index];
        } else if (argument == "--save-directory" && index + 1 < argc) {
            saveDirectory = argv[++index];
        }
    }
    if (smokeFrames.empty() || saveDirectory.empty()) {
        std::cerr << "fixture did not receive the smoke-run contract\n";
        return 31;
    }

    std::filesystem::create_directories(saveDirectory);
    std::ofstream log(saveDirectory / "log.txt");
    log << "fixture received " << smokeFrames << " smoke frames\n";
    log.flush();

    std::string mode = "success";
#ifdef _MSC_VER
    char configuredMode[32] {};
    std::size_t configuredModeLength = 0;
    if (getenv_s(
            &configuredModeLength,
            configuredMode,
            sizeof(configuredMode),
            "SOKOBAN_PACKAGE_VALIDATION_FIXTURE_MODE") == 0 &&
        configuredModeLength > 1) {
        mode = configuredMode;
    }
#else
    if (const char* const configuredMode =
            std::getenv("SOKOBAN_PACKAGE_VALIDATION_FIXTURE_MODE")) {
        mode = configuredMode;
    }
#endif
    if (mode == "failure") {
        log << "fixture initialization failure\n";
        std::cerr << "fixture stderr failure detail\n";
        return 23;
    }
    if (mode == "hang") {
        log << "fixture deliberate hang\n";
        log.flush();
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    std::cout << "fixture completed\n";
    return 0;
}
