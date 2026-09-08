[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Validator,
    [Parameter(Mandatory)][string]$FixtureExecutable,
    [Parameter(Mandatory)][string]$ApplicationExecutable,
    [Parameter(Mandatory)][string]$WorkingRoot,
    [Parameter(Mandatory)][string]$GameVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Condition {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw $Message
    }
}

function New-TestPackage {
    param([string]$Root, [string]$Executable)

    New-Item -ItemType Directory -Path (Join-Path $Root 'assets') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $Root 'licenses') -Force | Out-Null
    Copy-Item -LiteralPath $Executable -Destination (Join-Path $Root 'sokoban.exe')
    @(
        'format 1'
        "game-version $GameVersion"
        'file-count 0'
        'total-bytes 0'
    ) | Set-Content -LiteralPath (Join-Path $Root 'assets/content.index') -Encoding ascii
    foreach ($license in @(
            'SDL-LICENSE.txt',
            'miniaudio-LICENSE.txt',
            'nlohmann-json-LICENSE.txt',
            'stb-LICENSE.txt',
            'cgltf-LICENSE.txt',
            'imgui-LICENSE.txt')) {
        Set-Content -LiteralPath (Join-Path $Root "licenses/$license") -Value $license
    }
}

function Invoke-ExpectedFailure {
    param(
        [string]$Package,
        [string]$ExpectedText,
        [int]$TimeoutSeconds = 5,
        [switch]$SkipLaunch,
        [string]$Diagnostics
    )

    try {
        $parameters = @{
            Package = $Package
            SmokeFrames = 3
            SmokeTimeoutSeconds = $TimeoutSeconds
        }
        if ($SkipLaunch) {
            $parameters.SkipLaunch = $true
        }
        if ($Diagnostics) {
            $parameters.DiagnosticOutputDirectory = $Diagnostics
        }
        & $Validator @parameters | Out-Host
    } catch {
        $failure = $_ | Out-String
        Assert-Condition ($failure -match [regex]::Escape($ExpectedText)) `
            "Package gate failure did not contain '$ExpectedText':`n$failure"
        return $failure
    }
    throw "Package gate unexpectedly passed; expected '$ExpectedText'."
}

$root = [IO.Path]::GetFullPath($WorkingRoot)
Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $root | Out-Null

try {
    $fixturePackage = Join-Path $root 'fixture-package'
    New-TestPackage -Root $fixturePackage -Executable $FixtureExecutable

    $env:SOKOBAN_PACKAGE_VALIDATION_FIXTURE_MODE = 'success'
    $successDiagnostics = Join-Path $root 'success-diagnostics'
    & $Validator `
        -Package $fixturePackage `
        -SmokeFrames 3 `
        -SmokeTimeoutSeconds 5 `
        -DiagnosticOutputDirectory $successDiagnostics | Out-Host
    Assert-Condition `
        (Test-Path -LiteralPath (Join-Path $successDiagnostics 'profile/log.txt')) `
        'Successful smoke diagnostics did not preserve the isolated game log.'

    $env:SOKOBAN_PACKAGE_VALIDATION_FIXTURE_MODE = 'failure'
    $failureDiagnostics = Join-Path $root 'failure-diagnostics'
    $failure = Invoke-ExpectedFailure `
        -Package $fixturePackage `
        -ExpectedText 'smoke run exited with code 23' `
        -Diagnostics $failureDiagnostics
    Assert-Condition ($failure -match 'fixture initialization failure') `
        'Nonzero-exit diagnostics did not include the game log.'
    Assert-Condition ($failure -match 'fixture stderr failure detail') `
        'Nonzero-exit diagnostics did not include stderr.'

    $env:SOKOBAN_PACKAGE_VALIDATION_FIXTURE_MODE = 'hang'
    $hangDiagnostics = Join-Path $root 'hang-diagnostics'
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $hangFailure = Invoke-ExpectedFailure `
        -Package $fixturePackage `
        -ExpectedText 'did not complete its 3-frame smoke run within 1 seconds' `
        -TimeoutSeconds 1 `
        -Diagnostics $hangDiagnostics
    $timer.Stop()
    Assert-Condition ($timer.Elapsed.TotalSeconds -lt 10) `
        "Hung process was not stopped promptly ($($timer.Elapsed.TotalSeconds) seconds)."
    Assert-Condition ($hangFailure -match 'fixture deliberate hang') `
        'Timeout diagnostics did not include the game log.'

    $missingPackage = Join-Path $root 'missing-content-package'
    New-TestPackage -Root $missingPackage -Executable $FixtureExecutable
    Remove-Item -LiteralPath (Join-Path $missingPackage 'assets/content.index')
    Invoke-ExpectedFailure `
        -Package $missingPackage `
        -ExpectedText 'Missing content index' `
        -SkipLaunch | Out-Null

    $corruptPackage = Join-Path $root 'corrupt-content-package'
    New-TestPackage -Root $corruptPackage -Executable $FixtureExecutable
    (Get-Content -LiteralPath (Join-Path $corruptPackage 'assets/content.index')) `
        -replace '^total-bytes 0$', 'total-bytes 1' |
        Set-Content -LiteralPath (Join-Path $corruptPackage 'assets/content.index') -Encoding ascii
    Invoke-ExpectedFailure `
        -Package $corruptPackage `
        -ExpectedText 'Content index total differs' `
        -SkipLaunch | Out-Null

    # This is the real executable with a structurally valid but empty content
    # package. AssetManifest construction throws before Vulkan initialization.
    # Without smoke-mode dialog suppression, the process waits for a user and
    # this case reaches the timeout instead of returning code 1 with its log.
    $applicationPackage = Join-Path $root 'application-failure-package'
    New-TestPackage -Root $applicationPackage -Executable $ApplicationExecutable
    Remove-Item Env:SOKOBAN_PACKAGE_VALIDATION_FIXTURE_MODE
    $applicationDiagnostics = Join-Path $root 'application-diagnostics'
    $applicationFailure = Invoke-ExpectedFailure `
        -Package $applicationPackage `
        -ExpectedText 'smoke run exited with code 1' `
        -TimeoutSeconds 10 `
        -Diagnostics $applicationDiagnostics
    Assert-Condition ($applicationFailure -match 'Fatal error:') `
        'Application initialization failure did not reach the isolated log.'
} finally {
    Remove-Item Env:SOKOBAN_PACKAGE_VALIDATION_FIXTURE_MODE -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host 'Shipping package gate regression scenarios passed.'
