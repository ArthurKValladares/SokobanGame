#pragma once

#include <filesystem>
#include <string>

namespace sokoban {

// The Tuning tab: every value registered through engine/Tuning.hpp, grouped
// by section, editable while the game runs. Save writes a section's edited
// values back into its header so they become the compiled defaults.
class TuningDebugUi {
public:
    explicit TuningDebugUi(std::filesystem::path sourceRoot);

    void draw();

private:
    std::filesystem::path sourceRoot_;
    std::string status_;
    bool statusIsError_ = false;
};

} // namespace sokoban
