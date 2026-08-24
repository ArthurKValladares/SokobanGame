#include "engine/AtomicFile.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
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
        root = std::filesystem::temp_directory_path() /
            ("sokoban_atomic_file_tests_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    std::filesystem::path root;
};

void writeRaw(const std::filesystem::path& path, std::string_view value)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << value;
}

std::string readRaw(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), {} };
}

void testWriteCreatesAndReplacesFiles()
{
    TemporaryDirectory temporary;
    const auto destination = temporary.root / "profile.json";

    sokoban::atomicFile::write(destination, "first");
    CHECK(readRaw(destination) == "first");
    CHECK(!std::filesystem::exists(destination.string() + ".tmp"));

    sokoban::atomicFile::write(destination, "second");
    CHECK(readRaw(destination) == "second");
    CHECK(!std::filesystem::exists(destination.string() + ".tmp"));
    CHECK(!std::filesystem::exists(destination.string() + ".replace-old"));
}

void testReplaceInstallsClosedTemporaryFile()
{
    TemporaryDirectory temporary;
    const auto destination = temporary.root / "settings.json";
    const auto replacement = temporary.root / "settings.json.tmp";
    writeRaw(destination, "old");

    {
        std::ofstream stream(replacement, std::ios::binary | std::ios::trunc);
        stream << "new";
        stream.flush();
        CHECK(static_cast<bool>(stream));
    }

    sokoban::atomicFile::replace(destination, replacement);

    CHECK(readRaw(destination) == "new");
    CHECK(!std::filesystem::exists(replacement));
    CHECK(!std::filesystem::exists(destination.string() + ".replace-old"));
}

void testFailedInstallRestoresDisplacedDestination()
{
    TemporaryDirectory temporary;
    const auto destination = temporary.root / "save.dat";
    const auto missingTemporary = temporary.root / "missing.tmp";
    writeRaw(destination, "original");

    bool threw = false;
    try {
        // A missing source makes both install attempts fail. On platforms
        // that cannot rename over an existing file this also exercises the
        // displace-and-restore fallback; elsewhere the initial rename fails
        // before the same fallback is entered.
        sokoban::atomicFile::replace(destination, missingTemporary);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    CHECK(threw);
    CHECK(readRaw(destination) == "original");
    CHECK(!std::filesystem::exists(destination.string() + ".replace-old"));
}

void testWriteFailureCleansTemporaryFile()
{
    TemporaryDirectory temporary;
    const auto destination = temporary.root / "missing" / "file.txt";
    bool threw = false;
    try {
        sokoban::atomicFile::write(destination, "value");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(!std::filesystem::exists(destination.string() + ".tmp"));
}

void testInjectedPermissionAndDiskFullFailuresPreserveDestination()
{
    TemporaryDirectory temporary;
    const auto destination = temporary.root / "profile.json";
    writeRaw(destination, "committed");

    sokoban::atomicFile::failWriteAfterForTesting(
        0, std::errc::permission_denied);
    bool permissionDenied = false;
    try {
        sokoban::atomicFile::write(destination, "replacement");
    } catch (const std::system_error& error) {
        permissionDenied = error.code() ==
            std::make_error_code(std::errc::permission_denied);
    }
    CHECK(permissionDenied);
    CHECK(readRaw(destination) == "committed");
    CHECK(!std::filesystem::exists(destination.string() + ".tmp"));

    sokoban::atomicFile::failWriteAfterForTesting(
        0, std::errc::no_space_on_device);
    bool diskFull = false;
    try {
        sokoban::atomicFile::write(destination, "replacement");
    } catch (const std::system_error& error) {
        diskFull = error.code() ==
            std::make_error_code(std::errc::no_space_on_device);
    }
    CHECK(diskFull);
    CHECK(readRaw(destination) == "committed");
    CHECK(!std::filesystem::exists(destination.string() + ".tmp"));
}

} // namespace

int main()
{
    testWriteCreatesAndReplacesFiles();
    testReplaceInstallsClosedTemporaryFile();
    testFailedInstallRestoresDisplacedDestination();
    testWriteFailureCleansTemporaryFile();
    testInjectedPermissionAndDiskFullFailuresPreserveDestination();

    if (failures != 0) {
        std::cerr << "AtomicFileTests: " << failures << " failure(s) of "
                  << checks << " checks\n";
        return 1;
    }
    std::cout << "AtomicFileTests: " << checks << " checks passed\n";
    return 0;
}
