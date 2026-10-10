# Exp3r1ment4l

[Русский](README.ru.md) · [Download VST3](https://github.com/lowkGenuine/Exp3r1ment4l/releases/tag/v0.1.4) · [User guide](docs/USER_GUIDE.md)

[Support the author](https://www.donationalerts.com/r/lesha_diabet_)

**Downloads:** [Windows x64](https://github.com/lowkGenuine/Exp3r1ment4l/releases/download/v0.1.4/Exp3r1ment4l-0.1.4-win-vst3.zip) · [macOS Intel / Apple Silicon](https://github.com/lowkGenuine/Exp3r1ment4l/releases/download/v0.1.4/Exp3r1ment4l-0.1.4-mac-vst3.zip).

Code → Download ZIP and the automatic Source code archives are developer sources. Installable plug-ins are the `win-vst3.zip` and `mac-vst3.zip` files in Releases.

An experimental VST3 effect with five processing modules, plus a separate additive/FM synthesizer. Windows x64 and macOS universal builds share the light, graph-based interface and live output analyser.

**Enable a module → shape its sound → compare with Bypass → Undo.** The checkbox changes the audio; the module name opens its controls. The editor shows the actual output waveform and spectrum, with Mix, Output and Bypass always available.

| Module | What it does |
| --- | --- |
| Grains | Four overlapping heads read a live stereo buffer. Grain length, density, transposition, buffer position and freeze. |
| Permute | Clocked delay with XOR, bit rotation, reverse and stutter operators; controllable feedback. |
| Morph | Interpolate the main spectrum with a sidechain or looping audio file; frequency focus and spectral motion. |
| Spectral | Phase-continuous freeze, frequency warp, magnitude smear and spectral dynamics. |
| Pitch Grid | Map spectral components to notes in a selected root/scale, with amount and transposition. |

These are original implementations inspired by the kinds of experiments possible in Sumu, MORPH, Permut8, Synplant, SpecOps, PITCHMAP, Palindrome and MYTH. They do not embed those products or claim algorithmic equivalence. Capture is harmonic analysis, not an AI model; Pitch Grid does not separate instruments or identify individual notes in a mix.

## Install

Each platform archive contains two complete bundles: **Exp3r1ment4l.vst3** (effect) and **Exp3r1ment4l Synth.vst3** (MIDI instrument). Copy each entire folder, including `Contents`:

- **Windows x64:** copy both bundles into `C:\Program Files\Common Files\VST3`, then rescan your DAW.
- **macOS 11+:** copy both universal bundles into `~/Library/Audio/Plug-Ins/VST3` for your account or `/Library/Audio/Plug-Ins/VST3` for all users, then rescan. Intel and Apple Silicon are included in one download. The bundles use an ad-hoc signature and are not Apple notarized; macOS may require approval in Privacy & Security.
- **Linux:** source builds remain available; this release supplies Windows and macOS binaries.

**Exp3r1ment4l** is an audio effect with Grains, Permute, Morph, Spectral and Pitch Grid. It has no Synth tab, internal synthesis or synth automation parameters: put it after an existing instrument or on recorded audio. **Exp3r1ment4l Synth** is the separate eight-voice, 32-partial additive/FM MIDI instrument with editable harmonics, four source profiles, capture/mutation and the same five processing modules. Synth starts enabled in the instrument. MIDI notes, sustain and pitch bend drive that instrument; the effect can use incoming MIDI for Pitch Grid. Audio arriving from another instrument does not carry its MIDI notes or automatically generate new notes.

See [the user guide](docs/USER_GUIDE.md), [build instructions](docs/BUILD.md) and [action hierarchy](docs/DESIGN.md).

## Controls

Synth has Wave and Harmonics views. Choose Sine, Saw, Square or Soft as the source amplitude spectrum, then draw the harmonic bars to shape its sound. Soft uses odd harmonics with 1/n² amplitudes; these are harmonic profiles for the additive engine, not wavetable oscillators. On the Wave view, horizontal and vertical swipes adjust Tone and FM; the wheel adjusts Spread. Attack and Release nodes shape the envelope, with their values always visible. The LFO keeps Rate, Depth, Target and Sync visible beside its interactive graph. Live output waveform and FFT and the keyboard remain in view.

The Synth window opens at 1120 × 760. The effect window opens at 960 × 700 and expands to 960 × 840 with More. In other modules, horizontal and vertical movement on the XY surface changes the first two parameters; the mouse wheel changes the third. The wheel parameter is Pitch in Grains, Feedback in Permute, Motion in Morph, Dynamics in Spectral, and Transpose in Pitch Grid. Numeric values also support dragging or scrolling; double-click to enter a value.

## Latency and release status

Version 0.1.4 is a development alpha. Synth and the time-domain chain add **zero samples of audio-buffer latency** when Morph, Spectral and Pitch Grid are off. Enabling any of those spectral modules adds **1024 samples** (21.33 ms at 48 kHz). The DAW buffer, Attack and intentional grain/delay effects also affect the response. Change spectral module switches with playback stopped so the host can update delay compensation.

These packages are compiled without running the plug-ins or host/listening tests, at the author’s request. The source was recovered from a previous development conversation; the missing Grains/Permute/FFT section was reconstructed locally.

## Build

C++17, CMake 3.22+, JUCE 7.0.12. CMake fetches this JUCE release by default; an existing checkout can be provided with `-DJUCE_PATH=/path/to/JUCE`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DEXP3_BUILD_CHECKS=OFF
cmake --build build --config Release --target Exp3r1ment4l_VST3 Exp3r1ment4lInstrument_VST3 --parallel 2
```

AGPL-3.0-or-later. Dependencies and their original notices are described in [THIRD_PARTY.md](THIRD_PARTY.md). Source must accompany redistribution under the applicable licence terms.
