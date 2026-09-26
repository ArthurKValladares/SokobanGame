#include "engine/ShaderHotReload.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/ContentPipeline.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace sokoban {
namespace {

bool isWithin(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate)
{
    const std::filesystem::path relative =
        candidate.lexically_normal().lexically_relative(root.lexically_normal());
    return !relative.empty() && *relative.begin() != "..";
}

std::string readBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "cannot read compiled shader " + path.string());
    }
    return { std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>() };
}

void appendModuleDiagnostics(
    std::string& diagnostics,
    const std::string& module,
    const std::string& text)
{
    if (text.empty()) {
        return;
    }
    diagnostics += "== " + module + "\n" + text;
    if (diagnostics.back() != '\n') {
        diagnostics += '\n';
    }
}

} // namespace

ShaderHotReload::ShaderHotReload(Config config)
    : config_(std::move(config))
{
    if (!config_.compiler) {
        throw std::invalid_argument("ShaderHotReload requires a compiler");
    }
    modules_.reserve(config_.moduleNames.size());
    for (const std::string& name : config_.moduleNames) {
        modules_.push_back({
            .name = name,
            .source = config_.sourceDirectory / name,
        });
    }
    timestamps_ = sampleTimestamps();
}

ShaderHotReload::~ShaderHotReload()
{
    if (pending_ && pending_->outcome.valid()) {
        pending_->outcome.wait();
        // The compile wrote only to the scratch directory; discard it.
        std::error_code error;
        for (const Module& module : pending_->modules) {
            std::filesystem::remove(temporaryOutput(module), error);
        }
    }
}

ShaderHotReload::Timestamps ShaderHotReload::sampleTimestamps() const
{
    Timestamps result;
    std::error_code error;
    for (const Module& module : modules_) {
        const auto written =
            std::filesystem::last_write_time(module.source, error);
        if (!error) {
            result.emplace(module.source, written);
        }
    }
    for (std::filesystem::directory_iterator it(
             config_.includeDirectory, error), end;
         !error && it != end;
         it.increment(error)) {
        std::error_code entryError;
        if (!it->is_regular_file(entryError) || entryError) {
            continue;
        }
        const auto written = it->last_write_time(entryError);
        if (!entryError) {
            result.emplace(it->path(), written);
        }
    }
    return result;
}

void ShaderHotReload::requestFullRecompile()
{
    fullRecompileRequested_ = true;
}

bool ShaderHotReload::compiling() const
{
    return pending_.has_value();
}

void ShaderHotReload::poll()
{
    if (pending_ &&
        pending_->outcome.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready) {
        CompileOutcome outcome;
        try {
            outcome = pending_->outcome.get();
        } catch (const std::exception& error) {
            outcome = { .succeeded = false, .diagnostics = error.what() };
        }
        finished_ = publish(pending_->modules, std::move(outcome));
        pending_.reset();
    }

    const Timestamps current = sampleTimestamps();
    bool includeChanged = false;
    const auto noteChange = [&](const std::filesystem::path& path) {
        if (isWithin(config_.includeDirectory, path)) {
            includeChanged = true;
            return;
        }
        const auto module = std::ranges::find(modules_, path, &Module::source);
        if (module != modules_.end() &&
            std::ranges::find(dirty_, module->name, &Module::name) ==
                dirty_.end()) {
            dirty_.push_back(*module);
        }
    };
    for (const auto& [path, written] : current) {
        const auto previous = timestamps_.find(path);
        if (previous == timestamps_.end() || previous->second != written) {
            noteChange(path);
        }
    }
    for (const auto& [path, written] : timestamps_) {
        if (!current.contains(path)) {
            noteChange(path);
        }
    }
    timestamps_ = current;
    if (includeChanged || fullRecompileRequested_) {
        dirty_ = modules_;
        fullRecompileRequested_ = false;
    }

    if (!pending_ && !dirty_.empty()) {
        startCompile(std::exchange(dirty_, {}));
    }
}

void ShaderHotReload::startCompile(std::vector<Module> modules)
{
    std::vector<CompileRequest> requests;
    requests.reserve(modules.size());
    for (const Module& module : modules) {
        requests.push_back({
            .module = module,
            .output = temporaryOutput(module),
        });
    }
    std::error_code error;
    std::filesystem::create_directories(
        temporaryOutput(modules.front()).parent_path(), error);

    // Every module compiles even after a failure, so one pass reports every
    // error an include edit caused.
    std::future<CompileOutcome> outcome = std::async(
        std::launch::async,
        [compiler = config_.compiler, requests = std::move(requests)] {
            CompileOutcome combined { .succeeded = true };
            for (const CompileRequest& request : requests) {
                CompileOutcome single;
                try {
                    single = compiler(request);
                } catch (const std::exception& failure) {
                    single = { .succeeded = false,
                        .diagnostics = failure.what() };
                }
                combined.succeeded = combined.succeeded && single.succeeded;
                appendModuleDiagnostics(
                    combined.diagnostics,
                    request.module.name,
                    single.diagnostics);
            }
            return combined;
        });
    pending_ = PendingCompile {
        .modules = std::move(modules),
        .outcome = std::move(outcome),
    };
}

ShaderHotReload::Result ShaderHotReload::publish(
    const std::vector<Module>& modules,
    CompileOutcome outcome) const
{
    Result result {
        .published = false,
        .diagnostics = std::move(outcome.diagnostics),
    };
    for (const Module& module : modules) {
        result.modules.push_back(module.name);
    }
    std::error_code error;
    if (outcome.succeeded) {
        try {
            // Read every module before replacing any, so a missing output
            // cannot leave the staged tree half updated.
            std::vector<std::string> compiled;
            compiled.reserve(modules.size());
            for (const Module& module : modules) {
                compiled.push_back(readBytes(temporaryOutput(module)));
            }
            for (std::size_t index = 0; index < modules.size(); ++index) {
                atomicFile::write(
                    publishedOutput(modules[index]), compiled[index]);
            }
            result.published = true;
            (void)refreshContentPackageIndex(config_.runtimeAssetRoot);
        } catch (const std::exception& failure) {
            result.diagnostics += std::string("publication failed: ") +
                failure.what() + '\n';
        }
    }
    for (const Module& module : modules) {
        std::filesystem::remove(temporaryOutput(module), error);
    }
    return result;
}

std::optional<ShaderHotReload::Result> ShaderHotReload::takeResult()
{
    return std::exchange(finished_, std::nullopt);
}

std::filesystem::path ShaderHotReload::temporaryOutput(
    const Module& module) const
{
    // Beside the runtime tree, never inside it: an index refresh by another
    // tool while a compile is running must not see scratch files.
    return config_.runtimeAssetRoot.parent_path() / "shader-hot-reload" /
        (module.name + ".spv");
}

std::filesystem::path ShaderHotReload::publishedOutput(
    const Module& module) const
{
    return config_.runtimeAssetRoot / "shaders" / (module.name + ".spv");
}

ShaderHotReload::Compiler glslcCompiler(
    std::filesystem::path glslc,
    std::vector<std::string> arguments,
    std::filesystem::path includeDirectory)
{
    return [glslc = std::move(glslc),
               arguments = std::move(arguments),
               includeDirectory = std::move(includeDirectory)](
               const ShaderHotReload::CompileRequest& request) {
        const std::string& name = request.module.name;
        std::vector<std::string> command { glslc.string() };
        command.insert(command.end(), arguments.begin(), arguments.end());
        const std::string_view stage = name.find(".vert.") != std::string::npos
            ? "-fshader-stage=vertex"
            : "-fshader-stage=fragment";
        command.emplace_back(stage);
        command.push_back("-I");
        command.push_back(includeDirectory.string());
        command.push_back(request.module.source.string());
        command.push_back("-o");
        command.push_back(request.output.string());

        std::vector<const char*> argv;
        argv.reserve(command.size() + 1);
        for (const std::string& argument : command) {
            argv.push_back(argument.c_str());
        }
        argv.push_back(nullptr);

        const SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetPointerProperty(
            properties,
            SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
            reinterpret_cast<void*>(argv.data()));
        SDL_SetNumberProperty(
            properties,
            SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
            SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(
            properties,
            SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
            SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(
            properties,
            SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,
            true);
        SDL_Process* process = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (process == nullptr) {
            return ShaderHotReload::CompileOutcome {
                .succeeded = false,
                .diagnostics = "cannot start " + glslc.string() + ": " +
                    SDL_GetError() + '\n',
            };
        }
        std::size_t size = 0;
        int exitCode = -1;
        void* output = SDL_ReadProcess(process, &size, &exitCode);
        SDL_DestroyProcess(process);
        ShaderHotReload::CompileOutcome outcome {
            .succeeded = output != nullptr && exitCode == 0,
        };
        if (output != nullptr) {
            outcome.diagnostics.assign(static_cast<const char*>(output), size);
            SDL_free(output);
        } else {
            outcome.diagnostics = std::string("glslc did not complete: ") +
                SDL_GetError() + '\n';
        }
        return outcome;
    };
}

} // namespace sokoban
