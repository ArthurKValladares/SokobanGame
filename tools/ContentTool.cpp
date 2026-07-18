#include "engine/ContentPipeline.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

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

        const sokoban::ContentInventory inventory = validateOnly
            ? sokoban::collectContentInventory(roots)
            : sokoban::stageContent(roots, output, version);
        std::cout << (validateOnly ? "Validated " : "Staged ")
                  << inventory.files.size() << " files ("
                  << inventory.totalBytes << " bytes)";
        if (!validateOnly) {
            std::cout << " to " << output.string();
        }
        std::cout << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Content pipeline failed: " << error.what() << '\n';
        return 1;
    }
}
