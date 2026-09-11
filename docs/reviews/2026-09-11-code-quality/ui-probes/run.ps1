$ErrorActionPreference = 'Stop'
Import-Module 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'C:/Program Files/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$probeOutput = Join-Path $projectRoot 'out/code-quality-review/probes/ui'
New-Item -ItemType Directory -Path $probeOutput -Force | Out-Null
$probeExecutable = Join-Path $probeOutput 'UiReviewProbe.exe'
& cl /nologo /std:c++20 /EHsc /MTd /DSOKOBAN_ENABLE_DEBUG_UI=1 "/I$projectRoot/src" "/I$projectRoot/third_party/SDL/include" "/Fo$probeOutput/UiReviewProbe.obj" "/Fe$probeExecutable" (Join-Path $PSScriptRoot 'UiReviewProbe.cpp') "/Fd$probeOutput/UiReviewProbe.pdb" /link (Join-Path $projectRoot 'out/code-quality-review/Debug/sokoban_ui.lib') (Join-Path $projectRoot 'out/code-quality-review/Debug/sokoban_core.lib') (Join-Path $projectRoot 'out/code-quality-review/third_party/SDL/Debug/SDL3-static.lib') (Join-Path $projectRoot 'out/code-quality-review/Debug/sokoban_bc7enc16.lib') kernel32.lib user32.lib gdi32.lib winmm.lib imm32.lib ole32.lib oleaut32.lib version.lib uuid.lib advapi32.lib setupapi.lib shell32.lib dinput8.lib dbghelp.lib
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $probeExecutable $projectRoot
exit $LASTEXITCODE
