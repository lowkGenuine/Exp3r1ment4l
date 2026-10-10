// SPDX-License-Identifier: AGPL-3.0-or-later
// Native VST3 integration checks. This executable deliberately uses only host APIs.
// Usage: Exp3r1ment4lHostCheck /path/to/Exp3r1ment4l.vst3 [--preview /path/to/editor.png]
// On headless Linux run under Xvfb; editor hosting still requires a display.
#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int maxBlock = 512;
constexpr int latency = 2048;
constexpr double pi = 3.14159265358979323846;
constexpr std::array<int, 8> blockSizes{1, 7, 64, 127, 256, 511, 512, 31};

void require(bool condition, const juce::String& explanation)
{
    if (!condition)
        throw std::runtime_error(explanation.toStdString());
}

juce::String normalise(const juce::String& text)
{
    return text.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
}

juce::AudioProcessorParameter* findParameter(juce::AudioPluginInstance& plugin,
                                             std::initializer_list<const char*> identifiers)
{
    for (int i = 0; i < plugin.getParameters().size(); ++i)
    {
        auto* parameter = plugin.getParameters()[i];
        const auto* hosted = plugin.getHostedParameter(i);
        const auto id = hosted != nullptr ? hosted->getParameterID() : juce::String();
        for (const auto* candidate : identifiers)
        {
            const juce::String requested(candidate);
            // JUCE's VST3 wrapper exports hashed IDs, not the original APVTS strings.
            const auto compatible = juce::VST3ClientExtensions::convertJuceParameterId(requested, true);
            const auto unrestricted = juce::VST3ClientExtensions::convertJuceParameterId(requested, false);
            if (id == requested || id == juce::String(static_cast<juce::int64>(compatible)) ||
                id == juce::String(static_cast<juce::int64>(unrestricted)) ||
                normalise(parameter->getName(128)) == normalise(requested))
                return parameter;
        }
    }
    return nullptr;
}

void automate(juce::AudioProcessorParameter& parameter, float normalisedValue)
{
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost(std::clamp(normalisedValue, 0.0f, 1.0f));
    parameter.endChangeGesture();
}

void set(juce::AudioPluginInstance& plugin, std::initializer_list<const char*> identifiers, float value)
{
    auto* parameter = findParameter(plugin, identifiers);
    require(parameter != nullptr, "Missing host parameter: " + juce::String(*identifiers.begin()));
    automate(*parameter, value);
}

void setOptional(juce::AudioPluginInstance& plugin, std::initializer_list<const char*> identifiers,
                 float value)
{
    if (auto* parameter = findParameter(plugin, identifiers))
        automate(*parameter, value);
}

void modules(juce::AudioPluginInstance& plugin, bool effects, bool synth)
{
    const float enabled = effects ? 1.0f : 0.0f;
    set(plugin, {"morphOn", "Morph enabled", "Morph on"}, enabled);
    set(plugin, {"spectralOn", "Spectral enabled", "Spectral on"}, enabled);
    set(plugin, {"pitchOn", "Pitch Grid enabled", "Pitch Grid On", "Pitch on"}, enabled);
    set(plugin, {"grainOn", "grainsOn", "Grains enabled", "Grains on"}, enabled);
    set(plugin, {"permuteOn", "Permute enabled", "Permute on"}, enabled);
    set(plugin, {"synthOn", "Synth enabled", "Synth on"}, synth ? 1.0f : 0.0f);
}

void restore(juce::AudioPluginInstance& plugin, const juce::MemoryBlock& state)
{
    plugin.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
}

void configure(juce::AudioPluginInstance& plugin, int mainChannels, int sidechainChannels)
{
    plugin.releaseResources();
    auto layout = plugin.getBusesLayout();
    require(layout.inputBuses.size() >= 2 && layout.outputBuses.size() == 1,
            "Expected main audio input, optional sidechain input and one output bus");
    const auto main = mainChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.getReference(0) = mainChannels == 0 ? juce::AudioChannelSet::disabled() : main;
    layout.outputBuses.getReference(0) = main;
    layout.inputBuses.getReference(1) = sidechainChannels == 0   ? juce::AudioChannelSet::disabled()
                                        : sidechainChannels == 1 ? juce::AudioChannelSet::mono()
                                                                 : juce::AudioChannelSet::stereo();
    require(plugin.checkBusesLayoutSupported(layout),
            "Unsupported bus layout: main=" + juce::String(mainChannels) +
                ", sidechain=" + juce::String(sidechainChannels));
    require(plugin.setBusesLayout(layout), "Host could not apply supported bus layout");
    plugin.setRateAndBufferSizeDetails(sampleRate, maxBlock);
    plugin.prepareToPlay(sampleRate, maxBlock);
    require(plugin.getLatencySamples() == latency, "Reported VST3 latency must remain 2048 samples");
}

void flushAutomation(juce::AudioPluginInstance& plugin)
{
    juce::AudioBuffer<float> audio(
        std::max(plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels()), maxBlock);
    juce::MidiBuffer midi;
    for (int i = 0; i < 6; ++i)
    {
        audio.clear();
        midi.clear();
        plugin.processBlock(audio, midi);
    }
}

void checkAutomationAndState(juce::AudioPluginInstance& plugin, const juce::MemoryBlock& original)
{
    configure(plugin, 2, 0);
    std::vector<juce::AudioProcessorParameter*> parameters;
    for (auto* parameter : plugin.getParameters())
        if (parameter->isAutomatable() && !parameter->getName(128).startsWith("MIDI CC "))
            parameters.push_back(parameter);
    require(parameters.size() >= 12, "Too few automatable plug-in parameters");

    for (size_t i = 0; i < parameters.size(); ++i)
    {
        auto& parameter = *parameters[i];
        float value = static_cast<float>((i * 13 + 7) % 37) / 36.0f;
        const int steps = parameter.getNumSteps();
        if (steps > 1 && steps < 65536)
            value = std::round(value * static_cast<float>(steps - 1)) / static_cast<float>(steps - 1);
        automate(parameter, value);
    }
    flushAutomation(plugin); // VST3 changes reach the processor at processBlock, not at setValue.
    juce::MemoryBlock saved;
    plugin.getStateInformation(saved);
    require(saved.getSize() > 64, "Empty VST3 state");
    // Canonicalise quantised float parameters before comparing controller values.
    // The VST3 controller may temporarily retain the unsnapped host request.
    restore(plugin, saved);
    flushAutomation(plugin);
    std::vector<float> savedValues;
    for (auto* parameter : parameters)
        savedValues.push_back(parameter->getValue());
    for (auto* parameter : parameters)
        automate(*parameter, parameter->getDefaultValue());
    flushAutomation(plugin);
    restore(plugin, saved);
    flushAutomation(plugin);
    for (size_t i = 0; i < parameters.size(); ++i)
        require(std::abs(parameters[i]->getValue() - savedValues[i]) < 2.0e-4f,
                "State restore changed parameter: " + parameters[i]->getName(128));
    restore(plugin, original);
    std::cout << "PASS: host automation and state round trip for " << parameters.size() << " parameters\n";
}

float mainSignal(int channel, int frame)
{
    const double t = static_cast<double>(frame) / sampleRate;
    return static_cast<float>(0.12 * std::sin(2.0 * pi * (channel == 0 ? 233.0 : 347.0) * t) +
                              0.035 * std::sin(2.0 * pi * (711.0 + channel * 193.0) * t) +
                              (frame % 4093 == 0 ? 0.08 : 0.0));
}

void checkAllModules(juce::AudioPluginInstance& plugin, const juce::MemoryBlock& original, int mainChannels,
                     int sidechainChannels)
{
    restore(plugin, original);
    modules(plugin, true, true);
    set(plugin, {"mix", "Master mix", "Dry Wet"}, 1.0f);
    set(plugin, {"bypass", "Global bypass"}, 0.0f);
    configure(plugin, mainChannels, sidechainChannels);
    juce::AudioBuffer<float> audio(
        std::max(plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels()), maxBlock);
    juce::MidiBuffer midi;
    double energy = 0.0;
    int frame = 0, block = 0;
    constexpr int frames = 48000;
    while (frame < frames)
    {
        const int count =
            std::min(blockSizes[static_cast<size_t>(block++) % blockSizes.size()], frames - frame);
        audio.setSize(audio.getNumChannels(), count, false, false, true);
        audio.clear();
        midi.clear();
        for (int c = 0; c < mainChannels; ++c)
            for (int i = 0; i < count; ++i)
                audio.setSample(c, i, mainSignal(c, frame + i));
        if (sidechainChannels > 0)
        {
            auto sidechain = plugin.getBusBuffer(audio, true, 1);
            for (int c = 0; c < sidechain.getNumChannels(); ++c)
                for (int i = 0; i < count; ++i)
                    sidechain.setSample(c, i,
                                        static_cast<float>(0.2 * std::sin(2.0 * pi * (173.0 + 127.0 * c) *
                                                                          (frame + i) / sampleRate)));
        }
        if (frame == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(95)), 0);
        if (frame <= frames / 2 && frame + count > frames / 2)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), frames / 2 - frame);
        plugin.processBlock(audio, midi);
        for (int c = 0; c < mainChannels; ++c)
            for (int i = 0; i < count; ++i)
            {
                const float value = audio.getSample(c, i);
                require(std::isfinite(value), "Non-finite output with all modules enabled");
                require(std::abs(value) <= 8.0f, "Unbounded output with all modules enabled");
                if (frame + i > latency * 2)
                    energy += static_cast<double>(value) * value;
            }
        require(plugin.getLatencySamples() == latency, "A module switch changed fixed latency");
        frame += count;
    }
    require(energy > 1.0e-6, "All enabled modules produced silence for an audible input");
    std::cout << "PASS: finite bounded output, variable blocks, MIDI; main=" << mainChannels
              << ", sidechain=" << sidechainChannels << '\n';
}

void checkMidiSynth(juce::AudioPluginInstance& plugin, const juce::MemoryBlock& original)
{
    restore(plugin, original);
    modules(plugin, false, true);
    set(plugin, {"mix", "Master mix", "Dry Wet"}, 1.0f);
    set(plugin, {"bypass", "Global bypass"}, 0.0f);
    setOptional(plugin, {"synthRelease", "Synth release"}, 0.0f);
    configure(plugin, 0, 0);
    juce::AudioBuffer<float> audio(2, maxBlock);
    juce::MidiBuffer midi;
    constexpr int noteOnFrame = 37, noteOffFrame = 24000, totalFrames = 144000;
    double heldEnergy = 0.0, releasedEnergy = 0.0;
    int heldCount = 0, releasedCount = 0;
    for (int frame = 0; frame < totalFrames; frame += maxBlock)
    {
        const int count = std::min(maxBlock, totalFrames - frame);
        audio.setSize(2, count, false, false, true);
        audio.clear();
        midi.clear();
        if (frame <= noteOnFrame && frame + count > noteOnFrame)
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(110)),
                          noteOnFrame - frame);
        if (frame <= noteOffFrame && frame + count > noteOffFrame)
            midi.addEvent(juce::MidiMessage::noteOff(1, 69), noteOffFrame - frame);
        plugin.processBlock(audio, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < count; ++i)
            {
                const auto value = audio.getSample(c, i);
                require(std::isfinite(value) && std::abs(value) <= 8.0f, "Invalid MIDI synth output");
                const int absolute = frame + i;
                if (absolute >= latency + 1024 && absolute < noteOffFrame)
                {
                    heldEnergy += static_cast<double>(value) * value;
                    ++heldCount;
                }
                if (absolute > totalFrames - 12000)
                {
                    releasedEnergy += static_cast<double>(value) * value;
                    ++releasedCount;
                }
            }
    }
    const double heldMean = heldEnergy / std::max(1, heldCount);
    const double releasedMean = releasedEnergy / std::max(1, releasedCount);
    require(heldMean > 1.0e-8, "MIDI note-on did not generate audio from silence");
    require(releasedMean < heldMean * 0.02 + 1.0e-10, "MIDI note-off left a stuck synth voice");
    std::cout << "PASS: MIDI-only synth note-on and note-off, including sample offsets\n";
}

void checkBypass(juce::AudioPluginInstance& plugin, const juce::MemoryBlock& original, bool hostBypass)
{
    restore(plugin, original);
    modules(plugin, true, true);
    set(plugin, {"mix", "Master mix", "Dry Wet"}, 1.0f);
    set(plugin, {"bypass", "Global bypass"}, hostBypass ? 0.0f : 1.0f);
    configure(plugin, 2, 2);
    juce::AudioBuffer<float> audio(4, maxBlock);
    juce::MidiBuffer midi;
    double maximumError = 0.0;
    constexpr int totalFrames = 24000;
    int frame = 0, block = 0;
    while (frame < totalFrames)
    {
        const int count =
            std::min(blockSizes[static_cast<size_t>(block++) % blockSizes.size()], totalFrames - frame);
        audio.setSize(4, count, false, false, true);
        audio.clear();
        midi.clear();
        if (frame == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(100)), 0);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < count; ++i)
            {
                audio.setSample(c, i, mainSignal(c, frame + i));
                audio.setSample(c + 2, i, 0.13f); // Sidechain must never leak to bypass output.
            }
        if (hostBypass)
            plugin.processBlockBypassed(audio, midi);
        else
            plugin.processBlock(audio, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < count; ++i)
            {
                const float actual = audio.getSample(c, i);
                require(std::isfinite(actual), "Non-finite bypass output");
                // Leave settling time for host automation and a click-free bypass ramp.
                if (frame + i >= 16384)
                    maximumError =
                        std::max(maximumError,
                                 static_cast<double>(std::abs(actual - mainSignal(c, frame + i - latency))));
            }
        frame += count;
    }
    require(maximumError < 1.0e-5, "Bypass failed to reproduce input delayed by 2048 samples; error=" +
                                       juce::String(maximumError, 9));
    std::cout << "PASS: " << (hostBypass ? "host" : "parameter")
              << " bypass preserves delayed input with every effect and synth enabled\n";
}

bool hasImageContent(const juce::Image& image)
{
    if (!image.isValid())
        return false;
    const auto reference = image.getPixelAt(0, 0);
    int different = 0, checked = 0;
    for (int y = 0; y < image.getHeight(); y += 3)
        for (int x = 0; x < image.getWidth(); x += 3)
        {
            ++checked;
            if (image.getPixelAt(x, y) != reference)
                ++different;
        }
    return different > checked / 100;
}

void checkEditor(juce::AudioPluginInstance& plugin, const juce::MemoryBlock& original,
                 const juce::File& preview)
{
    restore(plugin, original);
    configure(plugin, 2, 0);
    require(plugin.hasEditor(), "VST3 does not expose an editor");
    std::unique_ptr<juce::AudioProcessorEditor> editor(plugin.createEditorIfNeeded());
    require(editor != nullptr && editor->getWidth() > 0 && editor->getHeight() > 0,
            "Native editor creation failed");
    editor->addToDesktop(0);
    editor->setVisible(true);
#if JUCE_MODAL_LOOPS_PERMITTED
    juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
#endif
    juce::Timer::callPendingTimersSynchronously();
    std::cout << "PASS: native editor creation (" << editor->getWidth() << 'x' << editor->getHeight()
              << ")\n";
    if (preview != juce::File())
    {
        const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        // Native VST3 child windows are not necessarily included in JUCE snapshots.
        // Fail explicitly instead of writing a blank image that looks like a valid preview.
        require(hasImageContent(snapshot),
                "Preview unavailable: createComponentSnapshot did not capture the native VST3 child window. "
                "Use a platform screenshot or source-linked editor preview.");
        require(preview.getParentDirectory().createDirectory().wasOk(),
                "Could not create preview output directory");
        auto stream = preview.createOutputStream();
        require(stream != nullptr, "Could not open preview PNG");
        require(juce::PNGImageFormat().writeImageToStream(snapshot, *stream), "Could not encode preview PNG");
        stream->flush();
        require(stream->getStatus().wasOk(), "Could not finish writing preview PNG");
        std::cout << "Preview: " << preview.getFullPathName() << '\n';
    }
    editor.reset();
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2 || (argc == 4 && juce::String(argv[2]) == "--preview"),
                "Usage: Exp3r1ment4lHostCheck plugin.vst3 [--preview editor.png]");
        juce::ScopedJuceInitialiser_GUI gui;
        juce::ScopedNoDenormals noDenormals;
        juce::AudioPluginFormatManager formats;
        formats.addDefaultFormats();
        juce::OwnedArray<juce::PluginDescription> descriptions;
        const juce::File bundle(juce::String::fromUTF8(argv[1]));
        require(bundle.exists(), "VST3 bundle does not exist: " + bundle.getFullPathName());
        for (auto* format : formats.getFormats())
            if (format->getName() == "VST3")
                format->findAllTypesForFile(descriptions, bundle.getFullPathName());
        require(descriptions.size() == 1, "Expected exactly one audio processor in the VST3 bundle");
        require(descriptions[0]->name == "Exp3r1ment4l" || descriptions[0]->name == "Exp3r1ment4l Synth",
                "Incorrect VST3 product name: " + descriptions[0]->name);
        juce::String error;
        auto plugin = formats.createPluginInstance(*descriptions[0], sampleRate, maxBlock, error);
        require(plugin != nullptr, "VST3 instantiation failed: " + error);
        require(plugin->acceptsMidi(), "VST3 does not advertise MIDI input");
        std::cout << "PASS: VST3 scan/load, product name and MIDI input\n";
        juce::MemoryBlock original;
        plugin->getStateInformation(original);
        require(original.getSize() > 64, "No default plug-in state");

        checkAutomationAndState(*plugin, original);
        for (const auto layout : {std::array<int, 2>{1, 0}, {2, 0}, {1, 1}, {2, 1}, {2, 2}})
            checkAllModules(*plugin, original, layout[0], layout[1]);
        checkMidiSynth(*plugin, original);
        checkBypass(*plugin, original, false);
        checkBypass(*plugin, original, true);
        checkEditor(*plugin, original,
                    argc == 4 ? juce::File(juce::String::fromUTF8(argv[3])) : juce::File());
        plugin->releaseResources();
        std::cout << "PASS: all native VST3 integration checks\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}
