# Action hierarchy

The design follows lowkGenuine's EQ-01 direction: pale surface, dark type, restrained lines and values rather than decorative knobs. Hierarchy is assigned to what the user does.

| Priority | Actions | Placement |
| --- | --- | --- |
| First | Enable a process, change its main character, compare wet/dry, set output level | FX has five persistent module checkboxes; the MIDI instrument has its Synth controls; persistent Mix, Output, Bypass |
| Second | Choose an operator/scale/donor, freeze, capture, audition, mutate, undo | Contextual FX module actions; Capture, keyboard, and Synth actions belong only to the MIDI instrument |
| Third | Modulate, tune Synth attack/FM, sync tempo | More opens one compact continuation of the same panel; Synth FM modulation is available only in the MIDI instrument |

Two independent agents reviewed the hierarchy and EQ-01 interaction choices. Their shared recommendation: separate activation from selection, avoid giant permanent visualisers, keep only the current module's controls visible, retain settings when disabling, and let one action immediately produce a useful result.

The FX plug-in contains only Grains, Permute, Morph, Spectral and Pitch Grid. Capture, keyboard, additive synthesis, and the Synth controls appear only in the separate MIDI instrument. The instrument editor opens at 1120 × 760, with Wave and Harmonics oscillator views, a four-shape source menu, editable harmonic bars, envelope nodes, an always-visible LFO section, live output waveform and FFT, and keyboard. The Wave surface maps horizontal movement to Tone, vertical movement to FM and the wheel to Spread. The instrument LFO graph remains interactive while Rate, Depth, Target and Sync stay visible; Synth FM is its exclusive target. Other module surfaces map their first two parameters to XY and their third parameter to the wheel.

The FX effect starts with its five modules off. The MIDI instrument starts with Synth on and feeds its generated audio through those same five effects. The separate signal flows are `FX input → Grains → Permute → Morph / Spectral / Pitch Grid` and `MIDI → Synth → Grains → Permute → Morph / Spectral / Pitch Grid`. Capture and Synth mutation are instrument-only and use the same Undo mechanism as parameter edits, so experimenting can be reversed.

Audio algorithms are implemented independently. The interface borrows design principles, not graphical assets or EQ-01 source code.
