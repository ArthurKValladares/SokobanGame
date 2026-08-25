[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path -LiteralPath $_ })]
    [string]$RuntimePackage,

    [string]$Installer,

    [ValidateRange(1, 120)]
    [int]$LaunchSeconds = 10,

    [string]$OutputDirectory = (Join-Path (Get-Location) (
        'Sokoban3D-ReleaseValidation-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))),

    [switch]$SkipManualChecklist,
    [switch]$IncludeReleaseArtifacts
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-JsonFile {
    param(
        [Parameter(Mandatory)]$Value,
        [Parameter(Mandatory)][string]$Path
    )

    $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding utf8
}

function Copy-LogTail {
    param(
        [Parameter(Mandatory)][string]$Destination
    )

    $locations = @(
        [Environment]::GetFolderPath('ApplicationData'),
        [Environment]::GetFolderPath('LocalApplicationData')
    ) | Where-Object { $_ }
    $logFiles = foreach ($location in $locations) {
        Get-ChildItem -LiteralPath (Join-Path $location 'Sokoban3D/Sokoban3D') `
            -Filter 'log*.txt' -File -ErrorAction SilentlyContinue
    }

    if (-not $logFiles) {
        'No Sokoban 3D log was found after the package launch check.' |
            Set-Content -LiteralPath $Destination -Encoding utf8
        return
    }

    foreach ($logFile in $logFiles) {
        "===== $($logFile.FullName) (last 300 lines) =====" |
            Add-Content -LiteralPath $Destination -Encoding utf8
        Get-Content -LiteralPath $logFile.FullName -Tail 300 -ErrorAction Continue |
            Add-Content -LiteralPath $Destination -Encoding utf8
    }
}

function Get-ChecklistResult {
    param(
        [Parameter(Mandatory)][string]$Id,
        [Parameter(Mandatory)][string]$Prompt
    )

    Write-Host ''
    Write-Host "$Id. $Prompt" -ForegroundColor Cyan
    do {
        $result = Read-Host 'Result: [P]ass, [F]ail, or [S]kipped'
    } while ($result -notmatch '^[PpFfSs]$')
    $notes = Read-Host 'Notes, issue ID, or reproduction details (optional)'

    [pscustomobject]@{
        id = $Id
        prompt = $Prompt
        result = switch ($result.ToUpperInvariant()) {
            'P' { 'pass' }
            'F' { 'fail' }
            default { 'skipped' }
        }
        notes = $notes
    }
}

$evidenceRoot = [IO.Path]::GetFullPath($OutputDirectory)
$workingRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'Sokoban3D-ReleaseValidation-' + [Guid]::NewGuid().ToString('N'))
$startedAt = Get-Date
if ($Installer -and -not (Test-Path -LiteralPath $Installer -PathType Leaf)) {
    throw "Installer was not found: '$Installer'. Relative paths are resolved from '$((Get-Location).Path)'. " +
        "Use the generated path, for example '.\\out\\shipping-installer\\installer\\Sokoban3D-<version>-Windows-x64-Setup.exe'."
}
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
New-Item -ItemType Directory -Path $workingRoot | Out-Null

$results = [ordered]@{
    schema = 1
    startedAt = $startedAt.ToString('o')
    runtimePackage = (Resolve-Path -LiteralPath $RuntimePackage).Path
    installer = if ($Installer) { (Resolve-Path -LiteralPath $Installer).Path } else { $null }
    automatedPackageGate = 'not-run'
    manualChecklist = 'not-run'
}

try {
    $os = Get-CimInstance -ClassName Win32_OperatingSystem
    $computer = Get-CimInstance -ClassName Win32_ComputerSystem
    $environment = [ordered]@{
        collectedAt = (Get-Date).ToString('o')
        os = [ordered]@{
            caption = $os.Caption
            version = $os.Version
            buildNumber = $os.BuildNumber
            architecture = $os.OSArchitecture
            installDate = $os.InstallDate
        }
        computer = [ordered]@{
            manufacturer = $computer.Manufacturer
            model = $computer.Model
            systemType = $computer.SystemType
            totalPhysicalMemoryBytes = $computer.TotalPhysicalMemory
        }
        videoControllers = @(Get-CimInstance -ClassName Win32_VideoController |
            Select-Object Name, VideoProcessor, AdapterRAM, DriverVersion,
                DriverDate, CurrentHorizontalResolution, CurrentVerticalResolution,
                CurrentRefreshRate, PNPDeviceID)
        monitors = @(Get-CimInstance -ClassName Win32_DesktopMonitor |
            Select-Object Name, ScreenHeight, ScreenWidth, PNPDeviceID)
    }
    Write-JsonFile -Value $environment -Path (Join-Path $evidenceRoot 'environment.json')

    $runtimeItem = Get-Item -LiteralPath $RuntimePackage
    if ($runtimeItem.PSIsContainer) {
        Get-ChildItem -LiteralPath $runtimeItem.FullName -Recurse -File |
            Get-FileHash -Algorithm SHA256 |
            Select-Object Path, Algorithm, Hash |
            Export-Csv -LiteralPath (Join-Path $evidenceRoot 'runtime-file-hashes.csv') -NoTypeInformation
        $installedExecutable = Join-Path $runtimeItem.FullName 'sokoban.exe'
        if (Test-Path -LiteralPath $installedExecutable -PathType Leaf) {
            Get-AuthenticodeSignature -FilePath $installedExecutable |
                Select-Object Status, StatusMessage, Path, SignerCertificate, TimeStamperCertificate |
                ConvertTo-Json -Depth 5 |
                Set-Content -LiteralPath (Join-Path $evidenceRoot 'runtime-signature.json') -Encoding utf8
        }
    } else {
        Get-FileHash -LiteralPath $runtimeItem.FullName -Algorithm SHA256 |
            Select-Object Path, Algorithm, Hash |
            Export-Csv -LiteralPath (Join-Path $evidenceRoot 'runtime-package-hash.csv') -NoTypeInformation
    }

    if ($Installer) {
        $installerItem = Get-Item -LiteralPath $Installer
        Get-FileHash -LiteralPath $installerItem.FullName -Algorithm SHA256 |
            Select-Object Path, Algorithm, Hash |
            Export-Csv -LiteralPath (Join-Path $evidenceRoot 'installer-hash.csv') -NoTypeInformation
        Get-AuthenticodeSignature -FilePath $installerItem.FullName |
            Select-Object Status, StatusMessage, Path, SignerCertificate, TimeStamperCertificate |
            ConvertTo-Json -Depth 5 |
            Set-Content -LiteralPath (Join-Path $evidenceRoot 'installer-signature.json') -Encoding utf8
    }

    $vulkanInfo = Get-Command vulkaninfo.exe -ErrorAction SilentlyContinue
    if (-not $vulkanInfo) {
        $vulkanInfo = Get-Command vulkaninfo -ErrorAction SilentlyContinue
    }
    if ($vulkanInfo) {
        $vulkanInfoOutput = Join-Path $evidenceRoot 'vulkaninfo-summary.txt'
        # A damaged optional Vulkan layer can make vulkaninfo fail before the
        # game's own launch check. Preserve that diagnostic as evidence rather
        # than preventing the rest of the release report from being created.
        $previousErrorActionPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & $vulkanInfo.Source --summary *> $vulkanInfoOutput
            "vulkaninfo exit code: $LASTEXITCODE" |
                Add-Content -LiteralPath $vulkanInfoOutput
        } finally {
            $ErrorActionPreference = $previousErrorActionPreference
        }
    } else {
        'vulkaninfo was not found on PATH.' |
            Set-Content -LiteralPath (Join-Path $evidenceRoot 'vulkaninfo-summary.txt') -Encoding utf8
    }

    $dxdiag = Join-Path $env:WINDIR 'System32/dxdiag.exe'
    if (Test-Path -LiteralPath $dxdiag) {
        $dxdiagOutput = Join-Path $evidenceRoot 'dxdiag.txt'
        $dxdiagProcess = Start-Process -FilePath $dxdiag -ArgumentList @('/whql:off', '/t', $dxdiagOutput) -PassThru
        if (-not $dxdiagProcess.WaitForExit(60000)) {
            Stop-Process -Id $dxdiagProcess.Id -ErrorAction SilentlyContinue
            'dxdiag did not complete within 60 seconds.' |
                Set-Content -LiteralPath $dxdiagOutput -Encoding utf8
        }
    }

    $packageGate = Join-Path $PSScriptRoot 'ValidateShippingPackage.ps1'
    $gateLog = Join-Path $evidenceRoot 'package-gate.log'
    try {
        & $packageGate -Package $RuntimePackage -LaunchSeconds $LaunchSeconds *>&1 |
            Tee-Object -FilePath $gateLog
        $results.automatedPackageGate = 'pass'
    } catch {
        $_ | Out-String | Add-Content -LiteralPath $gateLog -Encoding utf8
        $results.automatedPackageGate = 'fail'
        $results.packageGateError = $_.Exception.Message
    }
    Copy-LogTail -Destination (Join-Path $evidenceRoot 'sokoban-log-tail.txt')

    if ($SkipManualChecklist) {
        $results.manualChecklist = 'skipped by operator'
    } else {
        Write-Host ''
        Write-Host 'Manual release acceptance' -ForegroundColor Yellow
        Write-Host 'Launch the package yourself and complete each check before answering.'
        $checklist = @(
            Get-ChecklistResult -Id 'title-and-save' -Prompt 'Title screen appears; start a game, finish a puzzle, restart, and confirm the save resumes.'
            Get-ChecklistResult -Id 'display-settings' -Prompt 'Change display mode, VSync, frame cap, and anti-aliasing; confirm stable rebuilds and persistence.'
            Get-ChecklistResult -Id 'focus-and-window' -Prompt 'Minimize for 30 seconds, restore, alt-tab, resize, and use available monitor/DPI changes without a hang or input burst.'
            Get-ChecklistResult -Id 'extended-play' -Prompt 'Play at least ten minutes with keyboard/mouse and controller; exercise level transitions, previews, loading, and animation.'
            Get-ChecklistResult -Id 'installer-lifecycle' -Prompt 'For an installer build: verify uninstall retains saves and upgrade retains saves/settings. Otherwise mark skipped.'
        )
        Write-JsonFile -Value $checklist -Path (Join-Path $evidenceRoot 'manual-checklist.json')
        $results.manualChecklist = if ($checklist.result -contains 'fail') { 'fail' } elseif ($checklist.result -contains 'skipped') { 'partial' } else { 'pass' }
    }

    if ($IncludeReleaseArtifacts) {
        New-Item -ItemType Directory -Path (Join-Path $evidenceRoot 'artifacts') | Out-Null
        Copy-Item -LiteralPath $RuntimePackage -Destination (Join-Path $evidenceRoot 'artifacts') -Recurse
        if ($Installer) {
            Copy-Item -LiteralPath $Installer -Destination (Join-Path $evidenceRoot 'artifacts')
        }
    }
} finally {
    $results.completedAt = (Get-Date).ToString('o')
    Write-JsonFile -Value $results -Path (Join-Path $evidenceRoot 'result.json')
    Remove-Item -LiteralPath $workingRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$zipPath = "$evidenceRoot.zip"
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $evidenceRoot '*') -DestinationPath $zipPath
Write-Host "Release-validation evidence ZIP: $zipPath"
if ($results.automatedPackageGate -ne 'pass' -or $results.manualChecklist -eq 'fail') {
    exit 1
}
