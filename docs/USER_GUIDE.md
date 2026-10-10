# Using Exp3r1ment4l

## Start with the sound

1. Insert **Exp3r1ment4l** on an audio track to use its five effects, or **Exp3r1ment4l Synth** on an instrument track to play the Synth engine from MIDI.
2. The FX plug-in has five modules: Grains, Permute, Morph, Spectral and Pitch Grid. Click a module's small checkbox to enable it. It also opens that module's controls. Clicking its name opens controls without changing the processing chain. The Synth engine is available only in the separate MIDI instrument.
3. Use the selected module's controls. In Grains, Permute, Morph, Spectral and Pitch Grid, the XY surface changes the first two parameters and the wheel changes the third: Pitch, Feedback, Motion, Dynamics and Transpose respectively. Numeric values also respond to dragging or the wheel; double-click or press Enter to type. Hold Shift while dragging for fine adjustments. Arrow keys adjust the focused value.
4. Use Mix for wet/dry blending and Output for gain. Bypass compares with the input at the same latency. Undo restores the previous edit, including captured or mutated Synth timbre in the instrument and the donor-file reference. The editor remembers up to 32 actions while open; reopening the editor starts a fresh history.

Presets provide starting points: Init, Cloud, Corrupt, Glass, Grain Drift and Grid in the FX plug-in; the instrument offers MIDI Drift instead of Grain Drift. Mutate varies only the selected module. Enabling a module never erases its previous settings. EN/RU switches interface labels; module and algorithm names stay stable.

## Modules

**Synth instrument only:** The dedicated **Exp3r1ment4l Synth** window has Wave and Harmonics views. Choose Sine, Saw, Square or Soft as the source amplitude spectrum. These are harmonic profiles for the additive engine, not wavetable oscillators; Soft uses odd harmonics with 1/n² amplitudes. In Harmonics, draw the 32 bars to set the timbre directly; the graph updates the sound. In Wave, horizontal movement changes Tone, vertical movement changes FM, and the wheel changes Spread. Attack and Release nodes shape the envelope, with both values always visible. The instrument's LFO section keeps Rate, Depth, Target and Sync visible and includes an interactive graph; its Synth FM target exists only in this instrument. The live output waveform and FFT and the onscreen keyboard stay visible. Level controls the synth contribution before the effects. Capture reads the most recently analysed sound entering the spectral stage after Synth, Grains and Permute in the instrument's signal chain. The onscreen piano plays C4; enable Synth first. External MIDI supports eight voices, velocity, sustain CC64 and a ±2-semitone pitch wheel. The synthesis engine is channel-agnostic; this version does not implement MPE or separate per-channel instruments. A captured seed survives project save/restore. The FX plug-in has no Synth, Capture, keyboard or audio synthesis.

**Grains:** Size sets grain duration, Density schedules grain heads, Pitch shifts playback and Position reads further into the recent live buffer. Freeze stops writing new input to that buffer. Feed audio before freezing; a freshly empty buffer is silent. Up to four heads overlap, so the requested density can be constrained by available heads and grain duration.

**Permute:** Clock controls the processing clock. Disorder sets operator strength; Feedback feeds the delay. XOR and Rotate operate on quantised sample words; Reverse reads short segments backwards; Stutter repeats segments. This is an intentionally audible digital effect, not transparent saturation.

**Morph:** Connect your DAW's sidechain or choose Audio file and Load. Blend interpolates magnitudes, Focus weights frequency regions and Motion shifts donor content. The main signal supplies phase. Files loop from their first ten seconds, with rate conversion to the host. Missing donors leave the main signal available and the panel shows the missing/loading status. File paths are saved, not the audio bytes: keep the original file available when reopening a project. For bounded audio-thread ownership, each instance retains at most eight loaded file buffers; reopen the instance to load more.

**Spectral:** Warp remaps spectral components; Smear smooths magnitudes across frames; Dynamics alters spectral contrast. Freeze holds magnitudes and continues phase evolution. Activate the module and feed audio before freezing. Freeze is intentionally indefinite while enabled, even though the reported host tail is eight seconds.

**Pitch Grid:** Amount blends spectral note mapping. Transpose offsets mapped frequencies. Root and Scale set the grid; Free applies transposition without snapping. This produces spectral pitch effects on complex audio and is not monophonic vocal tuning or polyphonic source separation.

The effect editor opens at 960 × 700 pixels. Opening More expands it to 960 × 840. The dedicated MIDI instrument editor opens at 1120 × 760 pixels and shows its oscillator, envelope, LFO, output views and keyboard together.

## More

The LFO has Depth, Rate, Target and Sync. The FX plug-in targets Morph Blend, Spectral Warp, Grain Position, Permute Clock and Pitch Amount. The separate Synth instrument also offers Synth FM. Depth zero disables modulation. Rate is in Hz; Sync uses one LFO cycle per host beat when the host supplies tempo. When Sync is on, Rate is still displayed and is unavailable for editing; when tempo is unavailable, the saved manual rate is used. The Synth instrument keeps the LFO fields and interactive graph visible in its main layout. Modulation does not rewrite host automation values.

## Routing and latency

The two plug-ins have separate signal paths. The FX plug-in is `audio input → Grains → Permute → Morph / Spectral / Pitch Grid → Mix → Output`. The MIDI instrument is `MIDI input → Synth → Grains → Permute → Morph / Spectral / Pitch Grid → Mix → Output`; it uses the same five effects after its Synth engine. The effect plug-in has no internal Synth. The last three effects share one spectral analysis/resynthesis stage. Disabled engines are skipped. Synth, Grains and Permute add no algorithmic delay; enabling Morph, Spectral or Pitch Grid adds **1024 samples** of latency (about 21.3 ms at 48 kHz) for spectral processing. The host-compensated dry path and Bypass follow the active processing latency. Host buffer size and the Synth Attack setting also affect monitoring response.

The FX plug-in processes mono or stereo audio and supports an optional mono/stereo sidechain. The MIDI instrument receives MIDI and generates audio with its Synth engine, then can process that sound through the same five effects. Sidechain support and routing controls depend on your DAW.

The DSP smooths primary controls, rejects nonfinite values and limits internal feedback. It has no final mastering limiter; compare levels with Output when combining modules.
