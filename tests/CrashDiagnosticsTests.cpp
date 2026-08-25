#include "engine/CrashDiagnostics.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

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

struct TemporaryDirectory {
    TemporaryDirectory()
    {
        path = std::filesystem::temp_directory_path() /
            ("sokoban-crash-diagnostics-tests-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;
};

void testFatalMessageNamesDiagnosticArtifacts()
{
    const std::filesystem::path log = "C:/diagnostics/log.txt";
    const std::filesystem::path dump = "C:/diagnostics/crashes/fatal.dmp";
    const std::string message = sokoban::crash::fatalErrorMessage(
        "graphics device lost", log, dump);

    CHECK(message.find("graphics device lost") != std::string::npos);
    CHECK(message.find(log.string()) != std::string::npos);
    CHECK(message.find(dump.string()) != std::string::npos);
    CHECK(message.find("support report") != std::string::npos);
}

void testCaughtFatalMinidump()
{
    TemporaryDirectory directory;
    sokoban::crash::install(directory.path);
    const auto dump = sokoban::crash::writeMinidump();
#ifdef _WIN32
    CHECK(dump.has_value());
    if (dump) {
        CHECK(std::filesystem::exists(*dump));
        CHECK(std::filesystem::file_size(*dump) > 0);
    }
#else
    CHECK(!dump.has_value());
#endif
}

} // namespace

int main()
{
    testFatalMessageNamesDiagnosticArtifacts();
    testCaughtFatalMinidump();

    if (failures == 0) {
        std::cout << "CrashDiagnosticsTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "CrashDiagnosticsTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
