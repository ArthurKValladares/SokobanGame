// Entry point shared by the test suites linked into one runner executable.
//
// Every suite is written as its own program with its own `int main()`.
// sokoban_add_test compiles each suite's sources with `main` defined to
// sokobanTestSuite_<ctest name> and lists the suite in TestSuites.inc, a
// file generated per runner at configure time. CTest runs one suite per
// process:
//
//     sokoban_tests rules
//     sokoban_tests frame_time_telemetry --benchmark
//
// The suite sees the runner as its program name and everything after its own
// name as its arguments, exactly as it would have as a separate executable,
// and the suite's return value is the process exit code (so a skip code such
// as 77 still reaches CTest).
//
// Suites are never run together in one process. They share global test
// counters (TestHarness.hpp) and some change process-wide state such as the
// working directory or environment variables, and that is safe only because
// each suite gets a fresh process.

#include <array>
#include <cstdio>
#include <string_view>

#define SOKOBAN_TEST_SUITE_NO_ARGS(name) int sokobanTestSuite_##name();
#define SOKOBAN_TEST_SUITE_WITH_ARGS(name) \
    int sokobanTestSuite_##name(int argc, char** argv);
#include "TestSuites.inc"
#undef SOKOBAN_TEST_SUITE_NO_ARGS
#undef SOKOBAN_TEST_SUITE_WITH_ARGS

namespace {

struct Suite {
    std::string_view name;
    int (*run)(int argc, char** argv);
};

#define SOKOBAN_TEST_SUITE_NO_ARGS(name) \
    Suite { #name, [](int, char**) { return sokobanTestSuite_##name(); } },
#define SOKOBAN_TEST_SUITE_WITH_ARGS(name) \
    Suite { #name, [](int argc, char** argv) { \
        return sokobanTestSuite_##name(argc, argv); } },
constexpr std::array suites {
#include "TestSuites.inc"
};
#undef SOKOBAN_TEST_SUITE_NO_ARGS
#undef SOKOBAN_TEST_SUITE_WITH_ARGS

void printSuites(std::FILE* stream)
{
    for (const Suite& suite : suites) {
        std::fprintf(
            stream,
            "  %.*s\n",
            static_cast<int>(suite.name.size()),
            suite.name.data());
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 2 && std::string_view(argv[1]) == "--list") {
        printSuites(stdout);
        return 0;
    }
    if (argc < 2) {
        std::fprintf(
            stderr,
            "usage: %s <suite> [suite arguments...]\n"
            "       %s --list\n"
            "suites:\n",
            argv[0],
            argv[0]);
        printSuites(stderr);
        return 2;
    }

    const std::string_view requested = argv[1];
    for (const Suite& suite : suites) {
        if (suite.name == requested) {
            // Drop the suite name so the suite sees its own arguments at
            // argv[1..], with the runner as argv[0].
            argv[1] = argv[0];
            return suite.run(argc - 1, argv + 1);
        }
    }
    std::fprintf(
        stderr,
        "unknown test suite '%.*s'; available suites:\n",
        static_cast<int>(requested.size()),
        requested.data());
    printSuites(stderr);
    return 2;
}
