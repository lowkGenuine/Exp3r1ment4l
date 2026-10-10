// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace exp3
{
struct ParamSpec
{
    const char* id;
    const char* name;
    float min, max, value, step;
    const char* unit;
};
inline constexpr ParamSpec continuous[]{{"mix", "Mix", 0, 1, 1, .001f, ""},
                                        {"outputDb", "Output", -36, 12, 0, .1f, " dB"},
                                        {"morphBlend", "Blend", 0, 1, .5f, .001f, ""},
                                        {"morphFocus", "Focus", 0, 1, .5f, .001f, ""},
                                        {"morphMotion", "Motion", 0, 1, 0, .001f, ""},
                                        {"spectralWarp", "Warp", -1, 1, 0, .001f, ""},
                                        {"spectralSmear", "Smear", 0, 1, .35f, .001f, ""},
                                        {"spectralDynamics", "Dynamics", -1, 1, 0, .001f, ""},
                                        {"pitchAmount", "Amount", 0, 1, 1, .001f, ""},
                                        {"transpose", "Transpose", -24, 24, 0, 1, " st"},
                                        {"grainSize", "Size", 10, 400, 80, 1, " ms"},
                                        {"grainDensity", "Density", 1, 40, 8, .1f, " /s"},
                                        {"grainPitch", "Pitch", -24, 24, 0, .1f, " st"},
                                        {"grainPosition", "Position", 0, 1, .25f, .001f, ""},
                                        {"permuteClock", "Clock", 200, 48000, 12000, 1, " Hz"},
                                        {"permuteDepth", "Disorder", 0, 1, .5f, .001f, ""},
                                        {"permuteFeedback", "Feedback", 0, .85f, .2f, .001f, ""},
                                        {"synthTone", "Tone", 0, 1, .5f, .001f, ""},
                                        {"synthSpread", "Spread", 0, 1, .2f, .001f, ""},
                                        {"synthRelease", "Release", 20, 4000, 600, 1, " ms"},
                                        {"synthLevel", "Level", -60, 0, -12, .1f, " dB"},
                                        {"synthAttack", "Attack", 1, 2000, 10, 1, " ms"},
                                        {"synthFM", "FM", 0, 1, .25f, .001f, ""},
                                        {"motion", "Depth", 0, 1, 0, .001f, ""},
                                        {"rate", "Rate", .05f, 20, 1, .01f, " Hz"}};
inline constexpr const char* enableIds[]{"synthOn", "grainsOn",   "permuteOn",
                                         "morphOn", "spectralOn", "pitchOn"};
inline constexpr const char* moduleNames[]{"Synth", "Grains", "Permute", "Morph", "Spectral", "Pitch Grid"};
inline constexpr const char* enableNames[]{"Synth On", "Grains On",   "Permute On",
                                           "Morph On", "Spectral On", "Pitch Grid On"};
inline juce::AudioProcessorValueTreeState::ParameterLayout layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    for (const auto& s : continuous)
    {
        if (!JucePlugin_IsSynth && juce::String(s.id).startsWith("synth"))
            continue;
        juce::NormalisableRange<float> range(s.min, s.max, s.step);
        if (juce::String(s.id) == "permuteClock")
            range.setSkewForCentre(8000);
        if (juce::String(s.id) == "grainSize")
            range.setSkewForCentre(100);
        if (juce::String(s.id) == "synthRelease")
            range.setSkewForCentre(600);
        p.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{s.id, 1}, s.name, range, s.value,
            juce::AudioParameterFloatAttributes().withLabel(s.unit)));
    }
    for (int i = 0; i < 6; ++i)
    {
        if (!JucePlugin_IsSynth && i == 0)
            continue;
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{enableIds[i], 1}, enableNames[i],
                                                         JucePlugin_IsSynth && i == 0));
    }
    for (const auto& s : std::array<std::pair<const char*, const char*>, 4>{{{"bypass", "Bypass"},
                                                                             {"freeze", "Freeze"},
                                                                             {"grainFreeze", "Grain Freeze"},
                                                                             {"sync", "Tempo Sync"}}})
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{s.first, 1}, s.second, false));
    p.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"permuteOperator", 1}, "Operator",
        juce::StringArray{"XOR", "Rotate", "Reverse", "Stutter"}, 0));
    p.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"root", 1}, "Root",
        juce::StringArray{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 0));
    p.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"scale", 1}, "Scale",
        juce::StringArray{"Chromatic", "Major", "Minor", "Pentatonic", "Whole tone", "Diminished",
                          "Harmonic minor", "Free"},
        1));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"donorSource", 1}, "Donor",
                                                       juce::StringArray{"Sidechain", "Audio file"}, 0));
    juce::StringArray targets{"None", "Morph Blend", "Spectral Warp", "Grain Position", "Permute Clock"};
    if (JucePlugin_IsSynth) targets.add("Synth FM");
    targets.add("Pitch Amount");
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"modTarget", 1}, "Modulation Target", targets, 0));
    return p;
}
} // namespace exp3
