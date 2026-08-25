# Shipping Release Validation

P4-7 is a release acceptance gate. It is not complete until each row in the
hardware matrix below has a recorded passing result for the exact signed
installer or Runtime ZIP that will be published.

## Package gate

Build the final runtime package and retain its matching Symbols ZIP:

```powershell
cmake --preset shipping
cmake --build --preset shipping
cpack --preset shipping
```

On a clean standard-user Windows installation, use a separate
release-validation checkout (or copy this script beside the artifact), copy
only the Runtime ZIP, and run the package gate:

```powershell
powershell -ExecutionPolicy Bypass -File .\packaging\ValidateShippingPackage.ps1 `
  -Package .\out\shipping\Sokoban3D-0.1.0-Windows-x64-Runtime.zip
```

The script extracts the ZIP into a fresh temporary directory, rejects missing
licenses and accidental PDBs, verifies every `content.index` path and size,
and confirms that `sokoban.exe` stays alive for ten seconds when launched from
that extracted directory. It deliberately closes the game after this check.
Use `-KeepExtracted` to retain the exact runtime tree for manual inspection.
For an Inno Setup release, run the same script against the installer-stage
Runtime directory after installation, then validate uninstall and upgrade
separately.

Do not clear an existing player's data to simulate a clean install. Use a new
Windows user account, a VM snapshot, or a physical machine with no prior
Sokoban 3D profile.

## Manual acceptance

For every matrix row, use a fresh profile and verify all of the following:

1. The package gate passes, the title screen appears, and no fatal-error dialog
   or renderer error appears in the rotating log.
2. Start a new game, finish at least one puzzle, return to the title screen,
   restart the game, and confirm the save resumes correctly.
3. Change display mode, VSync, frame cap, and anti-aliasing; verify that the
   settings persist and each resulting swapchain rebuild is stable.
4. Minimize for at least 30 seconds, restore, alt-tab away and back, and resize
   or move the window between available monitors. Confirm no hang, excessive
   CPU use, device-loss dialog, or input burst occurs.
5. Play for at least ten minutes with a controller and keyboard/mouse. Exercise
   level transitions, screen previews, loading fallback behavior, and animated
   characters. Record any validation, renderer, or asset errors from the log.
6. Verify that uninstall removes the installed files but retains user saves;
   verify an installer upgrade preserves both saves and settings. For signed
   releases, record a successful Authenticode verification of the executable
   and installer.

## Supported-hardware matrix

The release renderer requires a Vulkan 1.3-capable GPU with dynamic rendering,
synchronization2, cube-map arrays, extended dynamic state, and the required
descriptor capacity. Test current vendor drivers; record the exact driver
version reported by the OS/vendor utility rather than treating a marketing
driver family as sufficient evidence.

| OS and display setup | GPU / driver version | Package gate | Manual acceptance | Owner / date | Result |
| --- | --- | --- | --- | --- | --- |
| Clean supported Windows 10 x64, single monitor | NVIDIA Vulkan 1.3-capable GPU / _record exact version_ |  |  |  |  |
| Clean supported Windows 11 x64, single monitor | AMD Vulkan 1.3-capable GPU / _record exact version_ |  |  |  |  |
| Clean supported Windows 11 x64, single monitor | Intel Vulkan 1.3-capable integrated GPU / _record exact version_ |  |  |  |  |
| Clean supported Windows 11 x64, mixed-DPI or multi-monitor | A GPU from a row above / _record exact version_ |  |  |  |  |

Add a row for every laptop, handheld, or distributor-supported configuration
that is part of the actual launch promise. A failing compatibility check is a
supported rejection only when the app shows the unsupported-hardware message;
otherwise it blocks release.
