// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "Params.h"
#include "Engine.h"

class ExperimentalProcessor final : public juce::AudioProcessor
{
  public:
    ExperimentalProcessor();
    ~ExperimentalProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override
    {
        return true;
    }
    const juce::String getName() const override
    {
        return JucePlugin_Name;
    }
    bool acceptsMidi() const override
    {
        return true;
    }
    bool producesMidi() const override
    {
        return false;
    }
    bool isMidiEffect() const override
    {
        return false;
    }
    double getTailLengthSeconds() const override
    {
        return 8.;
    }
    int getNumPrograms() override
    {
        return 1;
    }
    int getCurrentProgram() override
    {
        return 0;
    }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override
    {
        return "Default";
    }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    void setParameter(const juce::String&, float);
    void preset(int);
    void mutateSelected(int);
    int readVisualSamples(float* destination, int capacity);
    void setVisualTimbre(const std::array<float, 32>&);
    std::array<float, 32> visualTimbre() const noexcept
    {
        std::array<float, 32> snapshot{};
        for (size_t i = 0; i < snapshot.size(); ++i)
            snapshot[i] = seed[i].load(std::memory_order_relaxed);
        return snapshot;
    }
    void capture()
    {
#if JucePlugin_IsSynth
        captureRequested.store(true);
#endif
    }
    juce::Result loadDonor(const juce::File&);
    void resetDonor();
    float value(const char*) const;
    juce::String donorName() const;
    void setEditorModule(int m)
    {
        editorModule.store(std::clamp(m, JucePlugin_IsSynth ? 0 : 1, 5));
    }
    int getEditorModule() const
    {
        return std::clamp(editorModule.load(), JucePlugin_IsSynth ? 0 : 1, 5);
    }
    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState parameters;
    juce::MidiKeyboardState keyboard;
    std::atomic<float> inputPeak{0}, outputPeak{0};
    std::atomic<int> voices{0};
    std::atomic<bool> hasSidechain{false};

  private:
    struct Donor
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 48000;
        juce::String name, path;
    };
    enum class DonorStatus
    {
        empty,
        loading,
        ready,
        unavailable
    };
    std::unique_ptr<experimental::Engine> engine;
    std::array<std::atomic<float>*, std::size(exp3::continuous)> values{};
    std::array<std::atomic<float>*, 6> on{};
    std::array<std::atomic<float>*, 4> flags{};
    std::array<std::atomic<float>*, 5> choices{};
    juce::RangedAudioParameter* bypassParameter = nullptr;
    std::array<std::atomic<float>, 32> seed{};
    juce::AbstractFifo visualFifo{8192};
    std::array<float, 8192> visualSamples{};
    std::array<std::atomic<float>, 32> requestedSeed{};
    mutable juce::SpinLock seedLock;
    bool seedPublicationPending = false;
    std::atomic<bool> captureRequested{false}, seedRequested{false};
    std::atomic<uint32_t> mutationRequested{0};
    std::atomic<int> editorModule{1};
    mutable juce::CriticalSection donorLock;
    std::vector<std::unique_ptr<Donor>> donorVersions;
    juce::String donorPath;
    std::atomic<uint64_t> donorGeneration{0};
    std::atomic<DonorStatus> donorStatus{DonorStatus::empty};
    std::atomic<Donor*> donor{nullptr};
    Donor* playingDonor = nullptr;
    double donorPosition = 0;
    uint64_t heldNotesLo = 0, heldNotesHi = 0;
    double sampleRate = 48000;
    void process(juce::AudioBuffer<float>&, juce::MidiBuffer&, bool);
    experimental::Settings settings() const;
    void midi(const juce::MidiMessage&);
    bool publishSeed();
    juce::Result readDonor(const juce::File&, std::unique_ptr<Donor>&, uint64_t generation = 0);
    void reloadDonor(const juce::String&, uint64_t generation);
    // Declared last and joined explicitly before retained donor buffers are destroyed.
    juce::ThreadPool donorPool{1};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExperimentalProcessor)
};
