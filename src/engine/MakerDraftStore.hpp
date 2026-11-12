#pragma once

#include "engine/Level.hpp"

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

class MakerDraftStore {
public:
    static constexpr std::size_t maxNameBytes = 64;

    class NameExists : public std::runtime_error {
    public:
        NameExists() : std::runtime_error("A draft with this name already exists.") {}
    };

    explicit MakerDraftStore(std::filesystem::path root);

    [[nodiscard]] std::vector<std::string> list() const;
    void save(std::string_view name, const Level::Definition& definition,
        bool overwrite = false) const;
    [[nodiscard]] Level::Definition load(std::string_view name) const;

private:
    std::filesystem::path root_;
};

} // namespace sokoban
