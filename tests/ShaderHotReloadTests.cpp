#include "ScopedTestDirectory.hpp"
#include "TestHarness.hpp"

#include "engine/ContentPipeline.hpp"
#include "engine/ShaderHotReload.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

void writeFile(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>() };
}

// Timestamps can be coarse; move them explicitly so every edit is seen.
void edit(const std::filesystem::path& path, const std::string& text)
{
    const auto before = std::filesystem::last_write_time(path);
    writeFile(path, text);
    std::filesystem::last_write_time(path, before + std::chrono::seconds(2));
}

struct Fixture {
    explicit Fixture(const std::filesystem::path& root)
        : sources(root / "shaders")
        , includes(root / "shaders/include")
        , runtime(root / "build/Debug/assets")
    {
        writeFile(sources / "a.vert.glsl", "void main() { a(); }\n");
        writeFile(sources / "b.frag.glsl", "void main() { b(); }\n");
        writeFile(includes / "Common.glsl", "// shared\n");
        writeFile(runtime / "shaders/a.vert.glsl.spv", "old a");
        writeFile(runtime / "shaders/b.frag.glsl.spv", "old b");
        writeFile(runtime / "manifest.json", "{abc}");
        const std::string index =
            "format 1\n"
            "game-version 1.2.3\n"
            "file-count 3\n"
            "total-bytes 15\n"
            "file 5 manifest.json\n"
            "file 5 shaders/a.vert.glsl.spv\n"
            "file 5 shaders/b.frag.glsl.spv\n";
        writeFile(runtime / "content.index", index);
        sokoban::validateContentPackage(runtime, "1.2.3");
    }

    [[nodiscard]] sokoban::ShaderHotReload::Config config(
        sokoban::ShaderHotReload::Compiler compiler) const
    {
        return {
            .sourceDirectory = sources,
            .includeDirectory = includes,
            .runtimeAssetRoot = runtime,
            .moduleNames = { "a.vert.glsl", "b.frag.glsl" },
            .compiler = std::move(compiler),
        };
    }

    std::filesystem::path sources;
    std::filesystem::path includes;
    std::filesystem::path runtime;
};

// "Compiles" by prefixing the source text, and fails on sources that say so,
// the way glslc reports: file, line, message.
sokoban::ShaderHotReload::CompileOutcome fakeCompile(
    const sokoban::ShaderHotReload::CompileRequest& request)
{
    const std::string text = readFile(request.module.source);
    if (text.find("ERROR") != std::string::npos) {
        return { .succeeded = false,
            .diagnostics = request.module.source.string() +
                ":1: error: 'ERROR' : undeclared identifier\n" };
    }
    writeFile(request.output, "spirv:" + text);
    return { .succeeded = true };
}

std::optional<sokoban::ShaderHotReload::Result> waitForResult(
    sokoban::ShaderHotReload& reload)
{
    for (int attempt = 0; attempt < 500; ++attempt) {
        reload.poll();
        if (std::optional<sokoban::ShaderHotReload::Result> result =
                reload.takeResult()) {
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return std::nullopt;
}

void testUnchangedSourcesCompileNothing()
{
    TEST("unchanged sources compile nothing");
    ScopedTestDirectory temp("sokoban-shader-hot-reload");
    const Fixture fixture(temp.path());
    sokoban::ShaderHotReload reload(fixture.config(fakeCompile));
    reload.poll();
    CHECK(!reload.compiling());
    CHECK(!reload.takeResult().has_value());
    CHECK(readFile(fixture.runtime / "shaders/a.vert.glsl.spv") == "old a");
}

void testAnEditedModuleIsRecompiledAndPublished()
{
    TEST("an edited module is recompiled, published, and indexed");
    ScopedTestDirectory temp("sokoban-shader-hot-reload");
    const Fixture fixture(temp.path());
    sokoban::ShaderHotReload reload(fixture.config(fakeCompile));

    edit(fixture.sources / "a.vert.glsl", "void main() { a2(); }\n");
    const auto result = waitForResult(reload);
    CHECK(result.has_value());
    if (!result) {
        return;
    }
    CHECK(result->published);
    CHECK(result->modules == std::vector<std::string> { "a.vert.glsl" });
    CHECK(readFile(fixture.runtime / "shaders/a.vert.glsl.spv") ==
        "spirv:void main() { a2(); }\n");
    CHECK(readFile(fixture.runtime / "shaders/b.frag.glsl.spv") == "old b");
    // The index describes the new module size, so the next launch's
    // startup validation accepts the tree.
    sokoban::validateContentPackage(fixture.runtime, "1.2.3");
    CHECK(!std::filesystem::exists(
        temp.path() / "build/Debug/shader-hot-reload/a.vert.glsl.spv"));

    reload.poll();
    CHECK(!reload.compiling());
    CHECK(!reload.takeResult().has_value());
}

void testAnIncludeEditRecompilesEveryModule()
{
    TEST("an include edit recompiles every module");
    ScopedTestDirectory temp("sokoban-shader-hot-reload");
    const Fixture fixture(temp.path());
    sokoban::ShaderHotReload reload(fixture.config(fakeCompile));

    edit(fixture.includes / "Common.glsl", "// changed\n");
    const auto result = waitForResult(reload);
    CHECK(result.has_value() && result->published);
    CHECK(result.has_value() && result->modules.size() == 2);
    CHECK(readFile(fixture.runtime / "shaders/b.frag.glsl.spv") ==
        "spirv:void main() { b(); }\n");

    writeFile(fixture.includes / "Added.glsl", "// new header\n");
    const auto added = waitForResult(reload);
    CHECK(added.has_value() && added->modules.size() == 2);
}

void testAFailedCompilePublishesNothing()
{
    TEST("a failed compile publishes nothing and reports the error");
    ScopedTestDirectory temp("sokoban-shader-hot-reload");
    const Fixture fixture(temp.path());
    sokoban::ShaderHotReload reload(fixture.config(fakeCompile));
    const std::string indexBefore = readFile(fixture.runtime / "content.index");

    edit(fixture.includes / "Common.glsl", "// fine\n");
    edit(fixture.sources / "b.frag.glsl", "void main() { ERROR; }\n");
    const auto result = waitForResult(reload);
    CHECK(result.has_value());
    if (!result) {
        return;
    }
    CHECK(!result->published);
    CHECK(result->diagnostics.find("== b.frag.glsl") != std::string::npos);
    CHECK(result->diagnostics.find(":1: error:") != std::string::npos);
    // Module a compiled, but nothing is published from a failed pass.
    CHECK(readFile(fixture.runtime / "shaders/a.vert.glsl.spv") == "old a");
    CHECK(readFile(fixture.runtime / "shaders/b.frag.glsl.spv") == "old b");
    CHECK(readFile(fixture.runtime / "content.index") == indexBefore);
    CHECK(!std::filesystem::exists(
        temp.path() / "build/Debug/shader-hot-reload/a.vert.glsl.spv"));

    // Fixing the error publishes the next pass.
    edit(fixture.sources / "b.frag.glsl", "void main() { fixed(); }\n");
    const auto fixedResult = waitForResult(reload);
    CHECK(fixedResult.has_value() && fixedResult->published);
    CHECK(readFile(fixture.runtime / "shaders/b.frag.glsl.spv") ==
        "spirv:void main() { fixed(); }\n");
}

void testFullRecompileAndEditsDuringACompile()
{
    TEST("a requested recompile and edits made while compiling");
    ScopedTestDirectory temp("sokoban-shader-hot-reload");
    const Fixture fixture(temp.path());
    std::atomic<int> compiles = 0;
    sokoban::ShaderHotReload reload(fixture.config(
        [&](const sokoban::ShaderHotReload::CompileRequest& request) {
            ++compiles;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return fakeCompile(request);
        }));

    reload.requestFullRecompile();
    reload.poll();
    CHECK(reload.compiling());
    // Saved while the first pass is still running: picked up afterwards.
    edit(fixture.sources / "a.vert.glsl", "void main() { later(); }\n");
    const auto first = waitForResult(reload);
    CHECK(first.has_value() && first->modules.size() == 2);
    const auto second = waitForResult(reload);
    CHECK(second.has_value() &&
        second->modules == std::vector<std::string> { "a.vert.glsl" });
    CHECK(readFile(fixture.runtime / "shaders/a.vert.glsl.spv") ==
        "spirv:void main() { later(); }\n");
    CHECK(compiles.load() == 3);
}

#ifdef SOKOBAN_TEST_GLSLC
// The production compiler: the build's glslc, run as a child process.
void testGlslcCompilerReportsSuccessAndErrors()
{
    TEST("glslc compiler compiles a real shader and reports errors");
    ScopedTestDirectory temp("sokoban-shader-hot-reload");
    const std::filesystem::path shaders = SOKOBAN_TEST_SHADER_SOURCE_DIR;
    const sokoban::ShaderHotReload::Compiler compiler =
        sokoban::glslcCompiler(
            SOKOBAN_TEST_GLSLC,
            { "--target-env=vulkan1.3", "-O", "-DMAX_SKIN_JOINTS=128" },
            shaders / "include");

    const auto compiled = compiler({
        .module = { "atmosphere.frag.glsl", shaders / "atmosphere.frag.glsl" },
        .output = temp.path() / "atmosphere.frag.glsl.spv",
    });
    CHECK_MESSAGE(compiled.succeeded, compiled.diagnostics.c_str());
    const std::string spirv = readFile(temp.path() / "atmosphere.frag.glsl.spv");
    CHECK(spirv.size() > 4 &&
        static_cast<unsigned char>(spirv[0]) == 0x03 &&
        static_cast<unsigned char>(spirv[3]) == 0x07);

    const std::filesystem::path broken = temp.path() / "broken.frag.glsl";
    writeFile(broken, "#version 450\nvoid main() {\n    undefinedCall();\n}\n");
    const auto failed = compiler({
        .module = { "broken.frag.glsl", broken },
        .output = temp.path() / "broken.frag.glsl.spv",
    });
    CHECK(!failed.succeeded);
    CHECK(failed.diagnostics.find("broken.frag.glsl:3") != std::string::npos);
}
#endif

} // namespace

int main()
{
    try {
        testUnchangedSourcesCompileNothing();
        testAnEditedModuleIsRecompiledAndPublished();
        testAnIncludeEditRecompilesEveryModule();
        testAFailedCompilePublishesNothing();
        testFullRecompileAndEditsDuringACompile();
#ifdef SOKOBAN_TEST_GLSLC
        testGlslcCompilerReportsSuccessAndErrors();
#endif
    } catch (const std::exception& error) {
        std::cerr << "UNEXPECTED EXCEPTION: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " shader hot reload checks failed\n";
        return 1;
    }
    std::cout << "All " << checks << " shader hot reload checks passed\n";
    return 0;
}
