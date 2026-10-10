# Building

The project uses JUCE 7.0.12. Requirements: C++17 compiler and CMake 3.22+. The source contains no proprietary plug-in code or binary dependencies. Point `JUCE_PATH` at an existing JUCE 7.0.12 checkout, or let CMake fetch that release.

## Windows

The Windows package is cross-compiled with LLVM-MinGW. A native Visual Studio build is also supported. From a developer terminal with CMake and a C++17 toolchain:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH="C:/path/to/JUCE-7.0.12" -DEXP3_BUILD_CHECKS=OFF
cmake --build build --config Release --target Exp3r1ment4l_VST3 Exp3r1ment4lInstrument_VST3 --parallel 2
```

Windows runtimes are linked statically for MinGW builds. Build outputs are written under `build/`.

## Linux

Ubuntu/Debian build packages:

```sh
sudo apt-get install build-essential cmake git pkg-config libasound2-dev \
  libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev \
  libxcomposite-dev libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev xvfb
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

WebKit and libcurl are not required. Build products are written under `build/`.

## macOS

Xcode command-line tools and CMake are required:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DEXP3_BUILD_CHECKS=OFF
cmake --build build --parallel 2
```

For current macOS SDKs, first apply `python3 Build/patch_juce_macos.py /path/to/JUCE` and supply that checkout as JUCE_PATH. The release workflow builds a universal bundle and adds an ad-hoc signature during packaging. Apple notarization is not performed. The release workflow compiles and packages only; it never runs a plug-in or test.

## Targets

- `Exp3r1ment4l_VST3`: effect bundle.
- `Exp3r1ment4lInstrument_VST3`: instrument bundle.
- `Exp3r1ment4l_Standalone` / `Exp3r1ment4lInstrument_Standalone`: optional standalone apps.
- `Exp3r1ment4lDSPTest`: dependency-free engine checks.
- `Exp3r1ment4lHostCheck`: loads the actual VST3 through JUCE's host APIs.
- `Exp3r1ment4lPreview`: checks actions and renders native component screenshots.

Build products are in `build/<target>_artefacts/Release/VST3`. Host-check syntax:

```sh
xvfb-run -a build/Exp3r1ment4lHostCheck \
  build/Exp3r1ment4l_artefacts/Release/VST3/Exp3r1ment4l.vst3
xvfb-run -a build/Exp3r1ment4lPreview /absolute/path/to/screenshots
```

For only the independent DSP test, without JUCE or desktop dependencies:

```sh
cmake -S . -B build-dsp -DEXP3_BUILD_PLUGIN=OFF
cmake --build build-dsp
ctest --test-dir build-dsp --output-on-failure
```

## Published packages

`.github/workflows/build.yml` builds Windows x64 and macOS universal binaries, then publishes a prerelease only after both builds complete. `Build/package.py` preserves complete bundle contents and Mac executable permissions. Source distributions include the exact pinned JUCE source and its compatibility patch. Historical test targets are opt-in (`EXP3_BUILD_CHECKS=ON`) and are not part of the release workflow.
