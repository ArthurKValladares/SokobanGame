[CmdletBinding()]
param(
    [string]$BuildDirectory = "out/visual-studio",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "",
    [switch]$Quick,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $buildRoot "performance-results/$Configuration/comprehensive"
}
$outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
}

function Resolve-Executable([string]$Name) {
    $multiConfig = Join-Path $buildRoot "$Configuration/$Name.exe"
    if (Test-Path -LiteralPath $multiConfig) {
        return $multiConfig
    }
    $singleConfig = Join-Path $buildRoot "$Name.exe"
    if (Test-Path -LiteralPath $singleConfig) {
        return $singleConfig
    }
    throw "Could not find $Name under $buildRoot. Build the requested configuration first."
}

function Invoke-Checked(
    [string]$Executable,
    [string[]]$Arguments,
    [string]$LogPath = ""
) {
    Write-Host "> $Executable $($Arguments -join ' ')"
    if ([string]::IsNullOrWhiteSpace($LogPath)) {
        & $Executable @Arguments
    } else {
        & $Executable @Arguments 2>&1 | Tee-Object -FilePath $LogPath
    }
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable exited with code $LASTEXITCODE"
    }
}

if (-not $SkipBuild) {
    $targets = @("sokoban_performance_tests", "sokoban")
    $cmakeCache = Join-Path $buildRoot "CMakeCache.txt"
    $vulkanTestsEnabled = (Test-Path -LiteralPath $cmakeCache) -and
        (Select-String -LiteralPath $cmakeCache `
            -Pattern "^SOKOBAN_BUILD_VULKAN_SMOKE_TESTS:BOOL=ON$" `
            -Quiet)
    if ($vulkanTestsEnabled) {
        $targets += "sokoban_vulkan_tests"
    }
    & cmake --build $buildRoot --config $Configuration --target $targets --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed with code $LASTEXITCODE"
    }
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$cpuOutput = Join-Path $outputRoot "cpu"
New-Item -ItemType Directory -Path $cpuOutput -Force | Out-Null

$performanceExecutable = Resolve-Executable "sokoban_performance_tests"
$performanceMode = if ($Quick) { "--quick" } else { "--full" }
Invoke-Checked $performanceExecutable @(
    $performanceMode,
    "--output", $cpuOutput
)

$vulkanRunner = $null
try {
    $vulkanRunner = Resolve-Executable "sokoban_vulkan_tests"
} catch {
    Write-Warning "Vulkan test runner is unavailable; startup and prepared-asset benchmarks are skipped."
}
if ($null -ne $vulkanRunner) {
    $vulkanOutput = Join-Path $outputRoot "vulkan"
    New-Item -ItemType Directory -Path $vulkanOutput -Force | Out-Null
    Invoke-Checked $vulkanRunner @(
        "vulkan_smoke", "--benchmark-startup"
    ) (Join-Path $vulkanOutput "startup.txt")
    Invoke-Checked $vulkanRunner @(
        "vulkan_smoke", "--benchmark-prepared-assets"
    ) (Join-Path $vulkanOutput "prepared-assets.txt")
}

$gameExecutable = Resolve-Executable "sokoban"
$frameCount = if ($Quick) { 120 } else { 360 }
$scenarios = @(
    @{ Name = "baseline"; Arguments = @() },
    @{ Name = "point-light-stress"; Arguments = @("--evidence-point-light-stress") },
    @{ Name = "serial-scene-preparation"; Arguments = @("--serial-scene-preparation") }
)
if (-not $Quick) {
    $scenarios += @(
        @{ Name = "render-scale-75"; Arguments = @("--evidence-render-scale", "75") },
        @{ Name = "ao-disabled"; Arguments = @("--evidence-disable-ao") },
        @{ Name = "translucent-water"; Arguments = @("--evidence-water") },
        @{ Name = "frustum-culling-disabled"; Arguments = @("--evidence-disable-frustum-culling") },
        @{
            Name = "point-shadow-optimizations-disabled"
            Arguments = @(
                "--evidence-point-light-stress",
                "--disable-point-shadow-optimizations"
            )
        },
        @{ Name = "recorder-scratch-reuse-disabled"; Arguments = @("--disable-recorder-scratch-reuse") },
        @{ Name = "baseline-repeat"; Arguments = @() }
    )
}

$originalLocation = Get-Location
$runId = Get-Date -Format "yyyyMMdd-HHmmss-fff"
try {
    Set-Location (Split-Path -Parent $gameExecutable)
    foreach ($scenario in $scenarios) {
        $scenarioOutput = Join-Path $outputRoot "gpu/$($scenario.Name)"
        # A fresh preference/profile root makes scenarios comparable even when
        # the same report directory is reused for another run.
        $stateOutput = Join-Path $scenarioOutput "state-$runId"
        New-Item -ItemType Directory -Path $scenarioOutput -Force | Out-Null
        New-Item -ItemType Directory -Path $stateOutput -Force | Out-Null
        $arguments = @(
            "--smoke-frames", "$frameCount",
            "--save-directory", $stateOutput,
            "--evidence-output", $scenarioOutput
        ) + $scenario.Arguments
        Invoke-Checked $gameExecutable $arguments (Join-Path $scenarioOutput "run.log")
    }
} finally {
    Set-Location $originalLocation
}

$indexLines = @(
    "# Comprehensive performance run",
    "",
    "- Configuration: $Configuration",
    "- Mode: $(if ($Quick) { 'quick' } else { 'full' })",
    "- Evidence frames per GPU scenario: $frameCount",
    "",
    "## CPU and engine workloads",
    "",
    "- [Ranked CPU report](cpu/performance-report.md)",
    "- [Machine-readable results](cpu/performance-results.json)",
    "- [CPU trace](cpu/cpu-trace.json)",
    "",
    "## GPU evidence matrix",
    "",
    "| Scenario | CPU avg / p95 ms | GPU avg / p95 ms | Scene prep avg / p95 ms | Top finding |",
    "|---|---:|---:|---:|---|"
)
foreach ($scenario in $scenarios) {
    $metrics = Get-ChildItem -LiteralPath (Join-Path $outputRoot "gpu/$($scenario.Name)") `
        -Filter "metrics-*.md" | Select-Object -First 1
    if ($null -ne $metrics) {
        $relative = "gpu/$($scenario.Name)/$($metrics.Name)" -replace '\\', '/'
        $text = Get-Content -LiteralPath $metrics.FullName -Raw
        $cpu = [regex]::Match(
            $text,
            '(?m)^- CPU frame: average ([0-9.]+) ms, p95 ([0-9.]+) ms')
        $gpu = [regex]::Match(
            $text,
            '(?m)^- GPU frame: average ([0-9.]+) ms, p95 ([0-9.]+) ms')
        $scene = [regex]::Match(
            $text,
            '(?m)^- Scene preparation: average ([0-9.]+) ms, p95 ([0-9.]+) ms')
        $topFinding = [regex]::Match(
            $text,
            '(?m)^1\. \*\*(.+?)\*\*')
        $cpuText = if ($cpu.Success) {
            "$($cpu.Groups[1].Value) / $($cpu.Groups[2].Value)"
        } else { "unavailable" }
        $gpuText = if ($gpu.Success) {
            "$($gpu.Groups[1].Value) / $($gpu.Groups[2].Value)"
        } else { "unavailable" }
        $sceneText = if ($scene.Success) {
            "$($scene.Groups[1].Value) / $($scene.Groups[2].Value)"
        } else { "unavailable" }
        $findingText = if ($topFinding.Success) {
            $topFinding.Groups[1].Value
        } else { "No rule-based bottleneck" }
        $indexLines += "| [$($scenario.Name)]($relative) | $cpuText | $gpuText | $sceneText | $findingText |"
    }
}
if ($null -ne $vulkanRunner) {
    $indexLines += @(
        "",
        "## Vulkan startup and streaming",
        "",
        "- [Startup phases](vulkan/startup.txt)",
        "- [Prepared-asset pressure](vulkan/prepared-assets.txt)"
    )
    $startupText = Get-Content -LiteralPath `
        (Join-Path $outputRoot "vulkan/startup.txt") -Raw
    $startupPhases = [regex]::Matches(
        $startupText,
        '(?<name>[a-z_]+)_us=(?<value>[0-9]+)') | ForEach-Object {
            [pscustomobject]@{
                Name = $_.Groups['name'].Value -replace '_', ' '
                Microseconds = [uint64]$_.Groups['value'].Value
            }
        } | Sort-Object Microseconds -Descending | Select-Object -First 3
    if ($startupPhases.Count -gt 0) {
        $phaseSummary = ($startupPhases | ForEach-Object {
            "$($_.Name) $([math]::Round($_.Microseconds / 1000.0, 1)) ms"
        }) -join ", "
        $indexLines += "- Slowest measured startup phases: $phaseSummary."
    }
    $pressureText = Get-Content -LiteralPath `
        (Join-Path $outputRoot "vulkan/prepared-assets.txt") -Raw
    $pressure = [regex]::Match(
        $pressureText,
        'deferral_attempts=([0-9]+).*?deferred_us=([0-9]+).*?prepared_budget_deferrals=([0-9]+).*?elapsed_us=([0-9]+)')
    if ($pressure.Success) {
        $indexLines += "- Prepared-asset pressure: $($pressure.Groups[1].Value) residency deferral attempt(s), $($pressure.Groups[3].Value) prepared-budget deferral(s), $([math]::Round([uint64]$pressure.Groups[2].Value / 1000.0, 1)) ms deferred, $([math]::Round([uint64]$pressure.Groups[4].Value / 1000.0, 1)) ms total."
    }
}
$indexPath = Join-Path $outputRoot "index.md"
$indexLines | Set-Content -LiteralPath $indexPath -Encoding utf8
Write-Host "Comprehensive performance index: $indexPath"
