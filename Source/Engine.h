// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Original DSP for Exp3r1ment4l. This is not an implementation of any commercial
// plugin. Spectral stages add 1024 samples; time-domain paths render directly.
// The spectral pitch stage
// remaps FFT energy to a scale; it does not identify or separate instruments.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <vector>

namespace experimental
{

struct Settings
{
    bool synthOn = false, grainsOn = false, permuteOn = false, morphOn = false;
    bool spectralOn = false, pitchOn = false, bypass = false, freeze = false;
    bool grainFreeze = false, sync = false;
    float mix = 1, outputDb = 0, morphBlend = .5f, morphFocus = .5f, morphMotion = 0;
    float spectralWarp = 0, spectralSmear = 0, spectralDynamics = 0;
    float pitchAmount = 1, transpose = 0, grainSize = 80, grainDensity = 8;
    float grainPitch = 0, grainPosition = .25f, permuteClock = 12000;
    float permuteDepth = .5f, permuteFeedback = .2f, synthTone = .5f;
    float synthSpread = .2f, synthRelease = 600, synthLevel = -12;
    float synthAttack = 10, synthFM = .25f, motion = 0, rate = 1;
    int permuteOperator = 0, root = 0, scale = 1, donorSource = 0, modTarget = 0;
};

class Engine final
{
  public:
    static constexpr int latency = 1024;
    static constexpr int fftSize = 1024, hopSize = 256, binCount = fftSize / 2 + 1;
    static constexpr int voiceCount = 8, partialCount = 32;

    Engine() noexcept
    {
        defaultTimbre();
    }

    // The only allocation site. Audio buffers accommodate four seconds at the
    // actual sample rate; processing, MIDI, captures and mutation never allocate.
    void prepare(double sampleRate)
    {
        sr = std::isfinite(sampleRate) ? std::clamp(sampleRate, 8000.0, 384000.0) : 48000.0;
        sampleSmoothing = 1.f - static_cast<float>(std::exp(-1.0 / (sr * .02)));
        frameSmoothing = 1.f - static_cast<float>(std::exp(-hopSize / (sr * .035)));
        const auto capacity = static_cast<std::size_t>(std::ceil(sr * 4.0)) + fftSize + 8;
        for (auto& buffer : live)
            buffer.assign(capacity, 0.f);
        for (auto& buffer : delay)
            buffer.assign(capacity, 0.f);
        for (int i = 0; i < fftSize; ++i)
        {
            window[i] = std::sqrt(std::max(0.f, .5f - .5f * std::cos(tau * i / fftSize)));
            unsigned n = static_cast<unsigned>(i), r = 0;
            for (int bit = 0; bit < 10; ++bit)
            {
                r = (r << 1) | (n & 1);
                n >>= 1;
            }
            reversed[i] = static_cast<unsigned short>(r);
        }
        for (int k = 0; k < fftSize / 2; ++k)
            twiddle[k] = {std::cos(-tau * k / fftSize), std::sin(-tau * k / fftSize)};
        reset();
    }

    void reset() noexcept
    {
        for (auto& a : live)
            std::fill(a.begin(), a.end(), 0.f);
        for (auto& a : delay)
            std::fill(a.begin(), a.end(), 0.f);
        for (auto& a : input)
            a.fill(0);
        for (auto& a : donorInput)
            a.fill(0);
        for (auto& a : ola)
            a.fill(0);
        for (auto& a : dryDelay)
            a.fill(0);
        for (auto& a : previousPhase)
            a.fill(0);
        for (auto& a : remapPhase)
            a.fill(0);
        for (auto& a : frozenPhase)
            a.fill(0);
        for (auto& a : frozenMagnitude)
            a.fill(0);
        for (auto& a : frozenFrequency)
            a.fill(0);
        for (auto& a : smearMagnitude)
            a.fill(0);
        for (auto& a : dynamicGain)
            a.fill(1);
        lastMagnitude.fill(0);
        for (auto& v : voices)
            v = Voice{};
        for (auto& h : heads)
            h = Head{};
        position = livePosition = delayPosition = hopCounter = frames = 0;
        availableLive = 0;
        tickCounter = 0;
        grainCursor = 0;
        reverseAnchor = stutterAnchor = 0;
        held = {0, 0};
        grainPhase = 1;
        clockPhase = 0;
        lfoPhase = 0;
        randomState = 0x83d71f24u;
        ageCounter = 0;
        sustain = false;
        bend = bendTarget = 0;
        wasFrozen = false;
        donorFrameAvailable = false;
        synthAlpha = settings.synthOn ? 1.f : 0.f;
        grainsAlpha = settings.grainsOn ? 1.f : 0.f;
        permuteAlpha = settings.permuteOn ? 1.f : 0.f;
        morphAlpha = spectralAlpha = pitchAlpha = freezeAlpha = 0;
        smoothMix = settings.mix;
        smoothOutput = gain(settings.outputDb);
        bypassAlpha = settings.bypass ? 1.f : 0.f;
        current = settings;
        refreshPartials();
        partialGain = targetPartialGain;
        partialRatio = targetPartialRatio;
    }

    void setSettings(const Settings& next) noexcept
    {
        settings = next;
        settings.mix = bounded(next.mix, 0, 1, 1);
        settings.outputDb = bounded(next.outputDb, -60, 18, 0);
        settings.morphBlend = bounded(next.morphBlend, 0, 1, .5f);
        settings.morphFocus = bounded(next.morphFocus, 0, 1, .5f);
        settings.morphMotion = bounded(next.morphMotion, 0, 1, 0);
        settings.spectralWarp = bounded(next.spectralWarp, -1, 1, 0);
        settings.spectralSmear = bounded(next.spectralSmear, 0, 1, 0);
        settings.spectralDynamics = bounded(next.spectralDynamics, -1, 1, 0);
        settings.pitchAmount = bounded(next.pitchAmount, 0, 1, 1);
        settings.transpose = bounded(next.transpose, -48, 48, 0);
        settings.grainSize = bounded(next.grainSize, 5, 1000, 80);
        settings.grainDensity = bounded(next.grainDensity, .25f, 80, 8);
        settings.grainPitch = bounded(next.grainPitch, -36, 36, 0);
        settings.grainPosition = bounded(next.grainPosition, 0, 1, .25f);
        settings.permuteClock = bounded(next.permuteClock, 30, 192000, 12000);
        settings.permuteDepth = bounded(next.permuteDepth, 0, 1, .5f);
        settings.permuteFeedback = bounded(next.permuteFeedback, 0, .94f, .2f);
        settings.synthTone = bounded(next.synthTone, 0, 1, .5f);
        settings.synthSpread = bounded(next.synthSpread, 0, 1, .2f);
        settings.synthRelease = bounded(next.synthRelease, 5, 10000, 600);
        settings.synthLevel = bounded(next.synthLevel, -60, 6, -12);
        settings.synthAttack = bounded(next.synthAttack, .5f, 5000, 10);
        settings.synthFM = bounded(next.synthFM, 0, 1, .25f);
        settings.motion = bounded(next.motion, 0, 1, 0);
        // rate is Hz. The wrapper resolves host tempo when sync is enabled.
        settings.rate = bounded(next.rate, .01f, 40, 1);
        settings.permuteOperator = std::clamp(next.permuteOperator, 0, 3);
        settings.root = ((next.root % 12) + 12) % 12;
        settings.scale = std::clamp(next.scale, 0, 7);
        settings.modTarget = std::clamp(next.modTarget, 0, 6);
        refreshPartials();
    }

    void noteOn(int note, float velocity) noexcept
    {
        note = std::clamp(note, 0, 127);
        velocity = bounded(velocity, 0, 1, 0);
        if (velocity == 0)
        {
            noteOff(note);
            return;
        }
        Voice* chosen = nullptr;
        for (auto& v : voices)
            if (!v.active)
            {
                chosen = &v;
                break;
            }
        if (!chosen)
        {
            chosen = &voices[0];
            for (auto& v : voices)
                if (v.envelope < chosen->envelope || (v.envelope == chosen->envelope && v.age < chosen->age))
                    chosen = &v;
        }
        // A short tail keeps voice stealing from introducing an abrupt step.
        const float tail = chosen->last;
        *chosen = Voice{};
        chosen->active = true;
        chosen->keyDown = true;
        chosen->note = note;
        chosen->velocity = velocity;
        chosen->age = ++ageCounter;
        chosen->frequency = 440.0 * std::exp2((note - 69) / 12.0);
        chosen->tail = tail;
    }
    void noteOff(int note) noexcept
    {
        for (auto& v : voices)
            if (v.active && v.note == note && v.keyDown)
            {
                v.keyDown = false;
                if (sustain)
                    v.sustained = true;
                else
                    v.releasing = true;
            }
    }
    void allNotesOff() noexcept
    {
        sustain = false;
        for (auto& v : voices)
        {
            v.keyDown = false;
            v.sustained = false;
            v.releasing = true;
        }
    }
    void setSustain(bool down) noexcept
    {
        sustain = down;
        if (!down)
            for (auto& v : voices)
                if (v.sustained && !v.keyDown)
                {
                    v.sustained = false;
                    v.releasing = true;
                }
    }
    void setPitchBend(float semitones) noexcept
    {
        bendTarget = bounded(semitones, -48, 48, 0);
    }
    int activeVoiceCount() const noexcept
    {
        int result = 0;
        for (const auto& v : voices)
            result += v.active ? 1 : 0;
        return result;
    }
    std::array<float, 32> timbre() const noexcept
    {
        return seed;
    }
    int activeLatency() const noexcept
    {
        return settings.morphOn || settings.spectralOn || settings.pitchOn ? latency : 0;
    }
    void setTimbre(const std::array<float, 32>& values) noexcept
    {
        seed = values;
        normalizeTimbre();
        refreshPartials();
    }
    void mutateTimbre(std::uint32_t mutationSeed) noexcept
    {
        std::uint32_t rng = mutationSeed ? mutationSeed : 0x84ad792bu;
        for (int p = 0; p < partialCount; ++p)
        {
            const float u = random(rng);
            seed[p] = std::max(.0001f, seed[p]) * (.4f + u * 1.7f);
            if (random(rng) < .1f)
                seed[p] += .03f * random(rng);
        }
        normalizeTimbre();
        refreshPartials();
    }
    void captureTimbre() noexcept
    {
        // Estimate a usable root from the strongest peak in 40..1200 Hz, then
        // sample its harmonic neighbourhoods. This is sound-seed extraction,
        // not an inverse model fitting or a reconstruction promise.
        const int low = std::max(1, static_cast<int>(40 * fftSize / sr));
        const int high = std::min(binCount - 1, static_cast<int>(1200 * fftSize / sr));
        int peak = low;
        for (int k = low; k <= high; ++k)
            if (lastMagnitude[k] > lastMagnitude[peak])
                peak = k;
        if (lastMagnitude[peak] < .0001f)
            return;
        std::array<float, 32> captured{};
        for (int p = 0; p < partialCount; ++p)
        {
            const int centre = peak * (p + 1);
            if (centre >= binCount)
                break;
            for (int k = std::max(1, centre - 1); k <= std::min(binCount - 1, centre + 1); ++k)
                captured[p] = std::max(captured[p], lastMagnitude[k]);
            captured[p] /= lastMagnitude[peak];
        }
        seed = captured;
        normalizeTimbre();
        refreshPartials();
    }

    std::array<float, 2> process(float left, float right, float donorLeft = 0, float donorRight = 0,
                                 bool donorAvailable = false) noexcept
    {
        left = finite(left);
        right = finite(right);
        donorLeft = finite(donorLeft);
        donorRight = finite(donorRight);
        if (live[0].empty())
            return {left, right}; // prepare must precede host playback
        smoothControls();
        lfoPhase += current.rate / static_cast<float>(sr);
        if (lfoPhase >= 1)
            lfoPhase -= std::floor(lfoPhase);
        const float lfo = std::sin(tau * lfoPhase);
        const float generated = renderSynth(lfo) * synthAlpha;
        std::array<float, 2> signal{left + generated, right + generated};
        signal = renderGrains(signal, lfo);
        signal = renderPermute(signal, lfo);
        const std::array<float, 2> original{left, right};
        const bool spectralPath = activeLatency() != 0;
        std::array<float, 2> result{};
        for (int ch = 0; ch < 2; ++ch)
        {
            const float dry = spectralPath ? dryDelay[ch][position] : original[ch];
            dryDelay[ch][position] = original[ch];
            const float wet = spectralPath ? ola[ch][position] : signal[ch];
            ola[ch][position] = 0;
            input[ch][position] = signal[ch];
            donorInput[ch][position] = donorAvailable ? (ch == 0 ? donorLeft : donorRight) : 0;
            const float mixed = (dry + (wet - dry) * smoothMix) * smoothOutput;
            result[ch] = finite(mixed + (dry - mixed) * bypassAlpha);
        }
        donorFrameAvailable = donorFrameAvailable || donorAvailable;
        const int next = (position + 1) & (fftSize - 1);
        if (++hopCounter == hopSize)
        {
            hopCounter = 0;
            processFrame(next, lfo);
            donorFrameAvailable = false;
        }
        position = next;
        return result;
    }

  private:
    static constexpr float pi = 3.14159265358979323846f, tau = 2 * pi;
    using Spectrum = std::array<std::complex<float>, fftSize>;
    struct Voice
    {
        bool active = false, keyDown = false, sustained = false, releasing = false, attacked = false;
        int note = 0;
        std::uint64_t age = 0;
        float envelope = 0, velocity = 0, tail = 0, last = 0;
        double frequency = 440, fmPhase = 0;
        std::array<double, partialCount> phase{};
    };
    struct Head
    {
        bool active = false;
        double read = 0, step = 1;
        int age = 0, length = 1;
        float pan = 0;
    };
    Settings settings{}, current{};
    double sr = 48000;
    float sampleSmoothing = .001f, frameSmoothing = .25f;
    float smoothMix = 1, smoothOutput = 1, bypassAlpha = 0;
    float synthAlpha = 0, grainsAlpha = 0, permuteAlpha = 0;
    float morphAlpha = 0, spectralAlpha = 0, pitchAlpha = 0, freezeAlpha = 0;
    float bend = 0, bendTarget = 0, lfoPhase = 0;
    int position = 0, livePosition = 0, delayPosition = 0, hopCounter = 0;
    std::uint64_t frames = 0;
    int availableLive = 0, grainCursor = 0;
    bool sustain = false, wasFrozen = false, donorFrameAvailable = false;
    double grainPhase = 1, clockPhase = 0, reverseAnchor = 0, stutterAnchor = 0;
    std::uint64_t tickCounter = 0, ageCounter = 0;
    std::uint32_t randomState = 0x83d71f24u;
    std::array<float, 2> held{};
    std::array<Voice, voiceCount> voices{};
    std::array<Head, 4> heads{};
    std::array<float, partialCount> seed{}, partialGain{}, targetPartialGain{};
    std::array<float, partialCount> partialRatio{}, targetPartialRatio{};
    std::array<std::vector<float>, 2> live{}, delay{};
    std::array<std::array<float, fftSize>, 2> input{}, donorInput{}, ola{}, dryDelay{};
    std::array<std::array<float, binCount>, 2> previousPhase{}, remapPhase{};
    std::array<std::array<float, binCount>, 2> frozenPhase{}, frozenMagnitude{}, frozenFrequency{};
    std::array<std::array<float, binCount>, 2> smearMagnitude{}, dynamicGain{};
    std::array<float, binCount> lastMagnitude{};
    std::array<float, fftSize> window{};
    std::array<unsigned short, fftSize> reversed{};
    std::array<std::complex<float>, fftSize / 2> twiddle{};
    std::array<Spectrum, 2> spectrum{}, donorSpectrum{}, processed{};
    std::array<std::array<float, binCount>, 2> magnitude{}, phase{}, trueFrequency{};

    static float finite(float x) noexcept
    {
        return std::isfinite(x) ? std::clamp(x, -64.f, 64.f) : 0.f;
    }
    static float bounded(float x, float low, float high, float fallback) noexcept
    {
        return std::isfinite(x) ? std::clamp(x, low, high) : fallback;
    }
    static float gain(float db) noexcept
    {
        return std::pow(10.f, db * .05f);
    }
    static float wrapped(float x) noexcept
    {
        return x - tau * std::floor((x + pi) / tau);
    }
    static float random(std::uint32_t& state) noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state >> 8) * (1.f / 16777216.f);
    }
    static float follow(float x, float target, float coefficient) noexcept
    {
        const float next = x + (target - x) * coefficient;
        return coefficient > 0.f && next == x ? target : next;
    }
    void defaultTimbre() noexcept
    {
        for (int p = 0; p < partialCount; ++p)
            seed[p] = 1.f / std::pow(static_cast<float>(p + 1), 1.35f);
        normalizeTimbre();
        refreshPartials();
    }
    void normalizeTimbre() noexcept
    {
        float total = 0;
        for (float& value : seed)
        {
            value = bounded(value, 0, 16, 0);
            total += value;
        }
        if (total < 1.e-9f)
        {
            seed.fill(0);
            seed[0] = 1;
        }
        // Preserve already normalized saved seeds bit-for-bit across state
        // restoration, rather than renormalizing floating-point rounding noise.
        else if (std::abs(total - 1.f) > .000001f)
            for (float& value : seed)
                value /= total;
    }
    void refreshPartials() noexcept
    {
        float total = 0;
        for (int p = 0; p < partialCount; ++p)
        {
            targetPartialGain[p] =
                seed[p] * std::pow(static_cast<float>(p + 1), (settings.synthTone - .5f) * 2.5f);
            total += targetPartialGain[p];
            targetPartialRatio[p] =
                (p + 1) * std::exp2(settings.synthSpread * .035f * std::sin((p + 1) * 1.731f));
        }
        if (total > 0)
            for (float& value : targetPartialGain)
                value /= total;
    }
    void smoothControls() noexcept
    {
        synthAlpha = follow(synthAlpha, settings.synthOn ? 1.f : 0.f, sampleSmoothing);
        grainsAlpha = follow(grainsAlpha, settings.grainsOn ? 1.f : 0.f, sampleSmoothing);
        permuteAlpha = follow(permuteAlpha, settings.permuteOn ? 1.f : 0.f, sampleSmoothing);
        smoothMix = follow(smoothMix, settings.mix, sampleSmoothing);
        smoothOutput = follow(smoothOutput, gain(settings.outputDb), sampleSmoothing);
        bypassAlpha = follow(bypassAlpha, settings.bypass ? 1.f : 0.f, sampleSmoothing);
        bend = follow(bend, bendTarget, sampleSmoothing);
#define EXP_SMOOTH(name) current.name = follow(current.name, settings.name, sampleSmoothing)
        EXP_SMOOTH(morphBlend);
        EXP_SMOOTH(morphFocus);
        EXP_SMOOTH(morphMotion);
        EXP_SMOOTH(spectralWarp);
        EXP_SMOOTH(spectralSmear);
        EXP_SMOOTH(spectralDynamics);
        EXP_SMOOTH(pitchAmount);
        EXP_SMOOTH(transpose);
        EXP_SMOOTH(grainSize);
        EXP_SMOOTH(grainDensity);
        EXP_SMOOTH(grainPitch);
        EXP_SMOOTH(grainPosition);
        EXP_SMOOTH(permuteClock);
        EXP_SMOOTH(permuteDepth);
        EXP_SMOOTH(permuteFeedback);
        EXP_SMOOTH(synthRelease);
        EXP_SMOOTH(synthLevel);
        EXP_SMOOTH(synthAttack);
        EXP_SMOOTH(synthFM);
        EXP_SMOOTH(motion);
        EXP_SMOOTH(rate);
#undef EXP_SMOOTH
        for (int p = 0; p < partialCount; ++p)
        {
            partialGain[p] = follow(partialGain[p], targetPartialGain[p], sampleSmoothing);
            partialRatio[p] = follow(partialRatio[p], targetPartialRatio[p], sampleSmoothing);
        }
    }
    float renderSynth(float lfo) noexcept
    {
        const double bendRatio = std::exp2(bend / 12.0);
        const float release =
            static_cast<float>(std::exp(-6.90775527898 / (sr * current.synthRelease * .001)));
        const float attack = static_cast<float>(1.0 / (sr * current.synthAttack * .001));
        const float decay = static_cast<float>(1.0 - std::exp(-1.0 / (sr * .075)));
        const float fm = std::clamp(
            current.synthFM + (settings.modTarget == 5 ? current.motion * .4f * lfo : 0.f), 0.f, 1.f);
        float sum = 0;
        for (auto& voice : voices)
            if (voice.active)
            {
                if (voice.releasing)
                {
                    voice.envelope *= release;
                    if (voice.envelope < .00001f)
                    {
                        voice.active = false;
                        continue;
                    }
                }
                else if (voice.envelope < 1.f && !voice.attacked)
                {
                    voice.envelope = std::min(1.f, voice.envelope + attack);
                    if (voice.envelope >= 1.f)
                        voice.attacked = true;
                }
                else
                    voice.envelope = follow(voice.envelope, .8f, decay);
                const double frequency = voice.frequency * bendRatio;
                voice.fmPhase += frequency * 2 / sr;
                voice.fmPhase -= std::floor(voice.fmPhase);
                const float modulator = std::sin(static_cast<float>(tau * voice.fmPhase));
                float sample = 0;
                for (int p = 0; p < partialCount; ++p)
                {
                    const double hz = frequency * partialRatio[p];
                    if (hz >= sr * .45)
                        continue;
                    voice.phase[p] += hz / sr;
                    voice.phase[p] -= std::floor(voice.phase[p]);
                    // Reserve the approximate Carson bandwidth of the FM carrier.
                    // Close to Nyquist, reduce its index rather than aliasing the
                    // first significant upper sidebands back into the spectrum.
                    const float availableIndex =
                        static_cast<float>(std::max(0.0, (sr * .45 - hz) / (frequency * 2) - 1.0));
                    const float index = std::min(fm * 2.5f / (p + 1), availableIndex);
                    sample += partialGain[p] *
                              std::sin(static_cast<float>(tau * voice.phase[p]) + modulator * index);
                }
                sample = sample * voice.envelope * voice.velocity + voice.tail;
                voice.tail *= .97f;
                voice.last = sample;
                sum += sample;
            }
        return sum * gain(current.synthLevel);
    }
    float readBuffer(const std::vector<float>& buffer, double at) const noexcept
    {
        const double size = static_cast<double>(buffer.size());
        at -= size * std::floor(at / size);
        const auto a = static_cast<std::size_t>(at);
        const auto b = a + 1 == buffer.size() ? 0 : a + 1;
        const float fraction = static_cast<float>(at - a);
        return buffer[a] + (buffer[b] - buffer[a]) * fraction;
    }
    std::array<float, 2> renderGrains(const std::array<float, 2>& signal, float lfo) noexcept
    {
        const int size = static_cast<int>(live[0].size());
        if (!settings.grainFreeze)
        {
            for (int ch = 0; ch < 2; ++ch)
                live[ch][livePosition] = signal[ch];
            livePosition = (livePosition + 1) % size;
            availableLive = std::min(size, availableLive + 1);
        }
        grainPhase += current.grainDensity / sr;
        if (grainPhase >= 1)
        {
            grainPhase -= std::floor(grainPhase);
            auto& head = heads[grainCursor];
            grainCursor = (grainCursor + 1) % 4;
            head = Head{};
            head.active = availableLive > 1;
            head.length = std::max(8, static_cast<int>(std::min(current.grainSize * .001 * sr,
                                                                sr * 3.8 / current.grainDensity)));
            const float positionMod = settings.modTarget == 3 ? current.motion * .2f * lfo : 0;
            const float where = std::clamp(current.grainPosition + positionMod, 0.f, 1.f);
            const double history = std::min(sr * 2.0, static_cast<double>(availableLive - 2));
            const double safeHistory = std::max(0.0, history);
            const double drift = (random(randomState) - .5) * head.length * .12;
            const double offset = std::clamp(where * safeHistory + head.length + drift, 1.0,
                                             std::max(1.0, static_cast<double>(availableLive - 1)));
            head.read = livePosition - offset;
            head.step = std::exp2((current.grainPitch + (random(randomState) - .5f) * .15f) / 12.0);
            head.pan = (random(randomState) - .5f) * .5f;
        }

        std::array<float, 2> sum{0, 0};
        float overlap = 0;
        for (auto& head : heads)
            if (head.active)
            {
                const float fraction = static_cast<float>(head.age) / head.length;
                const float envelope = .5f - .5f * std::cos(tau * fraction);
                sum[0] += readBuffer(live[0], head.read) * envelope * (1 - head.pan);
                sum[1] += readBuffer(live[1], head.read) * envelope * (1 + head.pan);
                overlap += envelope;
                head.read += head.step;
                if (++head.age >= head.length)
                    head.active = false;
            }
        const float normalization = 1.f / std::max(1.f, overlap);
        std::array<float, 2> result{};
        for (int ch = 0; ch < 2; ++ch)
        {
            const float granular = sum[ch] * normalization;
            result[ch] = finite(signal[ch] + (granular - signal[ch]) * grainsAlpha);
        }
        return result;
    }

    // Restored locally from the original design: a clocked 15-bit processor
    // with address/word operations. This section was truncated in chat history.
    std::array<float, 2> renderPermute(const std::array<float, 2>& signal, float lfo) noexcept
    {
        const int size = static_cast<int>(delay[0].size());
        const float disorder = std::clamp(
            current.permuteDepth + (settings.modTarget == 4 ? current.motion * .35f * lfo : 0.f), 0.f, 1.f);
        const int span = std::clamp(static_cast<int>(sr * (.012 + .168 * disorder)), 8, size / 4);
        for (int ch = 0; ch < 2; ++ch)
            delay[ch][delayPosition] =
                std::clamp(finite(signal[ch] + held[ch] * current.permuteFeedback), -1.8f, 1.8f);
        clockPhase += std::min(static_cast<double>(current.permuteClock), sr) / sr;
        if (clockPhase >= 1)
        {
            clockPhase -= std::floor(clockPhase);
            ++tickCounter;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float fresh = delay[ch][delayPosition];
                float operated = fresh;
                if (settings.permuteOperator < 2)
                {
                    constexpr std::uint32_t wordMask = 0x7fff;
                    const int signedWord =
                        static_cast<int>(std::lround(std::clamp(fresh, -1.f, .99993896f) * 16384.f));
                    std::uint32_t word = static_cast<std::uint32_t>(signedWord) & wordMask;
                    if (settings.permuteOperator == 0)
                    {
                        const std::uint32_t mask = static_cast<std::uint32_t>(disorder * 8191.f);
                        word ^= mask;
                    }
                    else
                    {
                        const unsigned shift = static_cast<unsigned>(std::lround(disorder * 12.f));
                        if (shift != 0)
                            word = ((word << shift) | (word >> (15 - shift))) & wordMask;
                    }
                    const int decoded =
                        (word & 0x4000) ? static_cast<int>(word) - 32768 : static_cast<int>(word);
                    operated = decoded / 16384.f;
                    if (std::abs(fresh) < 1.e-9f)
                        operated = 0;
                }
                else if (settings.permuteOperator == 2)
                {
                    const int segmentPosition = static_cast<int>(reverseAnchor);
                    operated = readBuffer(delay[ch], delayPosition - 1 - 2.0 * segmentPosition);
                }
                else
                {
                    operated = readBuffer(delay[ch], stutterAnchor + reverseAnchor);
                }
                held[ch] = finite(fresh + (operated - fresh) * disorder);
            }
        }
        const int next = (delayPosition + 1) % size;
        if (++reverseAnchor >= span)
        {
            reverseAnchor = 0;
            stutterAnchor = next - span;
        }
        delayPosition = next;
        std::array<float, 2> result{};
        for (int ch = 0; ch < 2; ++ch)
            result[ch] = finite(signal[ch] + (held[ch] - signal[ch]) * permuteAlpha);
        return result;
    }

    void fft(Spectrum& values, bool inverse) noexcept
    {
        for (int i = 0; i < fftSize; ++i)
            if (reversed[i] > i)
                std::swap(values[i], values[reversed[i]]);
        for (int length = 2; length <= fftSize; length *= 2)
        {
            const int half = length / 2, stride = fftSize / length;
            for (int block = 0; block < fftSize; block += length)
                for (int j = 0; j < half; ++j)
                {
                    const auto w = inverse ? std::conj(twiddle[j * stride]) : twiddle[j * stride];
                    const auto odd = values[block + j + half] * w;
                    const auto even = values[block + j];
                    values[block + j] = even + odd;
                    values[block + j + half] = even - odd;
                }
        }
        if (inverse)
            for (auto& value : values)
                value /= static_cast<float>(fftSize);
    }
    static float spectrumMagnitude(const Spectrum& values, float bin) noexcept
    {
        if (bin < 0 || bin > binCount - 1)
            return 0;
        const int a = static_cast<int>(bin), b = std::min(a + 1, binCount - 1);
        const float fraction = bin - a;
        return std::abs(values[a]) * (1 - fraction) + std::abs(values[b]) * fraction;
    }
    float scaleFrequency(float hz) const noexcept
    {
        if (settings.scale == 7 || hz < 20)
            return hz * std::exp2(current.transpose / 12.f);
        constexpr std::uint16_t masks[]{0xfffu, 0xab5u, 0x5adu, 0x295u, 0x555u, 0x6dbu, 0x9adu, 0xfffu};
        const float note = 69.f + 12.f * std::log2(std::max(hz, .001f) / 440.f);
        const int centre = static_cast<int>(std::lround(note));
        float best = 0, distance = 1000;
        for (int candidate = centre - 12; candidate <= centre + 12; ++candidate)
        {
            const int degree = ((candidate - settings.root) % 12 + 12) % 12;
            if (!(masks[settings.scale] & (1u << degree)))
                continue;
            const float d = std::abs(note - candidate);
            if (d < distance)
            {
                distance = d;
                best = static_cast<float>(candidate);
            }
        }
        return 440.f * std::exp2((best - 69.f + current.transpose) / 12.f);
    }
    void processFrame(int oldest, float lfo) noexcept
    {
        morphAlpha = follow(morphAlpha, settings.morphOn && donorFrameAvailable ? 1.f : 0.f, frameSmoothing);
        spectralAlpha = follow(spectralAlpha, settings.spectralOn ? 1.f : 0.f, frameSmoothing);
        pitchAlpha = follow(pitchAlpha, settings.pitchOn ? 1.f : 0.f, frameSmoothing);
        freezeAlpha = follow(freezeAlpha, settings.spectralOn && settings.freeze ? 1.f : 0.f, frameSmoothing);
        const bool frozen = settings.freeze && settings.spectralOn;
        const bool captureFreeze = frozen && !wasFrozen;
        wasFrozen = frozen;
        lastMagnitude.fill(0);
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < fftSize; ++i)
            {
                const int at = (oldest + i) & (fftSize - 1);
                spectrum[ch][i] = {input[ch][at] * window[i], 0};
                if (donorFrameAvailable && morphAlpha > .00001f)
                    donorSpectrum[ch][i] = {donorInput[ch][at] * window[i], 0};
            }
            fft(spectrum[ch], false);
            if (donorFrameAvailable && morphAlpha > .00001f)
                fft(donorSpectrum[ch], false);
            for (int k = 0; k < binCount; ++k)
            {
                const auto value = spectrum[ch][k];
                magnitude[ch][k] = std::abs(value);
                phase[ch][k] = std::atan2(value.imag(), value.real());
                const float expected = tau * k / fftSize;
                const float delta = wrapped(phase[ch][k] - previousPhase[ch][k] - expected * hopSize);
                trueFrequency[ch][k] = frames == 0 ? expected : expected + delta / hopSize;
                previousPhase[ch][k] = phase[ch][k];
                lastMagnitude[k] += magnitude[ch][k] * .5f;
            }
            processed[ch].fill({0, 0});
            float morphBlend = current.morphBlend;
            if (settings.modTarget == 1)
                morphBlend += current.motion * .4f * lfo;
            morphBlend = std::clamp(morphBlend, 0.f, 1.f);
            const float warp = std::clamp(current.spectralWarp +
                                              (settings.modTarget == 2 ? current.motion * .5f * lfo : 0.f),
                                          -1.f, 1.f) *
                               spectralAlpha;
            const float amount =
                std::clamp(current.pitchAmount + (settings.modTarget == 6 ? current.motion * .5f * lfo : 0.f),
                           0.f, 1.f) *
                pitchAlpha;
            const float smear = current.spectralSmear * .96f;
            const float reference = 1000.f * fftSize / static_cast<float>(sr);
            for (int k = 0; k < binCount; ++k)
            {
                float mag = magnitude[ch][k];
                float angle = phase[ch][k];
                const float nominal = k * static_cast<float>(sr) / fftSize;
                if (morphAlpha > .00001f)
                {
                    const float focused = std::clamp(1.f + (current.morphFocus - .5f) * .35f *
                                                               std::log2(std::max(nominal, 30.f) / 1000.f),
                                                     0.f, 2.f);
                    const float donorBin = k * std::exp2(current.morphMotion * .6f * lfo);
                    const float donorMag = spectrumMagnitude(donorSpectrum[ch], donorBin);
                    const float blend = std::clamp(morphBlend * focused, 0.f, 1.f) * morphAlpha;
                    mag = mag + (donorMag - mag) * blend;
                }
                if (captureFreeze)
                {
                    frozenMagnitude[ch][k] = mag;
                    frozenFrequency[ch][k] = trueFrequency[ch][k];
                    frozenPhase[ch][k] = angle;
                }
                else
                    frozenPhase[ch][k] = wrapped(frozenPhase[ch][k] + frozenFrequency[ch][k] * hopSize);
                const auto liveComplex = std::polar(mag, angle);
                const auto freezeComplex = std::polar(frozenMagnitude[ch][k], frozenPhase[ch][k]);
                const auto heldSpectrum = liveComplex + (freezeComplex - liveComplex) * freezeAlpha;
                mag = std::abs(heldSpectrum);
                angle = std::atan2(heldSpectrum.imag(), heldSpectrum.real());
                smearMagnitude[ch][k] = follow(mag, smearMagnitude[ch][k], smear);
                mag = mag + (smearMagnitude[ch][k] - mag) * spectralAlpha;
                const float dynamics = current.spectralDynamics;
                const float threshold = fftSize * .015f;
                float targetGain = 1;
                if (std::abs(dynamics) > .00001f && mag > 1.e-8f)
                {
                    if (dynamics > 0 && mag > threshold)
                    {
                        const float compressed =
                            threshold * std::pow(mag / threshold, 1.f / (1 + 7 * dynamics));
                        targetGain = compressed / mag;
                    }
                    else if (dynamics < 0)
                    {
                        // Expansion/gating below threshold, restrained accent
                        // above it. This never creates uncontrolled bin gain.
                        targetGain = mag < threshold
                                         ? std::pow(mag / threshold, -dynamics * 2)
                                         : std::min(4.f, std::pow(mag / threshold, -dynamics * .35f));
                    }
                }
                const float dynamicCoefficient = targetGain < dynamicGain[ch][k] ? .28f : .07f;
                dynamicGain[ch][k] = follow(dynamicGain[ch][k], targetGain, dynamicCoefficient);
                mag *= 1 + (dynamicGain[ch][k] - 1) * spectralAlpha;

                float dest = static_cast<float>(k), ratio = 1;
                float frequency =
                    std::max(nominal * .25f, trueFrequency[ch][k] * static_cast<float>(sr) / tau);
                if (k > 0 && amount > .00001f)
                {
                    const float target = scaleFrequency(std::max(frequency, nominal * .5f));
                    ratio =
                        std::exp2(std::log2(std::max(target, .001f) / std::max(frequency, .001f)) * amount);
                    dest = k * ratio;
                }
                if (k > 0 && std::abs(warp) > .00001f)
                {
                    const float warped = reference * std::pow(dest / reference, 1 + warp * .5f);
                    dest = warped;
                    ratio = dest / k;
                }
                if (dest >= binCount || dest < 0)
                    continue;
                const bool remapped = k > 0 && std::abs(dest - k) > .0001f;
                if (!remapped || frames == 0)
                    remapPhase[ch][k] = angle;
                else
                {
                    const float evolving =
                        trueFrequency[ch][k] * (1 - freezeAlpha) + frozenFrequency[ch][k] * freezeAlpha;
                    remapPhase[ch][k] = wrapped(remapPhase[ch][k] + evolving * ratio * hopSize);
                    angle = remapPhase[ch][k];
                }
                const auto value = std::polar(mag, angle);
                const int a = static_cast<int>(dest);
                const float frac = dest - a;
                // Linear energy deposition avoids nearest-bin frequency steps.
                processed[ch][a] += value * (1 - frac);
                if (a + 1 < binCount)
                    processed[ch][a + 1] += value * frac;
            }
            processed[ch][0] = {processed[ch][0].real(), 0};
            processed[ch][fftSize / 2] = {processed[ch][fftSize / 2].real(), 0};
            for (int k = 1; k < fftSize / 2; ++k)
                processed[ch][fftSize - k] = std::conj(processed[ch][k]);
            fft(processed[ch], true);
            for (int i = 0; i < fftSize; ++i)
            {
                const int at = (oldest + i) & (fftSize - 1);
                // Four sqrt-Hann overlaps sum to 2. Exactly one N-sample
                // delay separates analysis and synthesis absolute timelines.
                ola[ch][at] += finite(processed[ch][i].real()) * window[i] * .5f;
            }
        }
        ++frames;
    }
};

} // namespace experimental
