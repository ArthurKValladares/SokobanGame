#pragma once

#include "engine/EvidenceScenarios.hpp"
#include "engine/UserSettingsConfig.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sokoban {

// Launch switches that travel from the command line to Application unchanged.
//
// Declared once and inherited by both CommandLineOptions and
// ApplicationOptions, so a new switch is added here, parsed in
// parseCommandLine and read by Application, with no field-by-field copy in
// between and no second default to keep in step. Free of platform headers so
// the parser stays testable headlessly.
struct LaunchOptions {
    // Render this many frames through the ordinary loop, start a game so the
    // scene pass is actually exercised, and exit. Zero runs until quit.
    //
    // A title-screen-only run would be close to worthless as a smoke test:
    // buildRenderFrame returns an empty RenderFrameData while no game is
    // loaded, so no tiles, models, shadows or SSAO are recorded at all.
    std::uint64_t smokeFrames = 0;
    // Evidence runs archive the last normal frame, a filtered SSAO debug
    // frame, and timing statistics after a smoke run. The scale override
    // keeps separate runs comparable without reading or rewriting a user
    // profile.
    int evidenceRenderScalePercent = 100;
    // Evidence must not inherit a workstation's saved profile. Keep the
    // product default unless the run explicitly selects another supported
    // sample count.
    int evidenceAntiAliasingSamples = config::antiAliasingSamples;
    // Uses the product's non-vsync policy for an evidence A/B run. The actual
    // selected mode is still hardware-dependent and is written to the report.
    bool evidenceVsyncEnabled = true;
    bool evidenceAmbientOcclusionEnabled = true;
    bool evidenceFrustumCullingEnabled = true;
    bool evidenceWaterEnabled = false;
    bool evidencePointLightEnabled = false;
    bool evidencePointLightStressEnabled = false;
    // Negative means the evidence run uses its default scene.
    int evidenceLevel = -1;
    int evidenceScreen = 0;
    bool evidenceDebugUi = false;
    bool evidenceProfilerEnabled = true;
    bool evidenceAnimate = false;
    std::string evidenceEffects;
    // Scripted play for an animated evidence run: keys pressed on given
    // frames, so that a capture can catch a push, a landing or a bump while
    // it happens. See parseEvidenceMoves.
    std::vector<EvidenceMove> evidenceMoves;
    bool evidenceWaterDisabled = false;
    bool evidenceWaterReflectionsDisabled = false;
    // Diagnostic A/B switches for frame-preparation profiling.
    bool parallelScenePreparationEnabled = true;
    bool pointShadowOptimizationsEnabled = true;
    bool recorderScratchReuseEnabled = true;
    bool waterCellCacheEnabled = true;
    bool groundGeometryProcessingEnabled = true;
    bool groundRimEnabled = true;
    bool groundChunksEnabled = false;
    bool groundChunkMeshoptimizerEnabled = true;
    bool evidenceGroundRimFixture = false;
    // Diagnostic override for exercising residency pressure. Zero keeps the
    // normal renderer budget.
    std::uint64_t textureResidencyBudgetKiB = 0;
    // Developer launch shortcuts. --continue loads the active save slot
    // instead of showing the title. --title shows the title even when a
    // developer session file would otherwise resume. --level/--screen need a
    // build with developer tools: they continue and then enter that puzzle
    // screen. A negative level means no location was requested.
    bool continueGame = false;
    bool showTitle = false;
    int startLevel = -1;
    int startScreen = 0;
};

} // namespace sokoban
