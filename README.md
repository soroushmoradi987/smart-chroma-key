# Smart Chroma Key - Professional Green Screen Removal for OBS Studio

A professional chroma key (green screen removal) plugin for OBS Studio with advanced features for clean, broadcast-quality keying.

## Features

- **Intelligent Auto-Tune**: One-click automatic calibration — samples your green screen from frame borders and sets optimal parameters
- **Advanced Hair Recovery**: 8-neighbor structure analysis + screen subtraction (Ultimatte-style) preserves thin hair strands
- **Dual Despill System**: Removes green spill from skin and edges using two complementary methods
- **5×5 Gaussian Alpha Blur**: Smooth, natural-looking edges
- **Full Color Correction**: Brightness, contrast, saturation, gamma, temperature, and tint controls
- **Key Saturation Boost**: Boosts saturation only for keying computation, improving detection without affecting output

## Installation

### macOS
1. Download `smart-chroma-key-macos.zip` from [Releases](../../releases)
2. Extract and copy `smart-chroma-key.plugin` to:
   ```
   ~/Library/Application Support/obs-studio/plugins/
   ```
3. Restart OBS Studio

### Windows
1. Download `smart-chroma-key-windows.zip` from [Releases](../../releases)
2. Extract the `smart-chroma-key` folder
3. Copy contents to your OBS plugins directory:
   ```
   C:\Program Files\obs-studio\obs-plugins\64bit\smart-chroma-key.dll
   C:\ProgramData\obs-studio\plugins\smart-chroma-key\data\  (effect + locale files)
   ```
   Or for portable installs, copy to `obs-studio\data\obs-plugins\smart-chroma-key\`
4. Restart OBS Studio

## Usage

1. In OBS, right-click your camera source → **Filters**
2. Click **+** under Effect Filters → Select **Smart Chroma Key**
3. Click **Auto Tune** for automatic green screen detection
4. Fine-tune with sliders if needed:
   - **Base Key / Edge Softness**: Core keying threshold and falloff
   - **Hair Detail / Hair Softness**: Recover thin hair strands
   - **Spill Reduction / Despill Light / Despill Dark**: Remove green color cast
   - **Edge Blur / Shrink / Grow**: Refine alpha edges

## Building from Source

### macOS
```bash
cmake --preset macos
cmake --build build_macos --config Release
```

### Windows
```bash
cmake --preset windows-x64
cmake --build build_x64 --config Release
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

Successful builds on GitHub Actions will produce build artifacts that can be downloaded for testing. These artifacts are commonly simple archives and will not contain package installers or installation programs.

### Building a Release

To create a release, an appropriately named tag needs to be pushed to the `main`/`master` branch using semantic versioning (e.g., `12.3.4`, `23.4.5-beta2`). A draft release will be created on the associated repository with generated installer packages or installation programs attached as release artifacts.

## Signing and Notarizing on macOS

Basic concepts of codesigning and notarization on macOS are explained in the correspodning [Wiki article](https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS) which has a specific section for the [GitHub Actions setup](https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS#setting-up-code-signing-for-github-actions).
