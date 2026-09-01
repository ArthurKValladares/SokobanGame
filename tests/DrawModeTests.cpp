// Pins the constants that shaders/include/ shares with C++.
//
// Named for the draw-mode numbering it started with; it now also covers the
// point-shadow near plane. Both are values that cross the CPU/GPU boundary as
// plain numbers, where a mismatch is silent and does not look like a mismatch.
//
// The two describe one numeric space that crosses the CPU/GPU boundary, and a
// mismatch is silent: a UI glyph would sample the title background, or a model
// would take the procedural-texture path. Nothing else catches that. The
// gpu_abi suite checks the *layout* of the blocks a shader reads; this checks a
// value the shader compares against, which never reaches SPIR-V in a form worth
// reading back - the optimizer folds it into a branch.
//
// So this reads the GLSL header as text. That is a real coupling to its
// formatting, which is why the parser is strict and says so when it fails
// rather than quietly matching nothing.

#include "TestHarness.hpp"

#include "engine/render/LightingConfig.hpp"
#include "engine/render/VulkanRenderConstants.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#ifndef SOKOBAN_TEST_SHADER_INCLUDE_DIR
#error "SOKOBAN_TEST_SHADER_INCLUDE_DIR must name the shared shader header directory"
#endif

namespace {

using namespace sokoban;

// Every `const int NAME = VALUE;` the header declares.
std::map<std::string, long> parseGlslConstants(const std::filesystem::path& path)
{
    std::map<std::string, long> values;
    std::ifstream file(path);
    if (!file) {
        return values;
    }
    const std::regex pattern {
        R"(^\s*const\s+int\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(-?[0-9]+)\s*;)"
    };
    std::string line;
    while (std::getline(file, line)) {
        std::smatch match;
        if (std::regex_search(line, match, pattern)) {
            values[match[1].str()] = std::stol(match[2].str());
        }
    }
    return values;
}

struct Expected {
    const char* glslName;
    DrawMaterialMode mode;
};

// Every enumerator, so a new one added on the C++ side without a GLSL entry
// fails here rather than at the first frame that uses it.
const std::vector<Expected>& expectedModes()
{
    static const std::vector<Expected> modes {
        { "DRAW_MODE_UNTEXTURED", DrawMaterialMode::Untextured },
        { "DRAW_MODE_MANIFEST_TEXTURE", DrawMaterialMode::ManifestTexture },
        { "DRAW_MODE_GLTF_MATERIAL", DrawMaterialMode::GltfMaterial },
        { "DRAW_MODE_FONT_GLYPH", DrawMaterialMode::FontGlyph },
        { "DRAW_MODE_TITLE_BACKGROUND", DrawMaterialMode::TitleBackground },
        { "DRAW_MODE_PROCEDURAL_TEXTURE", DrawMaterialMode::ProceduralTexture },
        { "DRAW_MODE_SCENE_IMAGE", DrawMaterialMode::SceneImage },
        { "DRAW_MODE_TEXTURE_IMAGE", DrawMaterialMode::TextureImage },
    };
    return modes;
}

void testGlslMirrorsTheEnum()
{
    TEST("glslMirrorsTheEnum");

    const std::filesystem::path header =
        std::filesystem::path { SOKOBAN_TEST_SHADER_INCLUDE_DIR }
        / "DrawMode.glsl";
    CHECK(std::filesystem::exists(header));

    const std::map<std::string, long> glsl = parseGlslConstants(header);
    // Without this the whole suite would pass by matching an empty map, which
    // is exactly how a text-parsing test rots.
    CHECK(!glsl.empty());
    if (glsl.empty()) {
        std::cerr << "  parsed no constants from " << header.string()
                  << "; the header's formatting has moved away from"
                     " `const int NAME = VALUE;`\n";
        return;
    }

    for (const Expected& expected : expectedModes()) {
        const auto found = glsl.find(expected.glslName);
        const bool present = found != glsl.end();
        ++checks;
        if (!present) {
            ++failures;
            std::cerr << "FAIL [" << currentTest << "] " << expected.glslName
                      << " is missing from DrawMode.glsl\n";
            continue;
        }
        const long want = static_cast<long>(
            static_cast<uint32_t>(expected.mode));
        ++checks;
        if (found->second != want) {
            ++failures;
            std::cerr << "FAIL [" << currentTest << "] " << expected.glslName
                      << " is " << found->second << " in DrawMode.glsl, but "
                      << want << " in DrawMaterialMode\n";
        }
    }

    // And nothing extra: a stale GLSL constant left behind after an enumerator
    // was renamed would otherwise sit there looking authoritative.
    for (const auto& [name, value] : glsl) {
        const bool known = std::any_of(
            expectedModes().begin(),
            expectedModes().end(),
            [&name](const Expected& expected) {
                return name == expected.glslName;
            });
        ++checks;
        if (!known) {
            ++failures;
            std::cerr << "FAIL [" << currentTest << "] DrawMode.glsl declares "
                      << name << " = " << value
                      << ", which is not in DrawMaterialMode\n";
        }
    }
}

// Every `const float NAME = VALUE;` a shared header declares.
std::map<std::string, double> parseGlslFloats(const std::filesystem::path& path)
{
    std::map<std::string, double> values;
    std::ifstream file(path);
    if (!file) {
        return values;
    }
    const std::regex pattern {
        R"(^\s*const\s+float\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*)"
        R"((-?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)\s*;)"
    };
    std::string line;
    while (std::getline(file, line)) {
        std::smatch match;
        if (std::regex_search(line, match, pattern)) {
            values[match[1].str()] = std::stod(match[2].str());
        }
    }
    return values;
}

void testThePointShadowNearPlaneAgrees()
{
    TEST("thePointShadowNearPlaneAgrees");
    // VulkanSceneRecorder builds each cube face's projection from this value
    // and PointShadow.glsl inverts it to recover a world distance from depth.
    // Disagreement offsets every recovered distance by a constant, which looks
    // like shadow acne or like shadows coming loose from their casters.
    const std::filesystem::path header =
        std::filesystem::path { SOKOBAN_TEST_SHADER_INCLUDE_DIR }
        / "PointShadow.glsl";
    const std::map<std::string, double> glsl = parseGlslFloats(header);

    const auto found = glsl.find("POINT_SHADOW_NEAR_PLANE");
    const bool present = found != glsl.end();
    CHECK(present);
    if (!present) {
        std::cerr << "  POINT_SHADOW_NEAR_PLANE is missing from "
                  << header.string()
                  << "; the header's formatting has moved away from"
                     " `const float NAME = VALUE;`\n";
        return;
    }
    // Compared as float, because that is the width the shader stores it at.
    CHECK(static_cast<float>(found->second) == config::pointShadowNearPlane);
}

void testTheModelMarkerIsASignTest()
{
    TEST("theModelMarkerIsASignTest");
    // A model draw identifies itself by putting a negative alpha in
    // gridColor.w, and every shader that cares tests the sign rather than the
    // value. Both halves of that need pinning: a positive marker would make
    // every model draw look like a quad, and a shader that compared for
    // equality with -1 would silently reject any other negative a caller
    // picked - which the constant's own comment says is allowed.
    CHECK(modelDrawMarkerAlpha < 0.0f);

    const std::filesystem::path header =
        std::filesystem::path { SOKOBAN_TEST_SHADER_INCLUDE_DIR }
        / "DrawMode.glsl";
    std::ifstream file(header);
    CHECK(static_cast<bool>(file));
    const std::string source {
        std::istreambuf_iterator<char> { file },
        std::istreambuf_iterator<char> {}
    };
    CHECK(!source.empty());

    // Deliberately strict, and reported rather than silently unmatched: this is
    // a text coupling, so it has to say when the text has moved.
    const std::regex signTest {
        R"(bool\s+isModelDraw\s*\(\s*vec4\s+\w+\s*\)\s*\{\s*)"
        R"(return\s+\w+\.w\s*<\s*0\.0\s*;\s*\})"
    };
    const bool matched = std::regex_search(source, signTest);
    CHECK(matched);
    if (!matched) {
        std::cerr << "  isModelDraw in " << header.string()
                  << " is no longer `return <param>.w < 0.0;`. If that is"
                     " deliberate, confirm it still accepts every negative"
                     " alpha and update this test.\n";
    }
}

void testModesAreDistinctAndContiguous()
{
    TEST("modesAreDistinctAndContiguous");
    // The shader recovers the mode with int(x + 0.5) from a float lane, so the
    // values must stay small, non-negative and distinct. Contiguity is not
    // required by anything, but a gap is far more likely to be a mistake than
    // a decision, so this asks to be told about one.
    long previous = -1;
    for (const Expected& expected : expectedModes()) {
        const long value = static_cast<long>(
            static_cast<uint32_t>(expected.mode));
        CHECK(value == previous + 1);
        previous = value;
    }
}

} // namespace

int main()
{
    testGlslMirrorsTheEnum();
    testModesAreDistinctAndContiguous();
    testTheModelMarkerIsASignTest();
    testThePointShadowNearPlaneAgrees();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "draw_mode: " << checks << " checks passed\n";
    return 0;
}
