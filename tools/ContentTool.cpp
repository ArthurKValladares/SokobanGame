#include "engine/ContentPipeline.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

// Size and modification time of the running tool, so a rebuilt tool never
// trusts a stage record written by its predecessor. Empty when the executable
// cannot be found from argv[0], which only makes the record stricter.
std::string toolIdentity(const char* argv0)
{
    std::error_code error;
    const std::filesystem::path executable =
        std::filesystem::absolute(argv0, error);
    if (error) {
        return {};
    }
    const std::uintmax_t size = std::filesystem::file_size(executable, error);
    if (error) {
        return {};
    }
    const auto written = std::filesystem::last_write_time(executable, error);
    if (error) {
        return {};
    }
    return std::to_string(size) + ' ' +
        std::to_string(written.time_since_epoch().count());
}

std::string valueAfter(int& index, int argc, char** argv, std::string_view option)
{
    if (++index >= argc) {
        throw std::runtime_error("missing value after " + std::string(option));
    }
    return argv[index];
}

} // namespace

int main(int argc, char** argv)
{
    try {
        sokoban::ContentSourceRoots roots;
        std::filesystem::path output;
        std::string version;
        bool validateOnly = false;
        sokoban::ContentStageOptions options;

        for (int i = 1; i < argc; ++i) {
            const std::string_view option = argv[i];
            if (option == "--assets") {
                roots.assets = valueAfter(i, argc, argv, option);
            } else if (option == "--levels") {
                roots.levels = valueAfter(i, argc, argv, option);
            } else if (option == "--shaders") {
                roots.shaders = valueAfter(i, argc, argv, option);
            } else if (option == "--output") {
                output = valueAfter(i, argc, argv, option);
            } else if (option == "--version") {
                version = valueAfter(i, argc, argv, option);
            } else if (option == "--validate-only") {
                validateOnly = true;
            } else if (option == "--texture-cache") {
                options.textureCache = valueAfter(i, argc, argv, option);
            } else if (option == "--incremental") {
                options.skipWhenUpToDate = true;
                options.toolIdentity = toolIdentity(argv[0]);
            } else {
                throw std::runtime_error("unknown option: " + std::string(option));
            }
        }

        if (roots.assets.empty() || roots.levels.empty() || roots.shaders.empty()) {
            throw std::runtime_error("--assets, --levels, and --shaders are required");
        }
        if (!validateOnly && (output.empty() || version.empty())) {
            throw std::runtime_error("staging requires --output and --version");
        }

        if (validateOnly) {
            const sokoban::ContentInventory inventory =
                sokoban::collectContentInventory(roots);
            std::cout << "Validated " << inventory.files.size() << " files ("
                      << inventory.totalBytes << " bytes)\n";
            return 0;
        }

        const sokoban::ContentStageReport report =
            sokoban::stageContent(roots, output, version, options);
        if (report.upToDate) {
            std::cout << "Content up to date: " << output.string() << '\n';
            return 0;
        }
        std::cout << "Staged " << report.inventory.files.size() << " files ("
                  << report.inventory.totalBytes << " bytes) to "
                  << output.string() << "; " << report.texturesEncoded
                  << " texture(s) encoded, " << report.texturesFromCache
                  << " from cache\n";
        if (report.textureCacheWriteFailures != 0) {
            std::cout << "warning: " << report.textureCacheWriteFailures
                      << " compressed texture(s) could not be cached; they "
                         "will be encoded again next time\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Content pipeline failed: " << error.what() << '\n';
        return 1;
    }
}
