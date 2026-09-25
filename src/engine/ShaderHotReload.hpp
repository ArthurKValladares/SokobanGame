#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sokoban {

// Recompiles edited GLSL while the game runs and publishes the SPIR-V into
// the staged runtime tree, where the renderer's pipeline factory reads it.
//
// The watcher is Vulkan-free: it decides what to compile, runs the compiler
// on a background thread, and on success replaces the staged modules and
// refreshes content.index, the same publication contract every authoring
// tool follows. Rebuilding pipelines from the new modules is the caller's
// job (VulkanRenderer::requestShaderReload). A failed compile publishes
// nothing, so the running pipelines and the staged tree stay as they were.
//
// Only shaders named in shaderCatalog::sources are watched, because those
// are the modules the renderer loads. A change to any file in the include
// directory recompiles every module, as the CMake rule does: glslc resolves
// includes itself, so nothing records which module reads which header.
class ShaderHotReload {
public:
    struct Module {
        std::string name;              // e.g. "atmosphere.frag.glsl"
        std::filesystem::path source;  // absolute source path
    };

    struct CompileRequest {
        Module module;
        std::filesystem::path output;
    };

    struct CompileOutcome {
        bool succeeded = false;
        // Compiler output, including warnings on success.
        std::string diagnostics;
    };

    // Compiles one module to `output`. Called on a background thread, one
    // module at a time.
    using Compiler = std::function<CompileOutcome(const CompileRequest&)>;

    struct Config {
        std::filesystem::path sourceDirectory;
        std::filesystem::path includeDirectory;
        // The staged runtime asset root; modules are published to
        // <runtimeAssetRoot>/shaders/<name>.spv.
        std::filesystem::path runtimeAssetRoot;
        std::vector<std::string> moduleNames;
        Compiler compiler;
    };

    struct Result {
        bool published = false;
        std::vector<std::string> modules;
        std::string diagnostics;
    };

    explicit ShaderHotReload(Config config);
    ~ShaderHotReload();

    ShaderHotReload(const ShaderHotReload&) = delete;
    ShaderHotReload& operator=(const ShaderHotReload&) = delete;

    // Compares source timestamps with the last poll and starts a compile of
    // whatever changed. While a compile is running, changes accumulate and
    // start the next one when it finishes.
    void poll();
    // Recompiles every module on the next poll, whether or not it changed.
    void requestFullRecompile();

    // Returns a finished compile once. A successful one has already been
    // published to the runtime tree; failures leave it untouched.
    [[nodiscard]] std::optional<Result> takeResult();

    [[nodiscard]] bool compiling() const;
    [[nodiscard]] const std::vector<Module>& modules() const
    {
        return modules_;
    }

private:
    using Timestamps =
        std::map<std::filesystem::path, std::filesystem::file_time_type>;

    struct PendingCompile {
        std::vector<Module> modules;
        std::future<CompileOutcome> outcome;
    };

    [[nodiscard]] Timestamps sampleTimestamps() const;
    void startCompile(std::vector<Module> modules);
    [[nodiscard]] Result publish(
        const std::vector<Module>& modules,
        CompileOutcome outcome) const;
    [[nodiscard]] std::filesystem::path temporaryOutput(
        const Module& module) const;
    [[nodiscard]] std::filesystem::path publishedOutput(
        const Module& module) const;

    Config config_;
    std::vector<Module> modules_;
    Timestamps timestamps_;
    std::vector<Module> dirty_;
    bool fullRecompileRequested_ = false;
    std::optional<PendingCompile> pending_;
    std::optional<Result> finished_;
};

// A Compiler that runs glslc as a child process with the given arguments
// before the per-module stage, include, input, and output arguments.
[[nodiscard]] ShaderHotReload::Compiler glslcCompiler(
    std::filesystem::path glslc,
    std::vector<std::string> arguments,
    std::filesystem::path includeDirectory);

} // namespace sokoban
