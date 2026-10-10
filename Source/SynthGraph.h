// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>

class SynthGraph final : public juce::Component
{
  public:
    enum class Kind { oscillator, envelope, lfo };
    SynthGraph(ExperimentalProcessor& p, Kind k) : processor(p), kind(k)
    {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    }
    ~SynthGraph() override { finish(); }
    std::function<void()> beforeEdit;
    void setLanguage(bool ru) { russian = ru; repaint(); }
    void setHarmonics(bool enabled) { finish(); harmonics = enabled; repaint(); }
    void paint(juce::Graphics& g) override
    {
        const auto r = plot();
        g.fillAll(juce::Colours::white);
        g.setColour(rail);
        for (int i = 0; i <= 4; ++i)
        {
            const float x = r.getX() + r.getWidth() * i / 4.f;
            const float y = r.getY() + r.getHeight() * i / 4.f;
            g.drawVerticalLine((int)x, r.getY(), r.getBottom());
            g.drawHorizontalLine((int)y, r.getX(), r.getRight());
        }
        if (kind == Kind::oscillator)
        {
            if (harmonics) drawHarmonics(g, r); else drawOscillator(g, r);
        }
        else if (kind == Kind::envelope) drawEnvelope(g, r);
        else drawLfo(g, r);
        if (hasKeyboardFocus(false))
        {
            g.setColour(guide);
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f), 4.f, 1.f);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        finish();
        if (e.mods.isPopupMenu() || !plot().expanded(10.f).contains(e.position)) return;
        last = origin = e.position;
        pending = true;
        grabKeyboardFocus();
        if (kind == Kind::oscillator && harmonics)
        {
            editableSeed = processor.visualTimbre();
            const float maximum = *std::max_element(editableSeed.begin(), editableSeed.end());
            if (maximum > 0.f) for (auto& value : editableSeed) value /= maximum;
            startEditing();
            editBar(e.position);
            return;
        }
        if (kind == Kind::envelope)
        {
            const auto points = envelopePoints();
            first = processor.parameters.getParameter(e.position.getDistanceFrom(points.first) <=
                e.position.getDistanceFrom(points.second) ? "synthAttack" : "synthRelease");
        }
        else
        {
            first = processor.parameters.getParameter(kind == Kind::oscillator ? "synthTone" : "rate");
            second = processor.parameters.getParameter(kind == Kind::oscillator ? "synthFM" : "motion");
            if (kind == Kind::lfo && processor.value("sync") > .5f) first = nullptr;
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!pending) return;
        if (kind == Kind::oscillator && harmonics)
        {
            const int from = barAt(last.x), to = barAt(e.position.x);
            const int steps = std::max(1, std::abs(to - from));
            for (int i = 1; i <= steps; ++i)
            {
                const float t = (float)i / steps;
                const auto point = last + (e.position - last) * t;
                editableSeed[(size_t)barAt(point.x)] = amplitudeAt(point.y);
            }
            processor.setVisualTimbre(editableSeed);
            last = e.position;
            repaint();
            return;
        }
        if (!editing)
        {
            if (origin.getDistanceFrom(e.position) < 3.f) return;
            startEditing();
        }
        const auto delta = (e.position - last) * (e.mods.isShiftDown() ? .12f : 1.f);
        last = e.position;
        const auto r = plot();
        if (first) move(*first, delta.x / (r.getWidth() * (kind == Kind::envelope ? .4f : 1.f)));
        if (second) move(*second, -delta.y / r.getHeight());
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { finish(); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (e.mods.isAnyMouseButtonDown()) return;
        const char* id = kind == Kind::oscillator ? "synthSpread" : kind == Kind::envelope ? "synthRelease" : "motion";
        if (auto* parameter = processor.parameters.getParameter(id))
        {
            const float amount = (wheel.deltaY != 0.f ? wheel.deltaY : wheel.deltaX) * (e.mods.isShiftDown() ? .02f : .16f);
            if (amount == 0.f) return;
            if (beforeEdit) beforeEdit();
            parameter->beginChangeGesture(); move(*parameter, amount); parameter->endChangeGesture();
            repaint();
        }
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        const int code = key.getKeyCode();
        if (code != juce::KeyPress::leftKey && code != juce::KeyPress::rightKey &&
            code != juce::KeyPress::upKey && code != juce::KeyPress::downKey) return false;
        const bool horizontal = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey;
        const char* id = kind == Kind::oscillator ? (horizontal ? "synthTone" : "synthFM") :
            kind == Kind::envelope ? (horizontal ? "synthAttack" : "synthRelease") : (horizontal ? "rate" : "motion");
        if (kind == Kind::lfo && horizontal && processor.value("sync") > .5f) return true;
        if (beforeEdit) beforeEdit();
        auto* parameter = processor.parameters.getParameter(id);
        parameter->beginChangeGesture();
        move(*parameter, (code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey ? 1.f : -1.f) *
             (key.getModifiers().isShiftDown() ? .001f : .01f));
        parameter->endChangeGesture(); repaint(); return true;
    }

  private:
    ExperimentalProcessor& processor;
    Kind kind;
    bool russian = false, harmonics = false, pending = false, editing = false;
    juce::Point<float> last, origin;
    juce::RangedAudioParameter *first = nullptr, *second = nullptr;
    std::array<float, 32> editableSeed{};
    const juce::Colour ink{0xff202127}, guide{0xff808591}, muted{0xff626570}, rail{0xffedeef2};
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced(12.f, 20.f); }
    juce::String tr(const char* en, const char* ru) const { return juce::String::fromUTF8(russian ? ru : en); }
    void label(juce::Graphics& g, const juce::String& value, juce::Rectangle<int> bounds, float size = 11.f) const
    {
        g.setColour(muted); g.setFont(juce::Font("Segoe UI", size, juce::Font::plain));
        g.drawFittedText(value, bounds, juce::Justification::left, 1);
    }
    void stroke(juce::Graphics& g, const juce::Path& curve, juce::Rectangle<float> r) const
    {
        auto filled = curve;
        filled.lineTo(r.getRight(), r.getBottom()); filled.lineTo(r.getX(), r.getBottom()); filled.closeSubPath();
        g.setColour(ink.withAlpha(.035f)); g.fillPath(filled);
        g.setColour(ink); g.strokePath(curve, juce::PathStrokeType(2.f));
    }
    std::array<float, 32> weightedSeed() const
    {
        auto seed = processor.visualTimbre();
        float sum = 0.f;
        for (size_t i = 0; i < seed.size(); ++i)
        {
            seed[i] *= std::pow((float)i + 1.f, (processor.value("synthTone") - .5f) * 2.5f);
            sum += seed[i];
        }
        if (sum > 0.f) for (auto& value : seed) value /= sum;
        return seed;
    }
    void drawOscillator(juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto weights = weightedSeed();
        std::array<float, 384> waveform{};
        float maximum = .001f;
        const double tau = juce::MathConstants<double>::twoPi;
        for (size_t i = 0; i < waveform.size(); ++i)
        {
            const double phase = (double)i / (waveform.size() - 1);
            const float modulator = (float)std::sin(tau * phase * 2.);
            for (size_t p = 0; p < weights.size(); ++p)
            {
                const double ratio = (p + 1) * std::exp2(processor.value("synthSpread") * .035 * std::sin((p + 1) * 1.731));
                waveform[i] += weights[p] * (float)std::sin(tau * phase * ratio +
                    modulator * processor.value("synthFM") * 2.5f / (p + 1));
            }
            maximum = std::max(maximum, std::abs(waveform[i]));
        }
        juce::Path curve;
        for (size_t i = 0; i < waveform.size(); ++i)
        {
            const float x = r.getX() + r.getWidth() * i / (waveform.size() - 1);
            const float y = r.getCentreY() - waveform[i] / maximum * r.getHeight() * .45f;
            if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
        }
        stroke(g, curve, r);
        label(g, tr("Shape preview", u8"Форма осциллятора"), {12, 1, getWidth() - 24, 17});
        label(g, tr("Brightness ↔   FM ↕   Wheel: spread", u8"Яркость ↔   FM ↕   Колесо: разброс"),
              {12, getHeight() - 17, getWidth() - 24, 16});
    }
    void drawHarmonics(juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto seed = processor.visualTimbre();
        const float maximum = std::max(.00001f, *std::max_element(seed.begin(), seed.end()));
        const float step = r.getWidth() / 32.f;
        for (size_t i = 0; i < seed.size(); ++i)
        {
            const float value = editing ? editableSeed[i] : seed[i] / maximum;
            const float height = std::sqrt(std::clamp(value, 0.f, 1.f)) * r.getHeight();
            g.setColour(ink.withAlpha(i == 0 ? .95f : .58f));
            g.fillRoundedRectangle(r.getX() + i * step + 1.f, r.getBottom() - height, step - 3.f, std::max(1.f, height), 2.f);
        }
        label(g, tr("Draw to shape 32 harmonics", u8"Рисуй амплитуды 32 гармоник"), {12, 1, getWidth() - 24, 17});
        label(g, "1         4               8                              16                                                     32",
              {12, getHeight() - 17, getWidth() - 24, 16});
    }
    std::pair<juce::Point<float>, juce::Point<float>> envelopePoints() const
    {
        const auto r = plot();
        const auto a = processor.parameters.getParameter("synthAttack")->getValue();
        const auto b = processor.parameters.getParameter("synthRelease")->getValue();
        return {{r.getX() + r.getWidth() * (.025f + .37f * a), r.getY()},
                {r.getX() + r.getWidth() * (.59f + .4f * b), r.getBottom()}};
    }
    void drawEnvelope(juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto points = envelopePoints();
        juce::Path curve;
        curve.startNewSubPath(r.getX(), r.getBottom()); curve.lineTo(points.first);
        const float holdX = r.getX() + r.getWidth() * .52f;
        curve.quadraticTo(points.first.x + 12.f, r.getY() + r.getHeight() * .2f, holdX, r.getY() + r.getHeight() * .2f);
        for (int i = 1; i <= 48; ++i)
        {
            const float t = i / 48.f;
            curve.lineTo(holdX + (points.second.x - holdX) * t, r.getBottom() - r.getHeight() * .8f * std::exp(-6.9078f * t));
        }
        stroke(g, curve, r);
        for (auto point : {points.first, points.second})
        {
            g.setColour(juce::Colours::white); g.fillEllipse(point.x - 7.f, point.y - 7.f, 14.f, 14.f);
            g.setColour(ink); g.drawEllipse(point.x - 5.f, point.y - 5.f, 10.f, 10.f, 2.f);
        }
        label(g, tr("Drag attack / release points", u8"Тяни точки атаки / затухания"), {12, 1, getWidth() - 24, 17});
        label(g, tr("Note on", u8"Нота вкл."), {12, getHeight() - 17, 90, 16});
        label(g, tr("Note off", u8"Нота выкл."), {(int)(r.getX() + r.getWidth() * .52f), getHeight() - 17, 110, 16});
    }
    void drawLfo(juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const bool sync = processor.value("sync") > .5f;
        const float depth = processor.value("motion");
        const float rate = sync ? 1.f : processor.value("rate");
        juce::Path curve;
        for (int i = 0; i <= 256; ++i)
        {
            const float t = (float)i / 256;
            const float x = r.getX() + r.getWidth() * t;
            const float y = r.getCentreY() - std::sin(juce::MathConstants<float>::twoPi * rate * 2.f * t) * depth * r.getHeight() * .46f;
            if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
        }
        g.setColour(ink); g.strokePath(curve, juce::PathStrokeType(1.8f));
        label(g, sync ? tr("Tempo sync · cycle preview", u8"Синхронизация · форма цикла") : tr("2 seconds", u8"2 секунды"),
              {12, 1, getWidth() - 24, 17});
        label(g, sync ? tr("Depth ↕", u8"Глубина ↕") : tr("Rate ↔  Depth ↕", u8"Частота ↔  Глубина ↕"),
              {12, getHeight() - 17, getWidth() - 24, 16});
    }
    int barAt(float x) const { return std::clamp((int)((x - plot().getX()) / plot().getWidth() * 32.f), 0, 31); }
    float amplitudeAt(float y) const
    {
        const auto r = plot(); const float a = std::clamp((r.getBottom() - y) / r.getHeight(), 0.f, 1.f);
        return a * a;
    }
    void editBar(juce::Point<float> point)
    {
        editableSeed[(size_t)barAt(point.x)] = amplitudeAt(point.y);
        processor.setVisualTimbre(editableSeed); repaint();
    }
    static void move(juce::RangedAudioParameter& p, float amount)
    {
        const float normalized = std::clamp(p.getValue() + amount, 0.f, 1.f);
        p.setValueNotifyingHost(p.convertTo0to1(p.getNormalisableRange().snapToLegalValue(p.convertFrom0to1(normalized))));
    }
    void startEditing()
    {
        if (beforeEdit) beforeEdit();
        if (first) first->beginChangeGesture();
        if (second) second->beginChangeGesture();
        editing = true;
    }
    void finish()
    {
        if (editing)
        {
            if (first) first->endChangeGesture();
            if (second) second->endChangeGesture();
        }
        pending = editing = false; first = second = nullptr;
    }
};
