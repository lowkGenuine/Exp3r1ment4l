Exp3r1ment4l 0.1.4 — build-only alpha

Windows x64 and universal macOS packages include the effect and the separate MIDI instrument. The effect contains five modules: Grains, Permute, Morph, Spectral, and Pitch Grid. Synth is available only in the instrument; incoming MIDI triggers the instrument's audio synthesis, with no internal Synth module in the effect.

The source archive includes project source, JUCE 7.0.12 at the pinned commit, license notices, and the macOS SDK compatibility patch. The plug-ins were not run, per the author's request, and DAW compatibility has not been verified. Reported processing latency is 0 samples in direct mode and 1024 samples in spectral mode. The macOS bundles use ad-hoc signing and are not notarized.
