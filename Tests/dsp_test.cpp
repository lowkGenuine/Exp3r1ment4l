// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../Source/Engine.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <malloc.h>
#endif

#if defined(_MSC_VER)
#define EXP_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define EXP_NOINLINE __attribute__((noinline))
#else
#define EXP_NOINLINE
#endif

// Count heap calls, including aligned allocations. Enable only while invoking
// the real-time API; test fixtures and prepare are intentionally allowed to allocate.
static std::atomic<bool> observeAllocation{false};
static std::atomic<std::size_t> allocationCount{0};
EXP_NOINLINE void* operator new(std::size_t n)
{
    if (observeAllocation.load(std::memory_order_relaxed))
        ++allocationCount;
    if (void* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n)
{
    return ::operator new(n);
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
EXP_NOINLINE void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
#if defined(__cpp_aligned_new)
void* operator new(std::size_t n, std::align_val_t align)
{
    if (observeAllocation.load(std::memory_order_relaxed))
        ++allocationCount;
    void* p = nullptr;
#if defined(_WIN32)
    p = _aligned_malloc(n ? n : 1, static_cast<std::size_t>(align));
#else
    if (posix_memalign(&p, static_cast<std::size_t>(align), n ? n : 1))
        p = nullptr;
#endif
    if (p)
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t a)
{
    return ::operator new(n, a);
}
void operator delete(void* p, std::align_val_t) noexcept
{
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void* p, std::align_val_t a) noexcept
{
    ::operator delete(p, a);
}
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept
{
    ::operator delete(p, a);
}
void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept
{
    ::operator delete(p, a);
}
#endif

using experimental::Engine;
using experimental::Settings;
using Stereo = std::array<float, 2>;
constexpr double tau = 6.283185307179586476925286766559;
static int checks = 0;
static void require(bool condition, const char* description)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(description);
}
static float signalAt(int i, double sr, int channel = 0)
{
    const double t = i / sr;
    const float noise =
        static_cast<float>(((static_cast<std::uint32_t>(i) * 1664525u + 1013904223u) >> 9) & 1023) / 1023 -
        .5f;
    const float transient = i % 4001 < 24 ? .25f : 0.f;
    return static_cast<float>(.22 * std::sin(tau * (channel ? 443 : 337) * t) +
                              .13 * std::sin(tau * (channel ? 877 : 743) * t)) +
           .018f * noise + transient;
}
static double rms(const std::vector<float>& x, std::size_t start = 0)
{
    double sum = 0;
    for (std::size_t i = start; i < x.size(); ++i)
        sum += x[i] * x[i];
    return std::sqrt(sum / std::max<std::size_t>(1, x.size() - start));
}
static std::vector<float> render(const Settings& settings, int count = 36000)
{
    auto engine = std::make_unique<Engine>();
    engine->prepare(48000);
    engine->setSettings(settings);
    std::vector<float> output(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const float donor = static_cast<float>(.3 * std::sin(tau * 523.25 * i / 48000.0) +
                                               .1 * std::sin(tau * 1046.5 * i / 48000.0));
        output[i] = engine->process(signalAt(i, 48000), signalAt(i, 48000, 1), donor, donor, true)[0];
    }
    return output;
}
static double difference(const std::vector<float>& a, const std::vector<float>& b, std::size_t first = 12000)
{
    double sum = 0;
    for (std::size_t i = first; i < a.size(); ++i)
    {
        const double d = a[i] - b[i];
        sum += d * d;
    }
    return std::sqrt(sum / std::max<std::size_t>(1, a.size() - first));
}

static void testLatencyAndNeutrality()
{
    for (double sr : {8000., 44100., 48000., 96000., 192000.})
    {
        auto engine = std::make_unique<Engine>();
        engine->prepare(sr);
        float peak = 0;
        int peakAt = -1;
        double leakage = 0;
        for (int i = 0; i < 6000; ++i)
        {
            const auto y = engine->process(i == 0 ? 1.f : 0.f, i == 0 ? -1.f : 0.f);
            if (std::abs(y[0]) > peak)
            {
                peak = std::abs(y[0]);
                peakAt = i;
            }
            if (i != Engine::latency)
                leakage += std::abs(y[0]);
            require(std::abs(y[0] + y[1]) < .00001f, "neutral stereo channels must remain independent");
        }
        require(peakAt == Engine::latency,
                "impulse latency must be exactly 2048 samples at every sample rate");
        require(std::abs(peak - 1) < .00002f, "sqrt-Hann WOLA must reconstruct impulse at unity");
        require(leakage < .0001, "neutral processing must not add impulse tails");
        engine->reset();
        std::array<float, Engine::latency> reference{};
        float worst = 0;
        for (int i = 0; i < 16000; ++i)
        {
            const float x =
                signalAt(i, sr) + (i & 1 ? .08f : -.08f); // exercise Nyquist, not only speech-band audio
            const float expected = reference[i % Engine::latency];
            reference[i % Engine::latency] = x;
            const auto y = engine->process(x, x);
            worst = std::max(worst, std::abs(y[0] - expected));
        }
        require(worst < .00008f, "neutral FFT path must preserve broadband audio including Nyquist");
    }
    std::cout << "latency / neutral WOLA / sample rates: passed\n";
}

static void testDryAlignment()
{
    Settings s;
    s.grainsOn = s.permuteOn = s.spectralOn = s.pitchOn = true;
    s.permuteDepth = .9f;
    s.spectralWarp = .6f;
    s.transpose = 7;
    s.mix = 0;
    auto engine = std::make_unique<Engine>();
    engine->setSettings(s);
    engine->prepare(48000);
    std::array<float, Engine::latency> reference{};
    float worst = 0;
    for (int i = 0; i < 18000; ++i)
    {
        const float x = signalAt(i, 48000);
        const float delayed = reference[i % Engine::latency];
        reference[i % Engine::latency] = x;
        const auto y = engine->process(x, x);
        worst = std::max(worst, std::abs(y[0] - delayed));
    }
    require(worst < .00001f, "dry Mix must remain aligned with 2048-sample wet latency");
    s.mix = 1;
    s.bypass = true;
    s.outputDb = 12;
    engine->setSettings(s);
    engine->prepare(48000);
    reference.fill(0);
    worst = 0;
    for (int i = 0; i < 18000; ++i)
    {
        const float x = signalAt(i, 48000);
        const float delayed = reference[i % Engine::latency];
        reference[i % Engine::latency] = x;
        const auto y = engine->process(x, x);
        worst = std::max(worst, std::abs(y[0] - delayed));
    }
    require(worst < .00001f, "global Bypass must bypass FX and output gain while retaining host latency");
    std::cout << "dry / bypass alignment: passed\n";
}

static void testAlgorithmsAreActive()
{
    Settings s;
    const auto neutral = render(s);
    std::vector<std::vector<float>> modes;
    s.morphOn = true;
    s.morphBlend = .85f;
    s.morphMotion = .5f;
    modes.push_back(render(s));
    s = Settings{};
    s.spectralOn = true;
    s.spectralWarp = .45f;
    s.spectralSmear = .7f;
    s.spectralDynamics = .8f;
    modes.push_back(render(s));
    s = Settings{};
    s.pitchOn = true;
    s.scale = 1;
    s.transpose = 5;
    modes.push_back(render(s));
    s = Settings{};
    s.grainsOn = true;
    s.grainPitch = 7;
    s.grainSize = 110;
    s.grainDensity = 24;
    s.grainPosition = .2f;
    modes.push_back(render(s));
    for (int op = 0; op < 4; ++op)
    {
        s = Settings{};
        s.permuteOn = true;
        s.permuteOperator = op;
        s.permuteClock = 2700;
        s.permuteDepth = .8f;
        s.permuteFeedback = .4f;
        modes.push_back(render(s));
    }
    for (std::size_t i = 0; i < modes.size(); ++i)
    {
        require(rms(modes[i], 12000) > .001, "each effect must produce non-silent audio");
        require(difference(neutral, modes[i]) > .015, "each effect must actually transform its input");
        for (std::size_t j = 0; j < i; ++j)
            require(difference(modes[i], modes[j]) > .01,
                    "different algorithms must not be aliases of one placeholder");
    }
    auto engine = std::make_unique<Engine>();
    engine->prepare(48000);
    s = Settings{};
    s.morphOn = true;
    s.morphBlend = 1;
    engine->setSettings(s);
    std::array<float, Engine::latency> reference{};
    float worst = 0;
    for (int i = 0; i < 16000; ++i)
    {
        const float x = signalAt(i, 48000), expected = reference[i % Engine::latency];
        reference[i % Engine::latency] = x;
        worst = std::max(worst, std::abs(engine->process(x, x, 0, 0, false)[0] - expected));
    }
    require(worst < .00008f, "Morph without a donor must preserve the main signal");
    std::cout << "morph / spectral / pitch / grains / four permute operators: passed\n";
}

static void testFreezeContinuity()
{
    auto engine = std::make_unique<Engine>();
    engine->prepare(48000);
    Settings s;
    s.spectralOn = true;
    engine->setSettings(s);
    for (int i = 0; i < 14000; ++i)
    {
        const float x = static_cast<float>(.3 * std::sin(tau * 337.0 * i / 48000));
        engine->process(x, x);
    }
    s.freeze = true;
    engine->setSettings(s);
    // Keep live audio present while the captured spectrum is blended in.
    for (int i = 14000; i < 18000; ++i)
    {
        const float x = static_cast<float>(.3 * std::sin(tau * 337.0 * i / 48000));
        engine->process(x, x);
    }
    double energy = 0;
    float previous = 0, maxJump = 0;
    for (int i = 0; i < 18000; ++i)
    {
        const float x = engine->process(0, 0)[0];
        if (i > 6000)
        {
            energy += x * x;
            maxJump = std::max(maxJump, std::abs(x - previous));
        }
        previous = x;
    }
    require(std::sqrt(energy / 12000) > .03, "spectral freeze must retain sound after live input stops");
    require(maxJump < .15f, "frozen phases must evolve continuously rather than restart every FFT hop");
    s.freeze = false;
    engine->setSettings(s);
    double tail = 0;
    for (int i = 0; i < 24000; ++i)
    {
        const float x = engine->process(0, 0)[0];
        if (i > 21000)
            tail += x * x;
    }
    require(tail < .000001, "unfreezing silence must eventually release captured sound");
    std::cout << "freeze capture / continuous phase / release: passed\n";
}

static void testSynthAndSeeds()
{
    auto engine = std::make_unique<Engine>();
    engine->prepare(48000);
    Settings s;
    s.synthOn = true;
    s.synthSpread = 0;
    s.synthFM = 0;
    s.synthLevel = -6;
    s.synthAttack = 1;
    s.synthRelease = 50;
    engine->setSettings(s);
    std::array<float, 32> sine{};
    sine[0] = 1;
    engine->setTimbre(sine);
    engine->noteOn(69, 1);
    auto measure = [&](int settle, int count)
    {
        for (int i = 0; i < settle; ++i)
            engine->process(0, 0);
        int crossings = 0;
        float previous = 0;
        double energy = 0;
        for (int i = 0; i < count; ++i)
        {
            const float x = engine->process(0, 0)[0];
            energy += x * x;
            if (previous <= 0 && x > 0)
                ++crossings;
            previous = x;
        }
        require(energy / count > .003, "MIDI note must produce audible synth output");
        return crossings * 48000.0 / count;
    };
    require(std::abs(measure(12000, 24000) - 440) < 4, "A4 additive voice must track MIDI pitch");
    engine->setPitchBend(12);
    require(std::abs(measure(12000, 24000) - 880) < 4, "pitch bend must transpose the synth by semitones");
    engine->setSustain(true);
    engine->noteOff(69);
    for (int i = 0; i < 12000; ++i)
        engine->process(0, 0);
    require(engine->activeVoiceCount() == 1, "sustain pedal must retain a released note");
    engine->setSustain(false);
    for (int i = 0; i < 14000; ++i)
        engine->process(0, 0);
    require(engine->activeVoiceCount() == 0, "pedal release must finish note envelopes");
    for (int note = 24; note < 60; ++note)
        engine->noteOn(note, .75f);
    require(engine->activeVoiceCount() == 8, "voice stealing must remain bounded to eight voices");
    engine->allNotesOff();
    for (int i = 0; i < 14000; ++i)
        engine->process(0, 0);
    require(engine->activeVoiceCount() == 0,
            "all notes off must release every voice including stolen voices");

    auto capture = std::make_unique<Engine>();
    capture->prepare(48000);
    for (int i = 0; i < 24000; ++i)
    {
        const float x = static_cast<float>(.3 * std::sin(tau * 375 * i / 48000.0) +
                                           .15 * std::sin(tau * 750 * i / 48000.0));
        capture->process(x, x);
    }
    capture->captureTimbre();
    const auto captured = capture->timbre();
    require(captured[0] > .5f && captured[1] > .15f && captured[9] < .01f,
            "timbre capture must extract harmonic amplitudes from actual input");
    auto clone = std::make_unique<Engine>();
    clone->setTimbre(captured);
    capture->mutateTimbre(92871);
    clone->mutateTimbre(92871);
    require(capture->timbre() == clone->timbre(),
            "seed mutation must be deterministic and state reproducible");
    require(capture->timbre() != captured, "seed mutation must change the stored sound seed");
    float total = 0;
    for (float value : capture->timbre())
    {
        require(std::isfinite(value) && value >= 0, "seed values must stay finite");
        total += value;
    }
    require(std::abs(total - 1) < .00001f, "stored seed must remain normalized");
    std::cout << "synth pitch / bend / sustain / notes / capture / deterministic mutation: passed\n";
}

static void testPitchAccuracyAndBandLimit()
{
    auto engine = std::make_unique<Engine>();
    engine->prepare(48000);
    Settings s;
    s.pitchOn = true;
    s.scale = 7;
    s.transpose = 12;
    engine->setSettings(s);
    int crossings = 0;
    float previous = 0;
    for (int i = 0; i < 50000; ++i)
    {
        const float x = static_cast<float>(.3 * std::sin(tau * 375 * i / 48000.0));
        const float y = engine->process(x, x)[0];
        if (i >= 26000 && previous <= 0 && y > 0)
            ++crossings;
        previous = y;
    }
    require(std::abs(crossings * 48000.0 / 24000 - 750) < 4,
            "phase-vocoder transpose must move a stable 375 Hz tone to 750 Hz");

    constexpr double sampleRate = 22050;
    engine->prepare(sampleRate);
    s = Settings{};
    s.synthOn = true;
    s.synthSpread = 0;
    s.synthFM = 1;
    engine->setSettings(s);
    engine->noteOn(108, 1);
    for (int i = 0; i < 16000; ++i)
        engine->process(0, 0);
    const double fundamental = 440 * std::exp2((108 - 69) / 12.0);
    const double aliasedThird = std::abs(sampleRate - 3 * fundamental);
    double realFund = 0, imagFund = 0, realAlias = 0, imagAlias = 0;
    constexpr int count = 22050;
    for (int i = 0; i < count; ++i)
    {
        const double sample = engine->process(0, 0)[0] * (.5 - .5 * std::cos(tau * i / count));
        realFund += sample * std::cos(tau * fundamental * i / sampleRate);
        imagFund += sample * std::sin(tau * fundamental * i / sampleRate);
        realAlias += sample * std::cos(tau * aliasedThird * i / sampleRate);
        imagAlias += sample * std::sin(tau * aliasedThird * i / sampleRate);
    }
    const double wanted = std::hypot(realFund, imagFund);
    const double alias = std::hypot(realAlias, imagAlias);
    require(wanted > 1, "high MIDI notes must retain their valid fundamental");
    require(alias < wanted * .001,
            "additive/FM carriers above Nyquist must be omitted, not folded into audible aliases");
    std::cout << "phase-vocoder octave / additive Nyquist rejection: passed\n";
}

static void testSilenceAutomationAndRealtime()
{
    auto engine = std::make_unique<Engine>();
    engine->prepare(48000);
    Settings s;
    s.grainsOn = s.permuteOn = s.morphOn = s.spectralOn = s.pitchOn = true;
    s.permuteDepth = 1;
    s.permuteFeedback = .94f;
    s.spectralDynamics = -1;
    s.spectralWarp = 1;
    s.grainDensity = 80;
    s.grainPitch = 36;
    s.spectralSmear = 1;
    engine->setSettings(s);
    float peak = 0;
    for (int i = 0; i < 16000; ++i)
    {
        const auto y = engine->process(0, 0, 0, 0, true);
        peak = std::max({peak, std::abs(y[0]), std::abs(y[1])});
    }
    // Bare XOR would generate DC from digital zero. The guarded operator must
    // not excite its feedback loop without an actual source signal.
    require(peak < .00001f, "an enabled chain with silent input must remain silent");

    for (double sr : {8000., 44100., 48000., 96000.})
    {
        engine->prepare(sr);
        s.synthOn = true;
        engine->setSettings(s);
        for (int n = 24; n < 32; ++n)
            engine->noteOn(n, .9f);
        float maximum = 0;
        allocationCount = 0;
        observeAllocation = true;
        for (int i = 0; i < 26000; ++i)
        {
            if (i % 997 == 0)
            {
                engine->setSustain((i / 997) % 2 == 0);
                engine->noteOff(24 + (i / 997) % 8);
                engine->noteOn(36 + (i / 997) % 24, .8f);
            }
            if (i == 12000)
            {
                engine->allNotesOff();
                engine->noteOn(48, .9f);
            }
            if (i % 113 == 0)
            {
                const bool alternate = (i / 113) % 2 == 0;
                s.permuteOperator = (i / 113) % 4;
                s.scale = (i / 113) % 8;
                s.spectralWarp = alternate ? -1.f : 1.f;
                s.spectralDynamics = alternate ? -1.f : 1.f;
                s.freeze = (i / 113) % 5 == 0;
                s.grainFreeze = (i / 113) % 7 == 0;
                s.grainPitch = alternate ? -36.f : 36.f;
                s.permuteClock = alternate ? 30.f : 192000.f;
                s.synthFM = alternate ? 0.f : 1.f;
                s.synthSpread = alternate ? 0.f : 1.f;
                s.modTarget = (i / 113) % 7;
                s.motion = 1;
                s.rate = 40;
                s.transpose = alternate ? -48.f : 48.f;
                s.bypass = (i / 113) % 13 == 0;
                engine->setSettings(s);
                engine->setPitchBend(alternate ? -24.f : 24.f);
                if (i % 226 == 0)
                    engine->mutateTimbre(static_cast<std::uint32_t>(i + 1));
                if (i % 339 == 0)
                    engine->captureTimbre();
            }
            const float x = signalAt(i, sr);
            const float donor = static_cast<float>(.2 * std::sin(tau * 619 * i / sr));
            const auto y = engine->process(x, -x, donor, -donor, i % 3072 < 2048);
            if (!std::isfinite(y[0]) || !std::isfinite(y[1]))
            {
                observeAllocation = false;
                require(false, "automation must not produce NaN/Inf");
            }
            maximum = std::max({maximum, std::abs(y[0]), std::abs(y[1])});
        }
        observeAllocation = false;
        require(allocationCount == 0,
                "processing/settings/MIDI/capture/mutation must not allocate on audio thread");
        require(maximum < 12, "worst-range feedback/automation must remain bounded for normal audio input");
    }
    // Deliberately malformed host values are normalized at the API boundary.
    s = Settings{};
    s.mix = std::numeric_limits<float>::quiet_NaN();
    s.outputDb = std::numeric_limits<float>::infinity();
    s.synthTone = -10000;
    s.permuteClock = -1;
    s.grainSize = std::numeric_limits<float>::quiet_NaN();
    engine->setSettings(s);
    for (int i = 0; i < 6000; ++i)
    {
        const auto y = engine->process(i == 0 ? std::numeric_limits<float>::quiet_NaN() : 0,
                                       std::numeric_limits<float>::infinity());
        require(std::isfinite(y[0]) && std::isfinite(y[1]),
                "invalid inputs and parameters must be contained");
    }
    std::cout << "silence / extreme automation / all rates / zero real-time allocations: passed\n";
}

int main()
{
    try
    {
        testLatencyAndNeutrality();
        testDryAlignment();
        testAlgorithmsAreActive();
        testFreezeContinuity();
        testSynthAndSeeds();
        testPitchAccuracyAndBandLimit();
        testSilenceAutomationAndRealtime();
        std::cout << "PASS: " << checks << " meaningful checks" << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        observeAllocation = false;
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
