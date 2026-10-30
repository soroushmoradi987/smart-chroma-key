# Smart Chroma Key - Professional Green & Blue Screen Removal for OBS Studio

A professional chroma key plugin for OBS Studio with dedicated green-screen and blue-screen processing, advanced edge recovery, and broadcast-quality controls.

## Windows 1.4.1: Direct3D fix and bundled AI

The Classic shader used `line` as a local variable, which is reserved in HLSL.
Direct3D rejected the shader and the filter passed video through while showing
controls. The variable is now renamed without changing the algorithm, shader
errors are logged, and a failed shader is reported in the filter properties.

Windows packages include **AI - Robust Video Matting (RVM)** using the official
FP32 MobileNetV3 ONNX model and a private ONNX Runtime CPU backend. Inference is
asynchronous with one job in flight, recurrent state, and paired foreground,
alpha and input frames. The processing resolution is selectable from 320 to 1280;
640 is the default. This does not add Apple Vision to Windows. CPU performance
and real-camera edge quality depend on the recipient's system and footage.

The Windows AI workflow builds a ZIP and runs real libobs/Direct3D checks for the
old shader failure, corrected Classic pixels, opacity/brightness controls, AI
inference, preview controls, switching methods, and Classic without AI files.
See [manual ZIP installation](scripts/WINDOWS-INSTALL.txt). Extract the complete
`smart-chroma-key` folder into `%ProgramData%\obs-studio\plugins` while OBS is closed.

## Features

- **Automatic Screen Detection**: Detects green versus blue and estimates the actual screen chromaticity from hundreds of robust border samples
- **Adaptive Chroma Matte**: Brightness-independent chromaticity matching removes folds and shadows without widening the key into foreground colors
- **AI Person & Face Protection on macOS**: Apple Vision produces a low-latency Balanced live-video person matte and uses face geometry to confine opaque hair-color reconstruction
- **Protected Green/Blue Clothing**: Semantic subject confidence preserves intentionally colored foreground garments while screen-family removal stays aggressive outside the person
- **Full Screen-Family Removal**: Removes bright, dark, pale, olive, and uneven shades across the selected green or blue screen outside the protected subject
- **Green + Blue Screen Modes**: Channel-aware keying, hair recovery, and despill for either screen color
- **Advanced Hair Recovery**: Fractional screen subtraction plus local structure analysis preserves thin and semi-transparent strands
- **Natural Hair-Color Reconstruction**: Searches a wider neighborhood for clean hair, matches its luminance, and uses Hair Color as a fallback instead of leaving green/blue contamination
- **Motion-Aware Temporal Denoise**: Suppresses flickering matte pixels without trailing moving body edges
- **Edge Decontamination + Despill**: Reconstructs partial-edge color from nearby screen samples before targeted green/blue spill suppression
- **Edge-Aware 3×3 Matte Refinement**: Shared GPU samples provide transition-only smoothing and true morphological shrink/grow
- **Full Color Correction**: Brightness, contrast, saturation, gamma, temperature, and tint controls
- **Key Saturation Boost**: Boosts saturation only for keying computation, improving detection without affecting output
- **Optimized Rendering**: Cached GPU parameter handles and a fast path that skips neighbor sampling when hair recovery is disabled

## Live AI hair fringe in 1.3.7

With AI protection on, the half-covered hair fringe still sparkled in live
video. The person mask was gated by the hue classifier, which flips between
"screen" and "subject" for pixels that keep the screen hue, so the mask
switched on and off per pixel and per frame. The gate now also requires
continuous screen coverage, and the temporal bound treats a pixel as
fractional when either the current or previous alpha is fractional, so
history holds a fringe pixel that noise briefly pushes to solid. The
`ai-fringe` regression (noisy 4:2:0 fringe under a person mask) fails on
1.3.5 and 1.3.6 with ~40 toggling pixels and passes with none.

## Dense hair band in 1.3.6

Inside the hair band, luma jumps between dark strands, grey hairs and
highlights as bright as the screen. The RGB colour-line fit read those pixels
as screen and punched 4:2:0-sized holes (checkerboard with AI on), while dark
hair measured against nearby skin looked half transparent (see-through halo
with AI off). The band now gets a hole-free lower bound from screen dominance,
low-pass filtered over 3x3 and measured against the screen (the sampled key is
the floor when all nearby samples are hair). **Hair Density** (default 0.8)
works like Ultra Key's Clip Black/White: sparse hair becomes a solid band. It
applies only where coverage stays partial 5 px away in most directions, so
narrow body edges and blends stay physically exact, and colour is unmixed with
physical coverage to avoid a dark rim.

On a 45-frame raw camera clip, see-through pixels in the dense hair band fell
from 941 to 13 per frame without AI and from 169 to 7 with AI, with no new
flicker or screen noise. Regressions pass at Hair Density 0 and 0.8.

## Stable hair in live video in 1.3.5

Single frames looked clean, but in live video faint strands switched on and
off between frames, showing as twinkling black specks. Steep per-pixel gates
on noisy measurements made each strand pixel a near-binary decision, and
temporal filtering only engaged on frames where the pixel was already
recognized. Now:

* Temporal filtering engages on ungated strand evidence, so a strand that a
  noisy frame drops is held by its history. A pixel the current frame sees as
  pure screen still never revives history, so trails stay clear.
* Motion is judged from a five-pixel average, so sensor noise no longer counts
  as movement and switches history off at random.
* Fractional hair may move further toward its history (±0.45 instead of
  ±0.18); solid edges keep the tight bound.
* Pedestal knees follow the whole line, so diagonal strands whose coverage
  splits between pixels stay continuous instead of dashed.
* Luma-recovered strands take the nearby hair colour (or **Hair Color**)
  instead of the clipped black that unmixing a screen-chroma pixel produces.

On a 90-frame raw camera clip with AI protection, visibly sparkling pixels
around the hair fell from 23 per frame (1.3.4) to 5, versus 75 in 1.3.2.
`live-strand` regressions run a faint anti-aliased strand through 24 noisy
4:2:0 frames with history.

## Flyaway hair and installer in 1.3.4

Cameras deliver 4:2:0 video and smooth chroma further, so a one-pixel hair
strand often exists only in luma: its colour still matches the screen. Hue and
screen-family tests therefore removed flyaway hair entirely. Like Premiere
Pro's Ultra Key, the matte now also uses continuous colour difference against
the local screen (16 nearby samples), unmixed with the darkest clean nearby
hair. Where luma coverage clearly exceeds chroma coverage along a line, a
luma-guided estimate recovers the strand; it uses nearby hair luma, or
**Hair Color** for isolated strands, and is disabled when hair and screen have
similar brightness (dark hair on blue). A pedestal, line continuity and a
flat-area skip keep screen noise transparent and limit the extra GPU cost.
Partly covered pixels use continuous coverage instead of an on/off hue
decision, and the Vision mask protects only its confident interior, removing
checkered and blocky hair outlines.

The macOS installer is bilingual (English/Persian) with light and dark artwork,
compatibility checks, a non-relocatable bundle and a clean-upgrade preinstall
script. `scripts/build-macos-installer.zsh` signs, notarizes and staples when
`CODESIGN_IDENT`, `CODESIGN_IDENT_INSTALLER` and `NOTARIZATION_PROFILE` are set.
Regenerate the artwork with `swift scripts/generate-installer-art.swift`.

## Hair noise and SDK cache repair in 1.3.2

The wide boundary fit now includes face-guided hair and very dark fringes whose
immediate neighbors still have the screen hue. Foreground references use RGB
distance with a sensor-noise tolerance, avoiding unstable chromaticity ratios
near black. The same foreground/background fit controls both coverage and color;
it can override a coarse person mask only when current color evidence agrees.

GPU regressions include 32-frame sequences with independent noise in each RGB
channel, brown/blond/black hair, green/blue screens, and coarse person masks.
Motion, stale masks, protected clothing, thin strands and body edges remain
covered. These are synthetic checks; live-camera quality and full-resolution
performance still need verification.

After an Xcode update removes an SDK, configuration automatically refreshes the
nested OBS cache if it contains missing SDK paths. Nested configure diagnostics
are saved to `.deps/obs-configure-universal.log` and included in failures instead
of only reporting a child process exit code.

## Body-edge correction in 1.3.1

Soft body edges now search eight directions six pixels away for clean foreground
and screen colors. Only a confident fit to the current camera pixel can refine
coverage and replace darkened RGB. This lets antialiased boundaries override an
overly opaque person mask, while the face-guided hair region keeps its existing
reconstruction. Intentional opaque black trim is preserved. This conditional path
adds eight GPU texture samples; camera/full-resolution performance still needs
separate verification.

Body regressions cover 1-, 3-, and 5-pixel transitions on neutral, skin, white and
dark foregrounds, with and without an overestimated semantic mask. Both white and
black composites are checked for color error, plus an opaque dark-seam control.

## Edge reconstruction in 1.3.0

The GPU estimates clean foreground and screen colors around each boundary and fits
`C = alpha * F + (1 - alpha) * B`. A color-fit confidence gate keeps this solution
away from unrelated textures and protected clothing. Dark mixed strands are
excluded from the screen estimate to avoid underestimating black-hair coverage.
Reliable foreground color replaces unstable screen subtraction at fractional
edges, without applying a second despill that would discolor blond hair.

Separate raw-input history rejects motion and cuts before accumulating both alpha
and reconstructed RGB. Output history uses RGBA16F to reduce accumulation rounding.
This adds a GPU copy pass and approximately 32 MiB of history storage at 1080p
compared with the previous two RGBA8 output buffers. The algorithm also works
without AI; Apple Vision on macOS provides person protection and an approximate
face-guided hair region, not individual strand or hair-color segmentation.

Run `zsh scripts/test-shader-hair.zsh` for real libobs/OpenGL shader tests of thin
strands, isolated and 3x3 correlated noise, dense hair holes, stale masks, clothing,
and 32-frame noisy mixtures of black, brown, blond and skin colors on green/blue.
After a macOS Release build, `zsh scripts/test-filter-pipeline-macos.zsh` loads
the actual plugin in an isolated libobs process and checks eight rendered frames,
including fractional edges and vacated screen pixels.
The sequence tests measure both white and black composites and reject motion
trails and scene-cut history. These synthetic checks do not establish camera,
full-resolution frame-rate, or Windows GPU quality; those require separate runs.

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
   - **AI Person & Hair Protection**: Keep enabled on macOS so green clothing is protected and hair reconstruction is face-guided
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
The AI protection path requires macOS 12 or newer and runs locally through Apple Vision; camera frames are not uploaded.

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
