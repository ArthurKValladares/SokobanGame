#include "TestHarness.hpp"

#include "engine/CommandLineOptions.hpp"

#include <array>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

sokoban::CommandLineOptions parse(
    std::initializer_list<std::string_view> arguments)
{
    const std::vector<std::string_view> values(arguments);
    return sokoban::parseCommandLine(values);
}

void testEmptyIsANormalRun()
{
    const sokoban::CommandLineOptions options = parse({});
    CHECK_MESSAGE(!options.malformed, "no arguments parses");
    CHECK_MESSAGE(!options.smokeRun(), "no arguments is not a smoke run");
    CHECK_MESSAGE(options.smokeFrames == 0, "no frame count without the flag");
    CHECK_MESSAGE(!options.requireValidation, "validation not required by default");
    CHECK_MESSAGE(!options.bakeTileThumbnails, "no bake by default");
    CHECK_MESSAGE(options.saveDirectory.empty(), "no save override by default");
    CHECK_MESSAGE(options.evidenceOutputDirectory.empty(),
        "no evidence output by default");
    CHECK_MESSAGE(options.evidenceRenderScalePercent == 100,
        "evidence scale defaults to 100 percent");
    CHECK_MESSAGE(options.evidenceAntiAliasingSamples == 4,
        "evidence MSAA defaults to the product default");
    CHECK_MESSAGE(options.evidenceAmbientOcclusionEnabled,
        "ambient occlusion is enabled in evidence runs by default");
    CHECK_MESSAGE(options.evidenceFrustumCullingEnabled,
        "frustum culling is enabled in evidence runs by default");
    CHECK_MESSAGE(!options.evidenceWaterEnabled,
        "evidence water fixture is disabled by default");
    CHECK_MESSAGE(options.parallelScenePreparationEnabled,
        "scene preparation is parallel by default");
    CHECK_MESSAGE(!options.evidencePointLightEnabled,
        "evidence point light is disabled by default");
    CHECK_MESSAGE(options.pointShadowOptimizationsEnabled,
        "point-shadow optimizations are enabled by default");
    CHECK_MESSAGE(options.recorderScratchReuseEnabled,
        "recorder scratch reuse is enabled by default");
    CHECK_MESSAGE(options.textureResidencyBudgetKiB == 0,
        "texture residency uses its production default");
}

void testFlags()
{
    const sokoban::CommandLineOptions bake = parse({ "--bake-tile-thumbnails" });
    CHECK_MESSAGE(!bake.malformed && bake.bakeTileThumbnails, "bake flag parses");

    const sokoban::CommandLineOptions smoke = parse(
        { "--smoke-frames", "240", "--require-validation",
            "--save-directory", "/tmp/profile" });
    CHECK_MESSAGE(!smoke.malformed, "full smoke invocation parses");
    CHECK_MESSAGE(smoke.smokeFrames == 240, "frame count is read");
    CHECK_MESSAGE(smoke.smokeRun(), "a non-zero count is a smoke run");
    CHECK_MESSAGE(smoke.requireValidation, "validation requirement is read");
    CHECK_MESSAGE(smoke.saveDirectory == "/tmp/profile", "save directory is read");

    const sokoban::CommandLineOptions residency = parse(
        { "--smoke-frames", "120", "--texture-residency-kib", "64" });
    CHECK_MESSAGE(!residency.malformed, "residency stress invocation parses");
    CHECK_MESSAGE(residency.textureResidencyBudgetKiB == 64,
        "texture residency override is read");

    const sokoban::CommandLineOptions serialPreparation = parse(
        { "--smoke-frames", "120", "--serial-scene-preparation" });
    CHECK_MESSAGE(!serialPreparation.malformed,
        "serial scene preparation invocation parses");
    CHECK_MESSAGE(!serialPreparation.parallelScenePreparationEnabled,
        "serial scene preparation override is read");

    const sokoban::CommandLineOptions pointShadowEvidence = parse(
        { "--smoke-frames", "120", "--evidence-output", "/tmp/evidence",
            "--evidence-point-light",
            "--disable-point-shadow-optimizations" });
    CHECK_MESSAGE(!pointShadowEvidence.malformed,
        "point-shadow evidence invocation parses");
    CHECK_MESSAGE(pointShadowEvidence.evidencePointLightEnabled,
        "point-shadow evidence light is enabled");
    CHECK_MESSAGE(!pointShadowEvidence.pointShadowOptimizationsEnabled,
        "point-shadow legacy control is read");

    const sokoban::CommandLineOptions pointShadowStress = parse(
        { "--smoke-frames", "120", "--evidence-output", "/tmp/evidence",
            "--evidence-point-light-stress" });
    CHECK_MESSAGE(!pointShadowStress.malformed,
        "point-shadow stress invocation parses");
    CHECK_MESSAGE(pointShadowStress.evidencePointLightStressEnabled,
        "point-shadow stress lights are enabled");

    const sokoban::CommandLineOptions transientRecorderScratch = parse(
        { "--smoke-frames", "120", "--disable-recorder-scratch-reuse" });
    CHECK_MESSAGE(!transientRecorderScratch.malformed,
        "transient recorder scratch invocation parses");
    CHECK_MESSAGE(!transientRecorderScratch.recorderScratchReuseEnabled,
        "recorder scratch control is read");

    const sokoban::CommandLineOptions evidence = parse(
        { "--smoke-frames", "180", "--evidence-output", "/tmp/evidence",
            "--evidence-render-scale", "50", "--evidence-msaa", "2" });
    CHECK_MESSAGE(!evidence.malformed, "evidence invocation parses");
    CHECK_MESSAGE(evidence.evidenceOutputDirectory == "/tmp/evidence",
        "evidence directory is read");
    CHECK_MESSAGE(evidence.evidenceRenderScalePercent == 50,
        "evidence scale is read");
    CHECK_MESSAGE(evidence.evidenceAntiAliasingSamples == 2,
        "evidence MSAA is read");

    for (const std::string_view samples : { "1", "2", "4", "8" }) {
        const sokoban::CommandLineOptions supported = parse(
            { "--smoke-frames", "3", "--evidence-output", "/tmp/evidence",
                "--evidence-msaa", samples });
        CHECK_MESSAGE(!supported.malformed,
            "each supported evidence MSAA sample count parses");
    }

    const sokoban::CommandLineOptions noAoEvidence = parse(
        { "--smoke-frames", "180", "--evidence-output", "/tmp/evidence",
            "--evidence-disable-ao" });
    CHECK_MESSAGE(!noAoEvidence.malformed, "AO-off evidence invocation parses");
    CHECK_MESSAGE(!noAoEvidence.evidenceAmbientOcclusionEnabled,
        "AO-off evidence invocation disables ambient occlusion");

    const sokoban::CommandLineOptions noCullingEvidence = parse(
        { "--smoke-frames", "180", "--evidence-output", "/tmp/evidence",
            "--evidence-disable-frustum-culling" });
    CHECK_MESSAGE(!noCullingEvidence.malformed,
        "culling-off evidence invocation parses");
    CHECK_MESSAGE(!noCullingEvidence.evidenceFrustumCullingEnabled,
        "culling-off evidence invocation disables frustum culling");

    const sokoban::CommandLineOptions waterEvidence = parse(
        { "--smoke-frames", "180", "--evidence-output", "/tmp/evidence",
            "--evidence-water" });
    CHECK_MESSAGE(!waterEvidence.malformed, "water evidence invocation parses");
    CHECK_MESSAGE(waterEvidence.evidenceWaterEnabled,
        "water evidence invocation enables the fixture");

    // Order must not matter: CI writes these in whatever order reads best.
    const sokoban::CommandLineOptions reordered = parse(
        { "--save-directory", "/tmp/profile", "--require-validation",
            "--smoke-frames", "240" });
    CHECK_MESSAGE(!reordered.malformed, "argument order does not matter");
    CHECK_MESSAGE(reordered.smokeFrames == 240, "reordered frame count is read");
    CHECK_MESSAGE(reordered.saveDirectory == "/tmp/profile",
        "reordered save directory is read");

    // A path that looks like a flag is still a path. Rejecting it would make
    // any directory starting with two dashes unusable.
    const sokoban::CommandLineOptions oddPath =
        parse({ "--save-directory", "--strange" });
    CHECK_MESSAGE(!oddPath.malformed, "a flag-shaped path is accepted as a value");
    CHECK_MESSAGE(oddPath.saveDirectory == "--strange", "flag-shaped path is read");
}

void testMalformedInput()
{
    // Each of these once produced a run that silently did nothing useful,
    // which is the failure mode a validation gate can least afford.
    CHECK_MESSAGE(parse({ "--smoke-frames" }).malformed,
        "missing frame count is rejected");
    CHECK_MESSAGE(parse({ "--save-directory" }).malformed,
        "missing save directory is rejected");
    CHECK_MESSAGE(parse({ "--evidence-output" }).malformed,
        "missing evidence directory is rejected");
    CHECK_MESSAGE(parse({ "--evidence-output", "" }).malformed,
        "empty evidence directory is rejected");
    CHECK_MESSAGE(parse({ "--evidence-render-scale" }).malformed,
        "missing evidence scale is rejected");
    CHECK_MESSAGE(parse({ "--evidence-msaa" }).malformed,
        "missing evidence MSAA sample count is rejected");
    CHECK_MESSAGE(parse({ "--texture-residency-kib" }).malformed,
        "missing residency budget is rejected");
    CHECK_MESSAGE(parse({ "--texture-residency-kib", "0" }).malformed,
        "zero residency budget is rejected");
    CHECK_MESSAGE(parse({ "--texture-residency-kib", "12x" }).malformed,
        "malformed residency budget is rejected");
    CHECK_MESSAGE(parse({ "--smoke-frames", "0" }).malformed,
        "zero frames is rejected");
    CHECK_MESSAGE(parse({ "--smoke-frames", "-1" }).malformed,
        "a negative frame count is rejected");
    CHECK_MESSAGE(parse({ "--smoke-frames", "abc" }).malformed,
        "a non-numeric frame count is rejected");
    CHECK_MESSAGE(parse({ "--smoke-frames", "12x" }).malformed,
        "trailing garbage after the count is rejected");
    CHECK_MESSAGE(parse({ "--smoke-frames", "" }).malformed,
        "an empty frame count is rejected");
    CHECK_MESSAGE(parse({ "--unknown-flag" }).malformed,
        "an unknown flag is rejected rather than ignored");
    CHECK_MESSAGE(parse({ "--smoke-frames", "2", "--evidence-output", "/tmp/e" })
            .malformed,
        "evidence capture requires two distinct capture frames");
    CHECK_MESSAGE(parse({ "--smoke-frames", "3", "--evidence-output", "/tmp/e",
                    "--evidence-render-scale", "24" })
            .malformed,
        "evidence scale below the renderer range is rejected");
    CHECK_MESSAGE(parse({ "--smoke-frames", "3", "--evidence-output", "/tmp/e",
                    "--evidence-render-scale", "101" })
            .malformed,
        "evidence scale above the renderer range is rejected");
    CHECK_MESSAGE(parse({ "--evidence-render-scale", "50" }).malformed,
        "evidence scale without an output directory is rejected");
    CHECK_MESSAGE(parse({ "--evidence-render-scale", "100" }).malformed,
        "explicit default evidence scale still requires an output directory");
    CHECK_MESSAGE(parse({ "--evidence-msaa", "4" }).malformed,
        "explicit default evidence MSAA still requires an output directory");
    CHECK_MESSAGE(parse({ "--smoke-frames", "3", "--evidence-output", "/tmp/e",
                    "--evidence-msaa", "0" })
            .malformed,
        "zero evidence MSAA is rejected");
    CHECK_MESSAGE(parse({ "--smoke-frames", "3", "--evidence-output", "/tmp/e",
                    "--evidence-msaa", "3" })
            .malformed,
        "unsupported evidence MSAA is rejected");
    CHECK_MESSAGE(parse({ "--smoke-frames", "3", "--evidence-output", "/tmp/e",
                    "--evidence-msaa", "8x" })
            .malformed,
        "malformed evidence MSAA is rejected");
    CHECK_MESSAGE(parse({ "--evidence-disable-ao" }).malformed,
        "AO-off evidence mode requires an output directory");
    CHECK_MESSAGE(parse({ "--evidence-disable-frustum-culling" }).malformed,
        "culling-off evidence mode requires an output directory");
    CHECK_MESSAGE(parse({ "--evidence-water" }).malformed,
        "water evidence mode requires an output directory");
    CHECK_MESSAGE(parse({ "--evidence-point-light" }).malformed,
        "point-light evidence mode requires an output directory");
    CHECK_MESSAGE(parse({ "--evidence-point-light-stress" }).malformed,
        "point-light stress mode requires an output directory");
    CHECK_MESSAGE(parse({ "--smoke-frames", "3", "--evidence-output", "/tmp/e",
                    "--evidence-point-light", "--evidence-point-light-stress" })
            .malformed,
        "point-light evidence modes are mutually exclusive");

    const sokoban::CommandLineOptions rejected = parse({ "--smoke-frames", "abc" });
    CHECK_MESSAGE(!rejected.error.empty(), "a rejection explains itself");
    CHECK_MESSAGE(rejected.smokeFrames == 0, "a rejected run has no frame count");
}

void testLargeCountFits()
{
    const sokoban::CommandLineOptions options =
        parse({ "--smoke-frames", "4294967296" });
    CHECK_MESSAGE(!options.malformed, "a count past 32 bits parses");
    CHECK_MESSAGE(options.smokeFrames == 4294967296ULL, "the count is not truncated");
}

} // namespace

int main()
{
    testEmptyIsANormalRun();
    testFlags();
    testMalformedInput();
    testLargeCountFits();
    if (failures != 0) {
        std::cerr << "CommandLineOptionsTests: " << failures
                  << " CHECK_MESSAGE(s) failed\n";
        return 1;
    }
    std::cout << "CommandLineOptionsTests passed\n";
    return 0;
}
