#include "ScopedTestDirectory.hpp"
#include "TestHarness.hpp"

#include "engine/Tuning.hpp"
#include "engine/render/FogOfWarConfig.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>

namespace {

namespace tuning = sokoban::tuning;

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>() };
}

// Registered the way a tools-build header registers its values, but by hand,
// so the test does not depend on the build's SOKOBAN_ENABLE_DEBUG_UI.
inline constexpr tuning::Section testSection {
    "Test values", "src/engine/TestConfig.hpp", "test" };
float testDensity = 4.25f;
uint32_t testSamples = 20;
sokoban::Vec3 testColor { 0.34f, 0.42f, 0.52f };
const tuning::Registration densityRegistration {
    testSection, "testDensity", &testDensity, 0.0f, 16.0f };
const tuning::Registration samplesRegistration {
    testSection, "testSamples", &testSamples, 1U, 64U };
const tuning::Registration colorRegistration {
    testSection, "testColor", &testColor };

const char* const header = R"(#pragma once
// SOKOBAN_TUNABLE_FLOAT(testSection, testDensity, 1.0f, 0.0f, 2.0f) in a comment
SOKOBAN_TUNING_SECTION(testSection,
    "Test values", "src/engine/TestConfig.hpp", "test");

SOKOBAN_TUNABLE_COLOR3(testSection, testColor, 0.34f, 0.42f, 0.52f);
SOKOBAN_TUNABLE_FLOAT(testSection, testDensity, 4.25f, 0.0f, 16.0f);
SOKOBAN_TUNABLE_FLOAT(testSection, testDensityScale, 4.25f, 0.0f, 16.0f);
SOKOBAN_TUNABLE_UINT(
    testSection,
    testSamples,
    20,
    1,
    64);
)";

tuning::Entry* entryNamed(std::string_view name)
{
    for (tuning::Entry& entry : tuning::entries()) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

void testFloatLiteralsRoundTrip()
{
    TEST("float literals round trip");
    CHECK(tuning::floatLiteral(4.25f) == "4.25f");
    CHECK(tuning::floatLiteral(3.0f) == "3.0f");
    CHECK(tuning::floatLiteral(0.0f) == "0.0f");
    CHECK(tuning::floatLiteral(-0.95f) == "-0.95f");
    CHECK(tuning::floatLiteral(0.1f) == "0.1f");
    const float awkward = 4.2500005f;
    const std::string literal = tuning::floatLiteral(awkward);
    CHECK(std::stof(literal) == awkward);
}

void testRegistrationTracksEdits()
{
    TEST("registration tracks edits and reverts");
    tuning::Entry* density = entryNamed("testDensity");
    CHECK(density != nullptr);
    if (density == nullptr) {
        return;
    }
    CHECK(density->section == &testSection);
    CHECK(density->kind == tuning::Kind::Float);
    CHECK(!tuning::modified(*density));
    testDensity = 5.0f;
    CHECK(tuning::modified(*density));
    tuning::revert(*density);
    CHECK(testDensity == 4.25f);
    CHECK(!tuning::modified(*density));
}

void testRewriteReplacesOnlyTheNamedLiterals()
{
    TEST("rewrite replaces only the named literals");
    const std::string rewritten = tuning::rewriteTunableValues(
        header,
        {
            { "testDensity", { "5.5f" } },
            { "testSamples", { "32" } },
            { "testColor", { "0.1f", "0.2f", "0.3f" } },
        });
    std::string expected = header;
    expected.replace(
        expected.find("testColor, 0.34f, 0.42f, 0.52f"),
        std::string("testColor, 0.34f, 0.42f, 0.52f").size(),
        "testColor, 0.1f, 0.2f, 0.3f");
    expected.replace(
        expected.find("testDensity, 4.25f"),
        std::string("testDensity, 4.25f").size(),
        "testDensity, 5.5f");
    expected.replace(
        expected.find("    20,\n"), std::string("    20,\n").size(), "    32,\n");
    CHECK(rewritten == expected);

    bool threw = false;
    try {
        (void)tuning::rewriteTunableValues(header, { { "missing", { "1.0f" } } });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK_MESSAGE(threw, "an undeclared name is an error, not a silent no-op");
}

void testSaveWritesModifiedValuesAndMarksThemSaved()
{
    TEST("save writes modified values and marks them saved");
    ScopedTestDirectory temp("sokoban-tuning");
    const std::filesystem::path file = temp.path() / "src/engine/TestConfig.hpp";
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << header;

    tuning::SaveResult nothing = tuning::saveToSource(temp.path(), testSection);
    CHECK(nothing.writtenFiles.empty() && nothing.errors.empty());
    CHECK(readFile(file) == header);

    testDensity = 6.75f;
    testColor.y = 0.5f;
    const tuning::SaveResult saved = tuning::saveToSource(temp.path(), testSection);
    CHECK(saved.errors.empty());
    CHECK(saved.writtenFiles.size() == 1);
    const std::string text = readFile(file);
    CHECK(text.find("testDensity, 6.75f, 0.0f, 16.0f") != std::string::npos);
    CHECK(text.find("testDensityScale, 4.25f") != std::string::npos);
    CHECK(text.find("testColor, 0.34f, 0.5f, 0.52f") != std::string::npos);
    CHECK(text.find("    20,\n") != std::string::npos);
    CHECK(!tuning::modified(*entryNamed("testDensity")));
    CHECK(!tuning::modified(*entryNamed("testColor")));

    // Reverting now returns to the saved value, not the original one.
    testDensity = 1.0f;
    tuning::revert(*entryNamed("testDensity"));
    CHECK(testDensity == 6.75f);

    testSamples = 33;
    const tuning::SaveResult missing = tuning::saveToSource(
        temp.path() / "elsewhere", testSection);
    CHECK(!missing.errors.empty());
    CHECK_MESSAGE(tuning::modified(*entryNamed("testSamples")),
        "a failed save leaves the value unsaved");
    testSamples = 20;
}

void testRealHeadersRoundTripUnchanged()
{
    TEST("every registered header rewrites to itself when nothing changed");
#if SOKOBAN_ENABLE_DEBUG_UI
    // Guards the declaration format of the real *Config.hpp headers: the
    // writer must find every tunable and spell its current value exactly
    // as the header does.
    const std::filesystem::path root = SOKOBAN_TEST_SOURCE_DIR;
    std::size_t checkedSections = 0;
    std::vector<const tuning::Section*> sections;
    for (const tuning::Entry& entry : tuning::entries()) {
        if (entry.section != &testSection &&
            std::ranges::find(sections, entry.section) == sections.end()) {
            sections.push_back(entry.section);
        }
    }
    for (const tuning::Section* section : sections) {
        std::vector<std::pair<std::string, std::vector<std::string>>> values;
        for (const tuning::Entry& entry : tuning::entries()) {
            if (entry.section != section) {
                continue;
            }
            std::vector<std::string> literals;
            if (entry.kind == tuning::Kind::Float) {
                literals.push_back(tuning::floatLiteral(entry.saved.x));
            } else if (entry.kind == tuning::Kind::UInt) {
                literals.push_back(std::to_string(
                    static_cast<uint32_t>(entry.saved.x)));
            } else {
                literals = { tuning::floatLiteral(entry.saved.x),
                    tuning::floatLiteral(entry.saved.y),
                    tuning::floatLiteral(entry.saved.z) };
            }
            values.emplace_back(std::string(entry.name), std::move(literals));
        }
        const std::string original =
            readFile(root / std::filesystem::path(section->sourceFile));
        CHECK_MESSAGE(
            tuning::rewriteTunableValues(original, values) == original,
            std::string(section->sourceFile).c_str());
        ++checkedSections;
    }
    CHECK_MESSAGE(checkedSections >= 1, "the fog-of-war section is registered");
    CHECK(sokoban::config::fogOfWarDensity == 4.25f);
#endif
}

} // namespace

int main()
{
    try {
        testFloatLiteralsRoundTrip();
        testRegistrationTracksEdits();
        testRewriteReplacesOnlyTheNamedLiterals();
        testSaveWritesModifiedValuesAndMarksThemSaved();
        testRealHeadersRoundTripUnchanged();
    } catch (const std::exception& error) {
        std::cerr << "UNEXPECTED EXCEPTION: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " tuning checks failed\n";
        return 1;
    }
    std::cout << "All " << checks << " tuning checks passed\n";
    return 0;
}
