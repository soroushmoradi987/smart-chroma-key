# Smart Chroma Key - Professional Green & Blue Screen Removal for OBS Studio

A professional chroma key plugin for OBS Studio with dedicated green-screen and blue-screen processing, advanced edge recovery, and broadcast-quality controls.

## Features

- **Automatic Screen Detection**: Detects green versus blue and estimates the actual screen chromaticity from hundreds of robust border samples
- **Adaptive Chroma Matte**: Brightness-independent chromaticity matching removes folds and shadows without widening the key into foreground colors
- **Green + Blue Screen Modes**: Channel-aware keying, hair recovery, and despill for either screen color
- **Advanced Hair Recovery**: Fractional screen subtraction plus local structure analysis preserves thin and semi-transparent strands
- **Edge Decontamination + Despill**: Reconstructs partial-edge color from nearby screen samples before targeted green/blue spill suppression
- **Edge-Aware 3×3 Matte Refinement**: Shared GPU samples provide transition-only smoothing and true morphological shrink/grow
- **Full Color Correction**: Brightness, contrast, saturation, gamma, temperature, and tint controls
- **Key Saturation Boost**: Boosts saturation only for keying computation, improving detection without affecting output
- **Optimized Rendering**: Cached GPU parameter handles and a fast path that skips neighbor sampling when hair recovery is disabled

## Installation

### macOS
1. Download `smart-chroma-key-<version>-macos-universal.pkg` from [Releases](../../releases).
2. Close OBS Studio and run the graphical installer wizard.
3. Restart OBS Studio.

For manual or portable installation, download `smart-chroma-key-macos.zip`, extract it, and copy `smart-chroma-key.plugin` to:
   ```
   ~/Library/Application Support/obs-studio/plugins/
   ```

### Windows
1. Download `smart-chroma-key-<version>-windows-x64-installer.exe` from [Releases](../../releases).
2. Close OBS Studio and run the modern installer wizard. It installs the complete plugin bundle under the OBS-recommended `C:\ProgramData\obs-studio\plugins` location and includes an uninstaller.

If you prefer manual installation or a portable OBS setup, download `smart-chroma-key-windows.zip`, extract it, and copy its contents to your OBS plugins directory:
   ```
   C:\ProgramData\obs-studio\plugins\smart-chroma-key\bin\64bit\smart-chroma-key.dll
   C:\ProgramData\obs-studio\plugins\smart-chroma-key\data\  (effect + locale files)
   ```
   Or for portable installs, copy to `obs-studio\data\obs-plugins\smart-chroma-key\`
5. Restart OBS Studio

To build the Windows wizard locally on a Windows machine:

```powershell
pwsh .github/scripts/Build-Installer-Windows.ps1 -Configuration Release -InstallDependencies
```

The generated files are written to `release/`, including `smart-chroma-key-<version>-windows-x64-installer.exe`.

Unless your release pipeline is configured with trusted signing credentials, locally generated Windows installers are unsigned and may trigger SmartScreen. Public macOS distribution should use a Developer ID Installer certificate and notarization.

## Usage

1. In OBS, right-click your camera source → **Filters**
2. Click **+** under Effect Filters → Select **Smart Chroma Key**
3. Leave **Screen Type** on **Auto Detect (Recommended)** for automatic green/blue selection
4. Click **Analyze Screen Now** whenever the camera, screen, or lighting changes
5. Fine-tune with sliders if needed:
   - **Base Key / Edge Softness**: Core keying threshold and falloff
   - **Hair Detail / Hair Softness**: Recover thin hair strands
   - **Spill Reduction / Despill Light / Despill Dark**: Remove green or blue color cast
   - **Edge Blur / Shrink / Grow**: Refine alpha edges

## Building from Source

### macOS
```bash
./scripts/build-macos-installer.zsh Release
```

This creates a universal graphical `.pkg` installer and validates that it contains the plugin payload. The lower-level build commands remain `cmake --preset macos` and `cmake --build build_macos --config Release`.

### Windows
```bash
cmake --preset windows-x64
cmake --build build_x64 --config Release
pwsh .github/scripts/Build-Installer-Windows.ps1 -Configuration Release -InstallDependencies
```

## License

GNU General Public License v2.0 - see [LICENSE](LICENSE) for details.

## Credits

Developed by Soroush Moradi.

Suggested reading to get up and running:

* [Getting started](https://github.com/obsproject/obs-plugintemplate/wiki/Getting-Started)
* [Build system requirements](https://github.com/obsproject/obs-plugintemplate/wiki/Build-System-Requirements)
* [Build system options](https://github.com/obsproject/obs-plugintemplate/wiki/CMake-Build-System-Options)

## GitHub Actions & CI

Default GitHub Actions workflows are available for the following repository actions:

* `push`: Run for commits or tags pushed to `master` or `main` branches.
* `pr-pull`: Run when a Pull Request has been pushed or synchronized.
* `dispatch`: Run when triggered by the workflow dispatch in GitHub's user interface.
* `build-project`: Builds the actual project and is triggered by other workflows.
* `check-format`: Checks CMake and plugin source code formatting and is triggered by other workflows.

The workflows make use of GitHub repository actions (contained in `.github/actions`) and build scripts (contained in `.github/scripts`) which are not needed for local development, but might need to be adjusted if additional/different steps are required to build the plugin.

### Retrieving build artifacts

Successful packaged builds produce both portable archives and graphical installers: a universal `.pkg` on macOS and an Inno Setup `.exe` on Windows. CI inspects the macOS payload and performs a silent install/uninstall smoke test for the Windows wizard before uploading artifacts.

### Building a Release

To create a release, an appropriately named tag needs to be pushed to the `main`/`master` branch using semantic versioning (e.g., `12.3.4`, `23.4.5-beta2`). A draft release will be created on the associated repository with generated installer packages or installation programs attached as release artifacts.

## Signing and Notarizing on macOS

Basic concepts of codesigning and notarization on macOS are explained in the correspodning [Wiki article](https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS) which has a specific section for the [GitHub Actions setup](https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS#setting-up-code-signing-for-github-actions).
