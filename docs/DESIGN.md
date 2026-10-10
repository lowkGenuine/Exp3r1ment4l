# Action hierarchy

The design follows lowkGenuine's EQ-01 direction: pale surface, dark type, restrained lines and values rather than decorative knobs. Hierarchy is assigned to what the user does.

| Priority | Actions | Placement |
| --- | --- | --- |
| First | Enable a process, change its main character, compare wet/dry, set output level | Six persistent checkboxes; one 3–4-field panel; persistent Mix, Output, Bypass |
| Second | Choose an operator/scale/donor, freeze, capture, audition, mutate, undo | Contextual module actions and a single Undo button |
| Third | Modulate, tune synth attack/FM, sync tempo | More opens one compact continuation of the same panel |

Two independent agents reviewed the hierarchy and EQ-01 interaction choices. Their shared recommendation: separate activation from selection, avoid giant permanent visualisers, keep only the current module's controls visible, retain settings when disabling, and let one action immediately produce a useful result.

The effect editor opens at 960 × 700 and expands to 960 × 840 with More. The dedicated Synth editor opens at 1120 × 760, with Wave and Harmonics oscillator views, a four-shape source menu, editable harmonic bars, envelope nodes, an always-visible LFO section, live output waveform and FFT, and keyboard. The Wave surface maps horizontal movement to Tone, vertical movement to FM and the wheel to Spread. The LFO graph remains interactive while Rate, Depth, Target and Sync stay visible. Other module surfaces map their first two parameters to XY and their third parameter to the wheel.

The effect starts with all modules off. The instrument starts with Synth on. Capture/mutation use the same Undo mechanism as parameter edits, so experimenting can be reversed.

Audio algorithms are implemented independently. The interface borrows design principles, not graphical assets or EQ-01 source code.
