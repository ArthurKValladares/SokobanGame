$ErrorActionPreference = 'Stop'
Import-Module 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'C:/Program Files/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$sourcePath = Join-Path $PSScriptRoot 'CoreReviewProbe.cpp'
$outputDirectory = Join-Path $projectRoot 'out/code-quality-review/probes/core'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$objectPath = Join-Path $outputDirectory 'CoreReviewProbe.obj'
$executablePath = Join-Path $outputDirectory 'CoreReviewProbe.exe'
& cl /nologo /std:c++20 /EHsc /MTd "/I$projectRoot/src" "/I$projectRoot/third_party/SDL/include" "/Fo$objectPath" "/Fe$executablePath" $sourcePath "/Fd$outputDirectory/CoreReviewProbe.pdb" /link (Join-Path $projectRoot 'out/code-quality-review/Debug/sokoban_core.lib') (Join-Path $projectRoot 'out/code-quality-review/third_party/SDL/Debug/SDL3-static.lib') (Join-Path $projectRoot 'out/code-quality-review/Debug/sokoban_bc7enc16.lib') kernel32.lib user32.lib gdi32.lib winmm.lib imm32.lib ole32.lib oleaut32.lib version.lib uuid.lib advapi32.lib setupapi.lib shell32.lib dinput8.lib dbghelp.lib
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$probeDataDirectory = Join-Path $outputDirectory ('run-' + [guid]::NewGuid().ToString('N'))
& $executablePath $probeDataDirectory
exit $LASTEXITCODE

