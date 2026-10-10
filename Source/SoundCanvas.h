// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>

// GUI-only analysis. The audio thread publishes samples through a bounded FIFO.
class SoundCanvas final : public juce::Component
{
  public:
    explicit SoundCanvas(ExperimentalProcessor& p) : processor(p)
    {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        setWantsKeyboardFocus(true);
        setTitle("Sound graph: drag to change sound");
    }
    ~SoundCanvas() override { finishGesture(); }
    std::function<void()> beforeEdit;
    void configure(int m, bool ru, bool monitor = false)
    {
        if (module != m) finishGesture();
        module = m;
        russian = ru;
        monitorOnly = monitor;
        repaint();
    }
    void refresh()
    {
        const int count = processor.readVisualSamples(incoming.data(), (int)incoming.size());
        for (int i = 0; i < count; ++i)
        {
            ring[(size_t)write] = std::isfinite(incoming[(size_t)i]) ? incoming[(size_t)i] : 0.f;
            write = (write + 1) % fftSize;
        }
        if (count > 0)
        {
            idleFrames = 0;
            for (int i = 0; i < fftSize; ++i)
                fftData[(size_t)i] = ring[(size_t)((write + i) % fftSize)] *
                    (.5f - .5f * std::cos(juce::MathConstants<float>::twoPi * i / (fftSize - 1)));
            std::fill(fftData.begin() + fftSize, fftData.end(), 0.f);
            fft.performFrequencyOnlyForwardTransform(fftData.data());
            const double sampleRate = std::max(8000., processor.getSampleRate());
            const double top = std::min(20000., sampleRate * .49);
            for (size_t i = 0; i < spectrum.size(); ++i)
            {
                const double frequency = 20. * std::pow(top / 20., (double)i / (spectrum.size() - 1));
                const float bin = (float)(frequency * fftSize / sampleRate);
                const int k = std::clamp((int)bin, 0, fftSize / 2 - 1);
                const float magnitude = (fftData[(size_t)k] +
                    (fftData[(size_t)k + 1] - fftData[(size_t)k]) * (bin - k)) * 4.f / fftSize;
                const float level = std::clamp((juce::Decibels::gainToDecibels(magnitude, -84.f) + 84.f) / 84.f, 0.f, 1.f);
                spectrum[i] = std::max(level, spectrum[i] * .88f);
            }
        }
        else
        {
            ++idleFrames;
            for (auto& level : spectrum) level *= .88f;
            if (idleFrames > 8) ring.fill(0.f);
        }
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        if (monitorOnly)
        {
            auto top = getLocalBounds().toFloat().withHeight(getHeight() * .62f);
            frame(g, top);
            drawSpectrum(g, top.reduced(10.f, 8.f), true);
            auto bottom = getLocalBounds().toFloat().withTrimmedTop(getHeight() * .66f);
            drawWave(g, bottom.reduced(8.f, 2.f));
            return;
        }
        const auto area = mainPlot();
        const auto side = sidePlot();
        label(g, module == 0 ? tr("Timbre", u8"Тембр") : tr("Sound", u8"Звук"),
              {0, 0, (int)area.getWidth(), 22}, 15.f, ink);
        label(g, module == 0 ? tr("Drag: brightness ↔ · FM ↕ · wheel: spread", u8"Свайп: яркость ↔ · FM ↕ · колесо: разброс")
                            : axisHint(), {0, getHeight() - 24, (int)area.getWidth(), 22}, 13.f, muted);
        frame(g, area);
        if (module == 0) drawHarmonics(g, area.reduced(14.f, 20.f));
        else drawSpectrum(g, area.reduced(14.f, 18.f), false);
        drawNode(g, area);
        label(g, tr("Output spectrum", u8"Спектр выхода"),
              {(int)side.getX(), 0, (int)side.getWidth(), 22}, 15.f, ink);
        frame(g, side);
        drawSpectrum(g, side.reduced(10.f, 8.f), true);
        if (module == 0) drawEnvelope(g);
        else
        {
            auto r = envelopePlot();
            frame(g, r);
            drawWave(g, r.reduced(10.f, 10.f));
            label(g, tr("Output waveform", u8"Форма волны выхода"),
                  {(int)r.getX(), (int)r.getBottom() + 2, (int)r.getWidth(), 20}, 12.f, muted);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        finishGesture();
        if (monitorOnly) return;
        if (e.mods.isPopupMenu()) return;
        origin = e.position;
        last = origin;
        if (mainPlot().contains(origin))
        {
            dragKind = 1;
            auto ids = pairIds();
            first = processor.parameters.getParameter(ids.first);
            second = processor.parameters.getParameter(ids.second);
        }
        else if (module == 0 && envelopePlot().expanded(0.f, 12.f).contains(origin))
        {
            const auto handles = envelopeHandles();
            dragKind = origin.getDistanceFrom(handles.first) <= origin.getDistanceFrom(handles.second) ? 2 : 3;
            first = processor.parameters.getParameter(dragKind == 2 ? "synthAttack" : "synthRelease");
        }
        if (dragKind != 0) grabKeyboardFocus();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!dragKind || !first) return;
        if (!dragging)
        {
            if (origin.getDistanceFrom(e.position) < 3.f) return;
            if (beforeEdit) beforeEdit();
            first->beginChangeGesture();
            if (second) second->beginChangeGesture();
            dragging = true;
        }
        const auto delta = (e.position - last) * (e.mods.isShiftDown() ? .12f : 1.f);
        last = e.position;
        if (dragKind == 1)
        {
            move(*first, delta.x / mainPlot().getWidth());
            if (second) move(*second, -delta.y / mainPlot().getHeight());
        }
        else move(*first, delta.x / (envelopePlot().getWidth() * .38f));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { finishGesture(); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (monitorOnly) return;
        const char* id = wheelId();
        if (!mainPlot().contains(e.position) || id == nullptr) return;
        const float delta = (std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX) *
                            (wheel.isReversed ? -1.f : 1.f) * (e.mods.isShiftDown() ? .025f : .2f);
        if (delta == 0.f) return;
        if (auto* parameter = processor.parameters.getParameter(id))
        {
            if (beforeEdit) beforeEdit();
            parameter->beginChangeGesture();
            move(*parameter, delta);
            parameter->endChangeGesture();
            repaint();
        }
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (monitorOnly) return false;
        const int code = key.getKeyCode();
        if (code != juce::KeyPress::leftKey && code != juce::KeyPress::rightKey &&
            code != juce::KeyPress::upKey && code != juce::KeyPress::downKey) return false;
        const auto ids = pairIds();
        const bool horizontal = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey;
        auto* parameter = processor.parameters.getParameter(horizontal ? ids.first : ids.second);
        if (!parameter) return false;
        if (beforeEdit) beforeEdit();
        parameter->beginChangeGesture();
        move(*parameter, (code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey ? 1.f : -1.f) *
                        (key.getModifiers().isShiftDown() ? .001f : .01f));
        parameter->endChangeGesture();
        repaint();
        return true;
    }

  private:
    ExperimentalProcessor& processor;
    static constexpr int fftSize = 2048;
    juce::dsp::FFT fft{11};
    std::array<float, 8192> incoming{};
    std::array<float, fftSize> ring{};
    std::array<float, fftSize * 2> fftData{};
    std::array<float, 160> spectrum{};
    int write = 0, idleFrames = 0, module = 0, dragKind = 0;
    bool russian = false, dragging = false, monitorOnly = false;
    juce::RangedAudioParameter *first = nullptr, *second = nullptr;
    juce::Point<float> origin, last;
    const juce::Colour ink{0xff202127}, muted{0xff626570}, accent{0xff202127}, grid{0xffe1e4eb};
    juce::String tr(const char* en, const char* ru) const { return juce::String::fromUTF8(russian ? ru : en); }
    std::pair<const char*, const char*> pairIds() const
    {
        static constexpr std::pair<const char*, const char*> ids[]{
            {"synthTone", "synthFM"}, {"grainSize", "grainDensity"}, {"permuteClock", "permuteDepth"},
            {"morphBlend", "morphFocus"}, {"spectralWarp", "spectralSmear"}, {"pitchAmount", "transpose"}};
        return ids[module];
    }
    const char* wheelId() const
    {
        static constexpr const char* ids[]{"synthSpread", "grainPitch", "permuteFeedback", "morphMotion", "spectralDynamics", "transpose"};
        return ids[module];
    }
    juce::String axisHint() const
    {
        static constexpr const char* en[]{"", "Size ↔ · Density ↕ · wheel: pitch", "Clock ↔ · Disorder ↕ · wheel: feedback",
            "Blend ↔ · Focus ↕ · wheel: motion", "Warp ↔ · Smear ↕ · wheel: dynamics", "Amount ↔ · Transpose ↕"};
        static constexpr const char* ru[]{"", u8"Длина ↔ · Плотность ↕ · колесо: высота", u8"Частота ↔ · Искажение ↕ · колесо: возврат",
            u8"Смешение ↔ · Фокус ↕ · колесо: движение", u8"Деформация ↔ · Размытие ↕ · колесо: динамика", u8"Сила ↔ · Сдвиг ↕"};
        return tr(en[module], ru[module]);
    }
    juce::Rectangle<float> mainPlot() const { return {0.f, 28.f, getWidth() * .63f - 20.f, getHeight() - 56.f}; }
    juce::Rectangle<float> sidePlot() const { return {getWidth() * .63f, 28.f, getWidth() * .37f, 64.f}; }
    juce::Rectangle<float> envelopePlot() const { return {getWidth() * .63f, 130.f, getWidth() * .37f, getHeight() - 162.f}; }
    float normalized(const char* id) const { return processor.parameters.getParameter(id)->getValue(); }
    std::pair<juce::Point<float>, juce::Point<float>> envelopeHandles() const
    {
        const auto r = envelopePlot().reduced(10.f, 8.f);
        return {{r.getX() + r.getWidth() * (.02f + .38f * normalized("synthAttack")), r.getY() + 7.f},
                {r.getX() + r.getWidth() * (.58f + .4f * normalized("synthRelease")), r.getBottom() - 7.f}};
    }
    void label(juce::Graphics& g, const juce::String& value, juce::Rectangle<int> r, float size, juce::Colour colour) const
    {
        g.setFont(juce::Font("Segoe UI", size, juce::Font::plain));
        g.setColour(colour);
        g.drawFittedText(value, r, juce::Justification::left, 1);
    }
    void frame(juce::Graphics& g, juce::Rectangle<float> r) const
    {
        g.setColour(juce::Colour(0xfff4f6fb));
        g.fillRoundedRectangle(r, 7.f);
        g.setColour(grid);
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine((int)(r.getY() + r.getHeight() * i / 4.f), r.getX() + 8.f, r.getRight() - 8.f);
    }
    void drawSpectrum(juce::Graphics& g, juce::Rectangle<float> r, bool small) const
    {
        juce::Path curve;
        for (size_t i = 0; i < spectrum.size(); ++i)
        {
            const float x = r.getX() + r.getWidth() * i / (spectrum.size() - 1);
            const float y = r.getBottom() - spectrum[i] * (r.getHeight() - (small ? 14.f : 24.f));
            if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
        }
        auto fill = curve;
        fill.lineTo(r.getRight(), r.getBottom()); fill.lineTo(r.getX(), r.getBottom()); fill.closeSubPath();
        g.setColour(accent.withAlpha(.12f)); g.fillPath(fill);
        g.setColour(accent.withAlpha(.8f)); g.strokePath(curve, juce::PathStrokeType(1.5f));
        label(g, "20 Hz", {(int)r.getX(), (int)r.getBottom() - 14, 55, 14}, 10.f, muted);
        const int top = (int)std::min(20., std::max(8., processor.getSampleRate() / 2000.));
        label(g, juce::String(top) + " kHz", {(int)r.getRight() - 47, (int)r.getBottom() - 14, 47, 14}, 10.f, muted);
    }
    void drawWave(juce::Graphics& g, juce::Rectangle<float> r) const
    {
        juce::Path wave;
        int start = (write + fftSize - 1024) % fftSize;
        for (int i = 0; i < 256; ++i)
        {
            const float value = std::clamp(ring[(size_t)((start + i * 3) % fftSize)], -1.f, 1.f);
            const float x = r.getX() + r.getWidth() * i / 255.f;
            const float y = r.getCentreY() - value * r.getHeight() * .46f;
            if (i == 0) wave.startNewSubPath(x, y); else wave.lineTo(x, y);
        }
        g.setColour(accent.withAlpha(.75f)); g.strokePath(wave, juce::PathStrokeType(1.3f));
    }
    void drawHarmonics(juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto seed = processor.visualTimbre();
        std::array<float, 32> weights{};
        float maximum = 0.f;
        for (size_t i = 0; i < weights.size(); ++i)
        {
            weights[i] = seed[i] * std::pow((float)i + 1.f, (processor.value("synthTone") - .5f) * 2.5f);
            maximum = std::max(maximum, weights[i]);
        }
        auto bars = r.withTrimmedBottom(60.f);
        const float gap = bars.getWidth() / 32.f;
        for (size_t i = 0; i < weights.size(); ++i)
        {
            const float height = maximum > 0.f ? std::sqrt(weights[i] / maximum) * (bars.getHeight() - 16.f) : 0.f;
            const float shift = processor.value("synthSpread") * 2.5f * std::sin((i + 1) * 1.731f);
            g.setColour(ink.withAlpha(.56f));
            g.fillRoundedRectangle(bars.getX() + i * gap + shift, bars.getBottom() - height, gap * .52f, std::max(2.f, height), 2.f);
        }
        label(g, tr("32 harmonics · timbre shape", u8"32 гармоники · форма тембра"),
              {(int)r.getX(), (int)r.getY() - 10, (int)r.getWidth(), 18}, 11.f, muted);
        drawWave(g, {r.getX(), r.getBottom() - 44.f, r.getWidth(), 38.f});
        label(g, tr("Live waveform", u8"Живая форма волны"),
              {(int)r.getX(), (int)r.getBottom() - 3, (int)r.getWidth(), 17}, 11.f, muted);
    }
    void dot(juce::Graphics& g, juce::Point<float> p, float radius) const
    {
        g.setColour(juce::Colours::white); g.fillEllipse(p.x - radius - 2.f, p.y - radius - 2.f, (radius + 2.f) * 2.f, (radius + 2.f) * 2.f);
        g.setColour(accent); g.fillEllipse(p.x - radius, p.y - radius, radius * 2.f, radius * 2.f);
    }
    void drawNode(juce::Graphics& g, juce::Rectangle<float> r) const
    {
        auto inner = r.reduced(12.f);
        const auto ids = pairIds();
        juce::Point<float> pos{inner.getX() + normalized(ids.first) * inner.getWidth(), inner.getBottom() - normalized(ids.second) * inner.getHeight()};
        g.setColour(accent.withAlpha(.16f));
        g.drawLine(pos.x, inner.getY(), pos.x, inner.getBottom());
        g.drawLine(inner.getX(), pos.y, inner.getRight(), pos.y);
        dot(g, pos, 7.f);
        if (hasKeyboardFocus(false)) { g.setColour(accent); g.drawRoundedRectangle(r.reduced(1.f), 7.f, 1.f); }
    }
    void drawEnvelope(juce::Graphics& g) const
    {
        const auto r = envelopePlot();
        label(g, tr("Envelope · drag the points", u8"Огибающая · тяни точки"),
              {(int)r.getX(), 104, (int)r.getWidth(), 22}, 14.f, ink);
        frame(g, r);
        const auto handles = envelopeHandles();
        auto inner = r.reduced(10.f, 8.f);
        juce::Path curve;
        curve.startNewSubPath(inner.getX(), inner.getBottom() - 7.f);
        curve.lineTo(handles.first);
        curve.lineTo(inner.getX() + inner.getWidth() * .52f, handles.first.y);
        const float start = inner.getX() + inner.getWidth() * .52f;
        // The engine release is exponential, unlike the linear attack.
        for (int i = 1; i <= 48; ++i)
        {
            const float t = i / 48.f;
            curve.lineTo(start + (handles.second.x - start) * t,
                         handles.second.y - (handles.second.y - handles.first.y) * std::exp(-6.9f * t));
        }
        g.setColour(accent); g.strokePath(curve, juce::PathStrokeType(2.f));
        dot(g, handles.first, 5.f); dot(g, handles.second, 5.f);
        label(g, tr("Attack ", u8"Атака ") + juce::String(processor.value("synthAttack"), 0) + " ms",
              {(int)r.getX(), (int)r.getBottom() + 3, (int)r.getWidth() / 2, 20}, 12.f, muted);
        label(g, tr("Release ", u8"Затухание ") + juce::String(processor.value("synthRelease"), 0) + " ms",
              {(int)r.getCentreX(), (int)r.getBottom() + 3, (int)r.getWidth() / 2, 20}, 12.f, muted);
    }
    static void move(juce::RangedAudioParameter& parameter, float delta)
    {
        const float value = std::clamp(parameter.getValue() + delta, 0.f, 1.f);
        const auto& range = parameter.getNormalisableRange();
        parameter.setValueNotifyingHost(parameter.convertTo0to1(range.snapToLegalValue(parameter.convertFrom0to1(value))));
    }
    void finishGesture()
    {
        if (dragging)
        {
            if (first) first->endChangeGesture();
            if (second) second->endChangeGesture();
        }
        dragging = false; dragKind = 0; first = second = nullptr;
    }
};
