// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>
#include <string_view>

namespace
{
constexpr const char* flagIds[]{"bypass", "freeze", "grainFreeze", "sync"};
constexpr const char* choiceIds[]{"permuteOperator", "root", "scale", "donorSource", "modTarget"};
int latencyFor(const experimental::Settings& settings) noexcept
{
    return settings.morphOn || settings.spectralOn || settings.pitchOn ? experimental::Engine::latency : 0;
}
constexpr size_t continuousIndex(std::string_view id)
{
    for (size_t i = 0; i < std::size(exp3::continuous); ++i)
        if (id == exp3::continuous[i].id)
            return i;
    return std::size(exp3::continuous);
}
} // namespace

ExperimentalProcessor::ExperimentalProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), !JucePlugin_IsSynth)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, &undo, "Exp3r1ment4l", exp3::layout()),
      engine(std::make_unique<experimental::Engine>())
{
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = parameters.getRawParameterValue(exp3::continuous[i].id);
    for (size_t i = 0; i < on.size(); ++i)
        on[i] = parameters.getRawParameterValue(exp3::enableIds[i]);
#if JucePlugin_IsSynth
    parameters.getParameter("synthOn")->setValue(1.0f);
    editorModule.store(0);
#endif
    for (size_t i = 0; i < flags.size(); ++i)
        flags[i] = parameters.getRawParameterValue(flagIds[i]);
    for (size_t i = 0; i < choices.size(); ++i)
        choices[i] = parameters.getRawParameterValue(choiceIds[i]);
    bypassParameter = parameters.getParameter("bypass");
    donorVersions.reserve(8);
    publishSeed();
    setLatencySamples(latencyFor(settings()));
}
ExperimentalProcessor::~ExperimentalProcessor()
{
    donorGeneration.fetch_add(1);
    donorPool.removeAllJobs(true, -1);
    donor.store(nullptr, std::memory_order_release);
}
void ExperimentalProcessor::prepareToPlay(double sr, int)
{
    sampleRate = std::isfinite(sr) ? std::clamp(sr, 8000., 384000.) : 48000.;
    engine->setSettings(settings());
    engine->prepare(sampleRate);
    playingDonor = nullptr;
    donorPosition = 0;
    heldNotesLo = heldNotesHi = 0;
    keyboard.reset();
    voices.store(0);
    setLatencySamples(latencyFor(settings()));
}
void ExperimentalProcessor::releaseResources()
{
    engine->reset();
    keyboard.reset();
    voices.store(0);
}
bool ExperimentalProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet(), in = l.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (!in.isDisabled() && in != out)
        return false;
    if (l.inputBuses.size() > 1)
    {
        const auto sc = l.inputBuses[1];
        if (!sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}
float ExperimentalProcessor::value(const char* id) const
{
    if (id == nullptr)
        return 0;
    for (size_t i = 0; i < values.size(); ++i)
        if (std::strcmp(id, exp3::continuous[i].id) == 0)
            return values[i] != nullptr ? values[i]->load(std::memory_order_relaxed) : exp3::continuous[i].value;
    for (size_t i = 0; i < on.size(); ++i)
        if (std::strcmp(id, exp3::enableIds[i]) == 0)
            return on[i] != nullptr ? on[i]->load(std::memory_order_relaxed) : 0.f;
    for (size_t i = 0; i < flags.size(); ++i)
        if (std::strcmp(id, flagIds[i]) == 0)
            return flags[i]->load(std::memory_order_relaxed);
    for (size_t i = 0; i < choices.size(); ++i)
        if (std::strcmp(id, choiceIds[i]) == 0)
            return choices[i]->load(std::memory_order_relaxed);
    return 0;
}
experimental::Settings ExperimentalProcessor::settings() const
{
    experimental::Settings s;
    s.synthOn = JucePlugin_IsSynth && on[0] != nullptr && on[0]->load() > .5f;
    s.grainsOn = on[1]->load() > .5f;
    s.permuteOn = on[2]->load() > .5f;
    s.morphOn = on[3]->load() > .5f;
    s.spectralOn = on[4]->load() > .5f;
    s.pitchOn = on[5]->load() > .5f;
#define READ(name)                                                                                           \
    {                                                                                                        \
        constexpr auto i = continuousIndex(#name);                                                           \
        static_assert(i < std::size(exp3::continuous));                                                      \
        s.name = values[i] != nullptr ? values[i]->load(std::memory_order_relaxed) : exp3::continuous[i].value; \
    }
    READ(mix);
    READ(outputDb);
    READ(morphBlend);
    READ(morphFocus);
    READ(morphMotion);
    READ(spectralWarp);
    READ(spectralSmear);
    READ(spectralDynamics);
    READ(pitchAmount);
    READ(transpose);
    READ(grainSize);
    READ(grainDensity);
    READ(grainPitch);
    READ(grainPosition);
    READ(permuteClock);
    READ(permuteDepth);
    READ(permuteFeedback);
    READ(synthTone);
    READ(synthSpread);
    READ(synthRelease);
    READ(synthLevel);
    READ(synthAttack);
    READ(synthFM);
    READ(motion);
    READ(rate);
#undef READ
    s.bypass = flags[0]->load(std::memory_order_relaxed) > .5f;
    s.freeze = flags[1]->load(std::memory_order_relaxed) > .5f;
    s.grainFreeze = flags[2]->load(std::memory_order_relaxed) > .5f;
    s.sync = flags[3]->load(std::memory_order_relaxed) > .5f;
    s.permuteOperator = (int)choices[0]->load(std::memory_order_relaxed);
    s.root = (int)choices[1]->load(std::memory_order_relaxed);
    s.scale = (int)choices[2]->load(std::memory_order_relaxed);
    s.donorSource = (int)choices[3]->load(std::memory_order_relaxed);
    const int selectedModTarget = choices[4] != nullptr ? (int)choices[4]->load(std::memory_order_relaxed) : 0;
#if JucePlugin_IsSynth
    s.modTarget = selectedModTarget;
#else
    s.modTarget = selectedModTarget == 5 ? 6 : selectedModTarget;
#endif
    if (s.sync)
        if (auto* p = getPlayHead())
            if (auto pos = p->getPosition())
                if (auto bpm = pos->getBpm())
                    s.rate = static_cast<float>(std::clamp(*bpm / 60., .05, 20.));
    return s;
}
void ExperimentalProcessor::midi(const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        engine->noteOn(m.getNoteNumber(), m.getFloatVelocity());
        const int n = m.getNoteNumber();
        if (n < 64)
            heldNotesLo |= uint64_t(1) << n;
        else
            heldNotesHi |= uint64_t(1) << (n - 64);
    }
    else if (m.isNoteOff())
    {
        engine->noteOff(m.getNoteNumber());
        const int n = m.getNoteNumber();
        if (n < 64)
            heldNotesLo &= ~(uint64_t(1) << n);
        else
            heldNotesHi &= ~(uint64_t(1) << (n - 64));
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        engine->allNotesOff();
        heldNotesLo = heldNotesHi = 0;
    }
    else if (m.isController() && m.getControllerNumber() == 64)
        engine->setSustain(m.getControllerValue() >= 64);
    else if (m.isPitchWheel())
        engine->setPitchBend((m.getPitchWheelValue() - 8192) / 8192.f * 2.f);
}
bool ExperimentalProcessor::publishSeed()
{
    const juce::SpinLock::ScopedTryLockType lock(seedLock);
    if (!lock.isLocked() || seedRequested.load(std::memory_order_acquire))
        return false;
    const auto t = engine->timbre();
    for (size_t i = 0; i < t.size(); ++i)
        seed[i].store(t[i], std::memory_order_relaxed);
    return true;
}
void ExperimentalProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
{
    process(b, m, false);
}
void ExperimentalProcessor::processBlockBypassed(juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
{
    process(b, m, true);
}
int ExperimentalProcessor::readVisualSamples(float* destination, int capacity)
{
    if (destination == nullptr || capacity <= 0)
        return 0;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    visualFifo.prepareToRead(capacity, start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i)
        destination[i] = visualSamples[(size_t)(start1 + i)];
    for (int i = 0; i < size2; ++i)
        destination[size1 + i] = visualSamples[(size_t)(start2 + i)];
    const int read = size1 + size2;
    visualFifo.finishedRead(read);
    return read;
}
void ExperimentalProcessor::setVisualTimbre(const std::array<float, 32>& values)
{
#if !JucePlugin_IsSynth
    (void)values;
    return;
#else
    std::array<float, 32> normalized{};
    float total = 0.f;
    for (size_t i = 0; i < values.size(); ++i)
    {
        normalized[i] = std::isfinite(values[i]) ? std::clamp(values[i], 0.f, 1.f) : 0.f;
        total += normalized[i];
    }
    if (total < 1.e-9f)
    {
        normalized[0] = 1.f;
    }
    else
    {
        const float inverseTotal = 1.f / total;
        for (auto& value : normalized)
            value *= inverseTotal;
    }

    const juce::SpinLock::ScopedLockType lock(seedLock);
    for (size_t i = 0; i < normalized.size(); ++i)
    {
        requestedSeed[i].store(normalized[i], std::memory_order_relaxed);
        seed[i].store(normalized[i], std::memory_order_relaxed);
    }
    captureRequested.store(false, std::memory_order_relaxed);
    mutationRequested.store(0, std::memory_order_relaxed);
    seedRequested.store(true, std::memory_order_release);
#endif
}
void ExperimentalProcessor::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& messages,
                                    bool hostBypass)
{
    juce::ScopedNoDenormals guard;
    auto s = settings();
    s.bypass = s.bypass || hostBypass;
    engine->setSettings(s);
    const int desiredLatency = latencyFor(s);
    if (getLatencySamples() != desiredLatency)
        setLatencySamples(desiredLatency);
    if (seedRequested.load(std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedTryLockType lock(seedLock);
        if (lock.isLocked() && seedRequested.exchange(false))
        {
            std::array<float, 32> t{};
            for (size_t i = 0; i < t.size(); ++i)
                t[i] = requestedSeed[i].load(std::memory_order_relaxed);
            engine->setTimbre(t);
        }
    }
    if (!seedRequested.load(std::memory_order_acquire))
    {
        if (auto n = mutationRequested.exchange(0))
        {
            engine->mutateTimbre(n);
            seedPublicationPending = true;
        }
        if (captureRequested.exchange(false))
        {
            engine->captureTimbre();
            seedPublicationPending = true;
        }
    }
    if (seedPublicationPending && publishSeed())
        seedPublicationPending = false;
    auto* newDonor = donor.load(std::memory_order_acquire);
    if (newDonor != playingDonor)
    {
        playingDonor = newDonor;
        donorPosition = 0;
    }
    const auto input = getBusBuffer(buffer, true, 0);
    auto output = getBusBuffer(buffer, false, 0);
    auto side = getBusBuffer(buffer, true, 1);
    const int count = buffer.getNumSamples(), ins = input.getNumChannels(), outs = output.getNumChannels(),
              sc = side.getNumChannels();
    const auto* inL = ins ? input.getReadPointer(0) : nullptr;
    const auto* inR = ins > 1 ? input.getReadPointer(1) : inL;
    const auto* scL = sc ? side.getReadPointer(0) : nullptr;
    const auto* scR = sc > 1 ? side.getReadPointer(1) : scL;
    auto* outL = output.getWritePointer(0);
    auto* outR = outs > 1 ? output.getWritePointer(1) : nullptr;
    hasSidechain.store(sc > 0);
    keyboard.processNextMidiBuffer(messages, 0, count, true);
    auto it = messages.begin();
    float inp = 0, outp = 0;
    for (int i = 0; i < count; ++i)
    {
        while (it != messages.end() && (*it).samplePosition <= i)
        {
            midi((*it).getMessage());
            ++it;
        }
        const float l = inL ? inL[i] : 0, r = inR ? inR[i] : l;
        float dl = 0, dr = 0;
        bool available = false;
        if (s.donorSource == 0 && scL)
        {
            dl = scL[i];
            dr = scR[i];
            available = true;
        }
        if (s.donorSource == 1 && playingDonor)
        {
            const auto& a = playingDonor->audio;
            const int len = a.getNumSamples();
            if (len > 1)
            {
                const int j = (int)donorPosition, k = (j + 1) % len;
                const float f = (float)(donorPosition - j);
                dl = a.getSample(0, j) * (1 - f) + a.getSample(0, k) * f;
                dr = a.getSample(1, j) * (1 - f) + a.getSample(1, k) * f;
                available = true;
                donorPosition += playingDonor->sampleRate / sampleRate;
                while (donorPosition >= len)
                    donorPosition -= len;
            }
        }
        const auto v = engine->process(l, r, dl, dr, available);
        outL[i] = outs == 1 ? (v[0] + v[1]) * .5f : v[0];
        if (outR)
            outR[i] = v[1];
        inp = std::max(inp, std::max(std::abs(l), std::abs(r)));
        outp = std::max(outp, std::max(std::abs(v[0]), std::abs(v[1])));
    }
    // Best-effort mono output feed for the GUI. Reserve once per block, then
    // write only the capacity available in the bounded FIFO.
    int visualStart1 = 0, visualSize1 = 0, visualStart2 = 0, visualSize2 = 0;
    visualFifo.prepareToWrite(count, visualStart1, visualSize1, visualStart2, visualSize2);
    for (int i = 0; i < visualSize1; ++i)
    {
        const int sample = visualStart1 + i;
        visualSamples[(size_t)sample] = outR ? (outL[i] + outR[i]) * .5f : outL[i];
    }
    for (int i = 0; i < visualSize2; ++i)
    {
        const int sample = visualStart2 + i;
        const int blockSample = visualSize1 + i;
        visualSamples[(size_t)sample] = outR ? (outL[blockSample] + outR[blockSample]) * .5f
                                             : outL[blockSample];
    }
    visualFifo.finishedWrite(visualSize1 + visualSize2);
    inputPeak.store(inp);
    outputPeak.store(outp);
    voices.store(engine->activeVoiceCount());
    messages.clear();
}
void ExperimentalProcessor::setParameter(const juce::String& id, float v)
{
    if (auto* p = parameters.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(v));
        p->endChangeGesture();
    }
}
juce::AudioProcessorParameter* ExperimentalProcessor::getBypassParameter() const
{
    return bypassParameter;
}
void ExperimentalProcessor::preset(int index)
{
    undo.beginNewTransaction("Preset");
    for (const auto& p : exp3::continuous)
        setParameter(p.id, p.value);
    for (const auto* id : exp3::enableIds)
        setParameter(id, 0);
#if JucePlugin_IsSynth
    // Instrument presets need a sound source, including Init and FX presets.
    setParameter("synthOn", 1);
#endif
    for (const auto* id : {"freeze", "grainFreeze", "sync", "bypass"})
        setParameter(id, 0);
    for (const auto* id : {"root", "scale", "permuteOperator", "donorSource", "modTarget"})
        setParameter(id, 0);
    setParameter("scale", 1);
    switch (index)
    {
    case 1:
        setParameter("grainsOn", 1);
        setParameter("grainSize", 160);
        setParameter("grainPitch", -12);
        setParameter("grainDensity", 18);
        setParameter("spectralOn", 1);
        setParameter("spectralSmear", .65f);
        setParameter("mix", .7f);
        break;
    case 2:
        setParameter("permuteOn", 1);
        setParameter("permuteClock", 4000);
        setParameter("permuteDepth", .7f);
        setParameter("permuteFeedback", .4f);
        setParameter("mix", .65f);
        break;
    case 3:
        setParameter("spectralOn", 1);
        setParameter("spectralWarp", .4f);
        setParameter("spectralSmear", .8f);
        setParameter("spectralDynamics", -.35f);
        setParameter("mix", .8f);
        break;
    case 4:
#if JucePlugin_IsSynth
        setParameter("synthOn", 1);
        setParameter("synthTone", .35f);
        setParameter("synthRelease", 1800);
        setParameter("synthAttack", 120);
        setParameter("synthFM", .18f);
#endif
        setParameter("grainsOn", 1);
        setParameter("grainDensity", 12);
        setParameter("mix", .8f);
        break;
    case 5:
        setParameter("pitchOn", 1);
        setParameter("scale", 3);
        setParameter("spectralOn", 1);
        setParameter("spectralSmear", .25f);
        setParameter("mix", .65f);
        break;
    default:
        break;
    }
}
void ExperimentalProcessor::mutateSelected(int module)
{
#if !JucePlugin_IsSynth
    if (module == 0)
        return;
#endif
    undo.beginNewTransaction("Mutate module");
    auto& rng = juce::Random::getSystemRandom();
    if (module == 0)
        mutationRequested.store(static_cast<uint32_t>(rng.nextInt()) | 1u);
    static const std::array<juce::StringArray, 6> ids{
        {{"synthTone", "synthSpread", "synthFM"},
         {"grainSize", "grainDensity", "grainPitch", "grainPosition"},
         {"permuteClock", "permuteDepth", "permuteFeedback"},
         {"morphBlend", "morphFocus", "morphMotion"},
         {"spectralWarp", "spectralSmear", "spectralDynamics"},
         {"pitchAmount", "transpose"}}};
    for (const auto& id : ids[(size_t)std::clamp(module, 0, 5)])
        if (auto* p = parameters.getParameter(id))
        {
            const float current = p->getValue(),
                        v = std::clamp(current + (rng.nextFloat() - .5f) * .45f, 0.f, 1.f);
            p->beginChangeGesture();
            p->setValueNotifyingHost(v);
            p->endChangeGesture();
        }
}
juce::Result ExperimentalProcessor::readDonor(const juce::File& file, std::unique_ptr<Donor>& result,
                                              uint64_t generation)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader)
        return juce::Result::fail("Choose a readable WAV, AIFF or FLAC audio file.");
    if (!std::isfinite(reader->sampleRate) || reader->sampleRate < 16 || reader->sampleRate > 384000 ||
        reader->lengthInSamples < 64)
        return juce::Result::fail("The audio file has an unsupported sample rate or is too short.");
    const int length = (int)std::min<int64_t>(reader->lengthInSamples, (int64_t)(reader->sampleRate * 10));
    if (length < 64)
        return juce::Result::fail("The audio file is too short.");
    auto next = std::make_unique<Donor>();
    next->audio.setSize(2, length);
    next->sampleRate = reader->sampleRate;
    next->name = file.getFileName();
    next->path = file.getFullPathName();
    for (int start = 0; start < length; start += 4096)
    {
        if (generation != 0 && donorGeneration.load(std::memory_order_acquire) != generation)
            return juce::Result::fail("The file load was superseded.");
        if (!reader->read(&next->audio, start, std::min(4096, length - start), start, true, true))
            return juce::Result::fail("Could not read this audio file.");
    }
    if (reader->numChannels == 1)
        next->audio.copyFrom(1, 0, next->audio, 0, 0, length);
    for (int channel = 0; channel < 2; ++channel)
    {
        auto* samples = next->audio.getWritePointer(channel);
        for (int i = 0; i < length; ++i)
            if (!std::isfinite(samples[i]))
                samples[i] = 0;
    }
    result = std::move(next);
    return juce::Result::ok();
}

juce::Result ExperimentalProcessor::loadDonor(const juce::File& file)
{
    {
        const juce::ScopedLock lock(donorLock);
        if (donorVersions.size() >= 8)
            return juce::Result::fail("Eight files per instance maximum. Reopen the instance to load more.");
    }
    std::unique_ptr<Donor> next;
    const auto result = readDonor(file, next);
    if (result.failed())
        return result;
    const juce::ScopedLock lock(donorLock);
    if (donorVersions.size() >= 8)
        return juce::Result::fail("Eight files per instance maximum. Reopen the instance to load more.");
    donorGeneration.fetch_add(1, std::memory_order_acq_rel);
    donorPath = next->path;
    auto* pointer = next.get();
    donorVersions.push_back(std::move(next));
    // Buffer contents and metadata never change or move after atomic publication.
    // Retaining previous buffers keeps destruction entirely off the audio thread.
    donor.store(pointer, std::memory_order_release);
    donorStatus.store(DonorStatus::ready);
    setParameter("donorSource", 1);
    parameters.state.setProperty("donorPath", donorPath, nullptr);
    return juce::Result::ok();
}

void ExperimentalProcessor::reloadDonor(const juce::String& path, uint64_t generation)
{
    if (donorGeneration.load(std::memory_order_acquire) != generation)
        return;
    const juce::File file(path);
    if (file.existsAsFile())
    {
        const juce::ScopedLock lock(donorLock);
        if (donorGeneration.load(std::memory_order_relaxed) != generation)
            return;
        for (const auto& retained : donorVersions)
            if (retained->path == path)
            {
                donor.store(retained.get(), std::memory_order_release);
                donorStatus.store(DonorStatus::ready);
                return;
            }
    }
    std::unique_ptr<Donor> next;
    const auto result = readDonor(file, next, generation);
    const juce::ScopedLock lock(donorLock);
    if (donorGeneration.load(std::memory_order_relaxed) != generation)
        return;
    if (result.failed() || donorVersions.size() >= 8)
    {
        donorStatus.store(DonorStatus::unavailable);
        return;
    }
    auto* pointer = next.get();
    donorVersions.push_back(std::move(next));
    donor.store(pointer, std::memory_order_release);
    donorStatus.store(DonorStatus::ready);
    // State already contains the source selection and path. Workers only publish audio.
}

void ExperimentalProcessor::resetDonor()
{
    const juce::ScopedLock lock(donorLock);
    donorGeneration.fetch_add(1, std::memory_order_acq_rel);
    donor.store(nullptr, std::memory_order_release);
    donorStatus.store(DonorStatus::empty);
    donorPath.clear();
    parameters.state.removeProperty("donorPath", nullptr);
}

juce::String ExperimentalProcessor::donorName() const
{
    if (auto* current = donor.load(std::memory_order_acquire))
        return current->name;
    const juce::ScopedLock lock(donorLock);
    if (auto* current = donor.load(std::memory_order_acquire))
        return current->name;
    if (donorStatus.load() == DonorStatus::loading)
        return "Loading audio file...";
    if (donorStatus.load() == DonorStatus::unavailable)
        return juce::File(donorPath).getFileName() + " (unavailable)";
    return "No audio file";
}

void ExperimentalProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::ValueTree state;
    std::array<float, 32> timbre{};
    {
        const juce::ScopedLock donorGuard(donorLock);
        state = parameters.copyState();
        state.setProperty("module", getEditorModule(), nullptr);
        if (donorPath.isEmpty())
            state.removeProperty("donorPath", nullptr);
        else
            state.setProperty("donorPath", donorPath, nullptr);
        const juce::SpinLock::ScopedLockType seedGuard(seedLock);
        for (size_t i = 0; i < timbre.size(); ++i)
            timbre[i] = seed[i].load(std::memory_order_relaxed);
    }
    juce::StringArray a;
    for (auto v : timbre)
        a.add(juce::String(v, 9));
    state.setProperty("seed", a.joinIntoString(","), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, dest);
}
void ExperimentalProcessor::setStateInformation(const void* data, int size)
{
    if (data == nullptr || size <= 0 || size > 4 * 1024 * 1024)
        return;
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(parameters.state.getType()))
        {
            const auto state = juce::ValueTree::fromXml(*xml);
            const auto a = juce::StringArray::fromTokens(state.getProperty("seed").toString(), ",", "");
            juce::String path;
            uint64_t generation = 0;
            {
                const juce::ScopedLock donorGuard(donorLock);
                parameters.replaceState(state);
                setEditorModule((int)state.getProperty("module", 1));
                if (a.size() == 32)
                {
                    const juce::SpinLock::ScopedLockType seedGuard(seedLock);
                    for (int i = 0; i < 32; ++i)
                    {
                        const float parsed = a[i].getFloatValue(),
                                    v = std::isfinite(parsed) ? std::clamp(parsed, 0.f, 8.f) : 0.f;
                        requestedSeed[(size_t)i].store(v, std::memory_order_relaxed);
                        seed[(size_t)i].store(v, std::memory_order_relaxed);
                    }
                    mutationRequested.store(0);
                    captureRequested.store(false);
                    seedRequested.store(true, std::memory_order_release);
                }
                path = donorPath = state.getProperty("donorPath").toString();
                generation = donorGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
                // Never play the previous state's file while the replacement is loading.
                donor.store(nullptr, std::memory_order_release);
                donorStatus.store(path.isEmpty() ? DonorStatus::empty : DonorStatus::loading);
            }
            if (path.isNotEmpty())
                donorPool.addJob([this, path, generation] { reloadDonor(path, generation); });
        }
}
juce::AudioProcessorEditor* ExperimentalProcessor::createEditor()
{
    return new ExperimentalEditor(*this);
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ExperimentalProcessor();
}
