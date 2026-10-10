// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

namespace
{
const juce::Colour bg{0xfff8f8fa}, paper{0xffffffff}, line{0xffdadce2}, ink{0xff202127}, muted{0xff626570},
    rail{0xffedeef2};
juce::Font font(float size)
{
    return juce::Font("Segoe UI", size, juce::Font::plain);
}
void text(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, float size = 14,
          juce::Colour c = muted, int align = juce::Justification::left)
{
    g.setFont(font(size));
    g.setColour(c);
    g.drawFittedText(s, r, align, 1);
}
void identify(juce::Component& c, const juce::String& id, const juce::String& title)
{
    c.setComponentID(id);
    c.setTitle(title);
    c.setWantsKeyboardFocus(true);
}
class ScrubLabel final : public juce::Label
{
  public:
    juce::Slider& slider;
    explicit ScrubLabel(juce::Slider& s) : slider(s)
    {
        setEditable(false, true, false);
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        setJustificationType(juce::Justification::left);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        slider.mouseDown(e.getEventRelativeTo(&slider));
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        slider.mouseDrag(e.getEventRelativeTo(&slider));
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        slider.mouseUp(e.getEventRelativeTo(&slider));
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (slider.isEnabled())
            slider.mouseDoubleClick(e.getEventRelativeTo(&slider));
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        slider.mouseWheelMove(e.getEventRelativeTo(&slider), wheel);
    }
};
} // namespace
ScrubField::ScrubField()
{
    setSliderStyle(juce::Slider::LinearHorizontal);
    setTextBoxStyle(juce::Slider::TextBoxAbove, false, 150, 30);
    setScrollWheelEnabled(false);
    setNumDecimalPlacesToDisplay(2);
}
void ScrubField::mouseDown(const juce::MouseEvent& e)
{
    if (!isEnabled() || e.mods.isPopupMenu())
        return;
    if (beforeEdit)
        beforeEdit();
    gesture.reset();
    hideTextBox(false);
    grabKeyboardFocus();
    origin = last = e.position;
    pending = true;
}
void ScrubField::mouseDrag(const juce::MouseEvent& e)
{
    if (!isEnabled() || !pending)
        return;
    if (!gesture)
    {
        auto d = e.position - origin;
        if (d.getDistanceFromOrigin() < 3)
            return;
        horizontal = std::abs(d.x) >= std::abs(d.y);
        gesture = std::make_unique<juce::Slider::ScopedDragNotification>(*this);
    }
    auto d = e.position - last;
    last = e.position;
    const double step = (horizontal ? d.x : -d.y) * (e.mods.isShiftDown() ? .12 : 1.) / 240.;
    setValue(proportionOfLengthToValue(std::clamp(valueToProportionOfLength(getValue()) + step, 0., 1.)),
             juce::sendNotificationSync);
}
void ScrubField::mouseUp(const juce::MouseEvent&)
{
    pending = false;
    gesture.reset();
}
void ScrubField::mouseDoubleClick(const juce::MouseEvent&)
{
    if (!isEnabled())
        return;
    if (beforeEdit)
        beforeEdit();
    pending = false;
    gesture.reset();
    showTextBox();
}
bool ScrubField::keyPressed(const juce::KeyPress& k)
{
    if (!isEnabled())
        return false;
    if (beforeEdit)
        beforeEdit();
    const int c = k.getKeyCode();
    if (c == juce::KeyPress::returnKey)
    {
        showTextBox();
        return true;
    }
    if (c == juce::KeyPress::leftKey || c == juce::KeyPress::rightKey || c == juce::KeyPress::upKey ||
        c == juce::KeyPress::downKey)
    {
        juce::Slider::ScopedDragNotification n(*this);
        setValue(proportionOfLengthToValue(
                     std::clamp(valueToProportionOfLength(getValue()) +
                                    (c == juce::KeyPress::rightKey || c == juce::KeyPress::upKey ? 1 : -1) *
                                        (k.getModifiers().isShiftDown() ? .001 : .01),
                                0., 1.)),
                 juce::sendNotificationSync);
        return true;
    }
    return juce::Slider::keyPressed(k);
}
void ScrubField::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (!isEnabled()) return;
    const double delta = (std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX) *
                         (wheel.isReversed ? -1. : 1.) * (e.mods.isShiftDown() ? .025 : .2);
    if (delta == 0.) return;
    if (beforeEdit) beforeEdit();
    juce::Slider::ScopedDragNotification n(*this);
    setValue(proportionOfLengthToValue(std::clamp(valueToProportionOfLength(getValue()) + delta, 0., 1.)),
             juce::sendNotificationSync);
}

ExperimentalSkin::ExperimentalSkin()
{
    setDefaultSansSerifTypefaceName("Segoe UI");
    for (int id : std::array<int, 3>{juce::ComboBox::backgroundColourId, juce::PopupMenu::backgroundColourId,
                                     juce::TextEditor::backgroundColourId})
        setColour(id, paper);
    setColour(juce::ComboBox::textColourId, ink);
    setColour(juce::PopupMenu::textColourId, ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, rail);
    setColour(juce::PopupMenu::highlightedTextColourId, ink);
    setColour(juce::TextEditor::textColourId, ink);
    setColour(juce::TextEditor::focusedOutlineColourId, ink);
    setColour(juce::TextEditor::highlightColourId, line);
    setColour(juce::Slider::textBoxTextColourId, ink);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TooltipWindow::backgroundColourId, paper);
    setColour(juce::TooltipWindow::textColourId, ink);
    setColour(juce::TooltipWindow::outlineColourId, line);
}
void ExperimentalSkin::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                            bool hover, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(1);
    const auto id = b.getComponentID();
    if (id.startsWith("power"))
    {
        const auto dot = r.withSizeKeepingCentre(16, 16);
        g.setColour(b.getToggleState() ? ink : paper);
        g.fillRoundedRectangle(dot, 4);
        g.setColour(b.getToggleState() ? ink : muted);
        g.drawRoundedRectangle(dot, 4, .8f);
        if (b.getToggleState())
        {
            g.setColour(paper);
            g.drawLine(dot.getX() + 4, dot.getCentreY(), dot.getX() + 7, dot.getBottom() - 4, 1.3f);
            g.drawLine(dot.getX() + 7, dot.getBottom() - 4, dot.getRight() - 3, dot.getY() + 4, 1.3f);
        }
    }
    else
    {
        if (hover || down)
        {
            g.setColour(rail);
            g.fillRoundedRectangle(r, 6);
        }
        if (id.startsWith("select"))
        {
            if (b.getToggleState())
            {
                g.setColour(ink);
                g.fillRect(r.getX() + 6, r.getBottom() - 2, r.getWidth() - 12, 2.f);
            }
        }
        else if (b.getToggleState())
        {
            g.setColour(ink);
            g.fillRoundedRectangle(r, 6);
        }
        else if (id == "bypass" || id == "freeze")
        {
            g.setColour(line);
            g.drawRoundedRectangle(r, 6, .8f);
        }
    }
    if (b.hasKeyboardFocus(true))
    {
        g.setColour(ink);
        g.drawRoundedRectangle(r.reduced(1), 6, 1.3f);
    }
}
void ExperimentalSkin::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    if (b.getComponentID().startsWith("power"))
        return;
    const bool tab = b.getComponentID().startsWith("select");
    text(g, b.getButtonText(), b.getLocalBounds().reduced(4), tab ? 16 : 14,
         b.getToggleState() && !tab ? paper : ink, juce::Justification::centred);
}
void ExperimentalSkin::drawLinearSlider(juce::Graphics& g, int, int, int, int, float, float, float,
                                        juce::Slider::SliderStyle, juce::Slider& s)
{
    auto r = s.getLocalBounds().toFloat();
    const float y = r.getBottom() - 12.f;
    g.setColour(line);
    g.fillRoundedRectangle(8.f, y - 3.f, r.getWidth() - 16.f, 6.f, 3.f);
    const float x = 8.f + (r.getWidth() - 16.f) * (float)s.valueToProportionOfLength(s.getValue());
    g.setColour(juce::Colour(0xff202127).withAlpha(s.isEnabled() ? .9f : .3f));
    g.fillRoundedRectangle(8.f, y - 3.f, std::max(1.f, x - 8.f), 6.f, 3.f);
    g.fillEllipse(x - 5.f, y - 5.f, 10.f, 10.f);
}
juce::Font ExperimentalSkin::getLabelFont(juce::Label& l)
{
    return dynamic_cast<juce::Slider*>(l.getParentComponent()) ? font(22) : l.getFont();
}
juce::Label* ExperimentalSkin::createSliderTextBox(juce::Slider& s)
{
    return new ScrubLabel(s);
}
void ExperimentalSkin::drawLabel(juce::Graphics& g, juce::Label& l)
{
    if (dynamic_cast<juce::Slider*>(l.getParentComponent()))
    {
        if (l.isMouseOverOrDragging())
        {
            g.setColour(rail);
            g.fillRoundedRectangle(l.getLocalBounds().toFloat().reduced(1), 6);
        }
        if (!l.isBeingEdited())
            text(g, l.getText(), l.getLocalBounds().reduced(5), 22, ink);
    }
    else
        juce::LookAndFeel_V4::drawLabel(g, l);
}
juce::Font ExperimentalSkin::getComboBoxFont(juce::ComboBox&)
{
    return font(14);
}
juce::Font ExperimentalSkin::getPopupMenuFont()
{
    return font(15);
}
void ExperimentalSkin::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int,
                                    juce::ComboBox& b)
{
    if (b.isMouseOverOrDragging())
    {
        g.setColour(rail);
        g.fillRoundedRectangle(b.getLocalBounds().toFloat(), 6);
    }
    juce::Path arrow;
    arrow.startNewSubPath(w - 16.f, h * .5f - 2);
    arrow.lineTo(w - 12.f, h * .5f + 2);
    arrow.lineTo(w - 8.f, h * .5f - 2);
    g.setColour(muted);
    g.strokePath(arrow, juce::PathStrokeType(1.3f));
    if (b.hasKeyboardFocus(true))
    {
        g.setColour(ink);
        g.drawRoundedRectangle(b.getLocalBounds().toFloat().reduced(1), 6, 1);
    }
}
void ExperimentalSkin::positionComboBoxText(juce::ComboBox& b, juce::Label& l)
{
    l.setBounds(4, 0, b.getWidth() - 24, b.getHeight());
    l.setFont(font(14));
    l.setBorderSize(juce::BorderSize<int>(0));
}

juce::String ExperimentalEditor::tr(const char* en, const char* ru) const
{
    return juce::String::fromUTF8(russian ? ru : en);
}
void ExperimentalEditor::setupField(ScrubField& field, const char* id,
                                    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& a)
{
    a.reset();
    const auto* p = processor.parameters.getParameter(id);
    identify(field, id, p->getName(64));
    field.setTooltip(tr("Swipe the number or rail, or use the wheel. Shift: fine. Double-click: type.",
                        u8"Тяни число или полоску, либо крути колесо. Shift — точно. Двойной клик — ввод."));
    a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, id,
                                                                               field);
    field.setTextValueSuffix("");
    field.textFromValueFunction = nullptr;
    field.valueFromTextFunction = nullptr;
    for (const auto& spec : exp3::continuous)
        if (juce::String(id) == spec.id)
        {
            field.setTextValueSuffix(spec.unit);
            field.setNumDecimalPlacesToDisplay(spec.step >= 1 ? 0 : spec.step >= .1f ? 1 : 2);
            if (spec.max == 1)
            {
                field.textFromValueFunction = [](double v) { return juce::String(v * 100., 1) + " %"; };
                field.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue() * .01; };
            }
        }
    field.updateText();
}
ExperimentalEditor::ExperimentalEditor(ExperimentalProcessor& p)
    : AudioProcessorEditor(p), processor(p), play(p), canvas(p),
      oscillator(p, SynthGraph::Kind::oscillator), envelope(p, SynthGraph::Kind::envelope),
      lfo(p, SynthGraph::Kind::lfo),
      piano(p.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel(&skin);
    addAndMakeVisible(canvas);
    canvas.beforeEdit = [this] { remember(); };
    for (auto* graph : {&oscillator, &envelope, &lfo})
    {
        addAndMakeVisible(*graph);
        graph->beforeEdit = [this] { remember(); };
    }
    addAndMakeVisible(waveSource);
    waveSource.beforeEdit = [this] { remember(); };
    waveSource.onChange = [this]
    {
        const int shape = waveSource.getSelectedId();
        if (shape == 0) return;
        remember();
        std::array<float, 32> seed{};
        for (size_t i = 0; i < seed.size(); ++i)
        {
            const float harmonic = (float)i + 1.f;
            if (shape == 1) seed[i] = i == 0 ? 1.f : 0.f;
            if (shape == 2) seed[i] = 1.f / harmonic;
            if (shape == 3) seed[i] = i % 2 == 0 ? 1.f / harmonic : 0.f;
            if (shape == 4) seed[i] = i % 2 == 0 ? 1.f / (harmonic * harmonic) : 0.f;
        }
        processor.setVisualTimbre(seed);
    };
    for (auto* button : {&waveView, &harmonicView}) addAndMakeVisible(*button);
    waveView.setToggleState(true, juce::dontSendNotification);
    waveView.onClick = [this]
    {
        waveView.setToggleState(true, juce::dontSendNotification);
        harmonicView.setToggleState(false, juce::dontSendNotification);
        oscillator.setHarmonics(false);
    };
    harmonicView.onClick = [this]
    {
        waveView.setToggleState(false, juce::dontSendNotification);
        harmonicView.setToggleState(true, juce::dontSendNotification);
        oscillator.setHarmonics(true);
    };
    addAndMakeVisible(piano);
    piano.setAvailableRange(48, 83);
    piano.setLowestVisibleKey(48);
    piano.setOctaveForMiddleC(4);
    piano.setKeyWidth(40.f);
    piano.setScrollButtonsVisible(false);
    piano.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, paper);
    piano.setColour(juce::MidiKeyboardComponent::blackNoteColourId, ink);
    piano.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colour(0xff202127).withAlpha(.6f));
    piano.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colour(0xff202127).withAlpha(.2f));
    russian = p.parameters.state.getProperty("language", "en").toString() == "ru";
    for (int i = 0; i < 6; ++i)
    {
        if (!JucePlugin_IsSynth && i == 0) continue;
        addAndMakeVisible(select[(size_t)i]);
        addAndMakeVisible(power[(size_t)i]);
        select[(size_t)i].setButtonText(exp3::moduleNames[i]);
        identify(select[(size_t)i], "select" + juce::String(i), "Edit " + juce::String(exp3::moduleNames[i]));
        identify(power[(size_t)i], "power" + juce::String(i), exp3::enableNames[i]);
        power[(size_t)i].setClickingTogglesState(true);
        powerAttachments[(size_t)i] = std::make_unique<QuietButtonAttachment>(
            *p.parameters.getParameter(exp3::enableIds[i]), power[(size_t)i]);
        select[(size_t)i].onClick = [this, i] { chooseModule(i); };
        power[(size_t)i].onClick = [this, i]
        {
            if (power[(size_t)i].getToggleState())
                chooseModule(i);
        };
    }
    for (int i = 0; i < 4; ++i)
    {
        addAndMakeVisible(fields[(size_t)i]);
        addAndMakeVisible(captions[(size_t)i]);
        captions[(size_t)i].setFont(font(14));
        captions[(size_t)i].setColour(juce::Label::textColourId, muted);
        captions[(size_t)i].setBorderSize(juce::BorderSize<int>(0));
    }
    for (auto* f : {&mix, &output, &depth, &rate, &attack, &fm})
        addAndMakeVisible(*f);
    setupField(mix, "mix", mixAttachment);
    setupField(output, "outputDb", outputAttachment);
    setupField(depth, "motion", depthAttachment);
    setupField(rate, "rate", rateAttachment);
    if (JucePlugin_IsSynth)
    {
        setupField(attack, "synthAttack", attackAttachment);
        setupField(fm, "synthFM", fmAttachment);
    }
    for (auto* b : std::array<juce::TextButton*, 9>{&bypass, &freeze, &more, &mutate, &capture, &load, &undo,
                                                    &language, &sync})
        addAndMakeVisible(*b);
    identify(bypass, "bypass", "Bypass");
    identify(freeze, "freeze", "Freeze");
    identify(more, "more", "More parameters");
    identify(mutate, "mutate", "Mutate selected module");
    identify(capture, "capture", "Capture timbre");
    identify(load, "load", "Load audio file");
    identify(undo, "undo", "Undo");
    identify(language, "language", "Language");
    identify(sync, "sync", "Tempo sync");
    bypass.setClickingTogglesState(true);
    freeze.setClickingTogglesState(true);
    more.setClickingTogglesState(true);
    sync.setClickingTogglesState(true);
    bypassAttachment = std::make_unique<QuietButtonAttachment>(*p.parameters.getParameter("bypass"), bypass);
    syncAttachment = std::make_unique<QuietButtonAttachment>(*p.parameters.getParameter("sync"), sync);
    more.onClick = [this]
    {
        expanded = more.getToggleState();
        setSize(selected == 0 ? 1120 : 960, selected == 0 ? 760 : expanded ? 840 : 700);
        configure();
    };
    mutate.onClick = [this] { processor.mutateSelected(selected); };
    capture.onClick = [this] { processor.capture(); };
    load.onClick = [this] { openFile(); };
    undo.onClick = [this] { undoAction(); };
    language.onClick = [this]
    {
        russian = !russian;
        processor.parameters.state.setProperty("language", russian ? "ru" : "en", nullptr);
        updateLanguage();
    };
    addAndMakeVisible(play);
    identify(play, "play", "Hold to audition C4");
    for (auto* c : std::array<ActionChoice*, 4>{&preset, &choice, &root, &modTarget})
        addAndMakeVisible(*c);
    identify(preset, "presets", "Factory presets");
    identify(choice, "choice", "Algorithm");
    identify(root, "root", "Root note");
    identify(modTarget, "modTarget", "Modulation target");
    preset.addItemList({"Init", "Cloud", "Corrupt", "Glass", JucePlugin_IsSynth ? "MIDI Drift" : "Grain Drift", "Grid"}, 1);
    preset.setTextWhenNothingSelected("Presets");
    preset.onChange = [this]
    {
        if (preset.getSelectedId() > 0)
        {
            processor.preset(preset.getSelectedId() - 1);
            preset.setSelectedId(0, juce::dontSendNotification);
            timerCallback();
        }
    };
    root.addItemList({"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 1);
    rootAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters, "root", root);
    juce::StringArray targets{"None", "Morph Blend", "Spectral Warp", "Grain Position", "Permute Clock"};
    if (JucePlugin_IsSynth) targets.add("Synth FM");
    targets.add("Pitch Amount");
    modTarget.addItemList(targets, 1);
    targetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        p.parameters, "modTarget", modTarget);
    for (auto* l : {&hint, &status})
    {
        addAndMakeVisible(*l);
        l->setFont(font(13));
        l->setColour(juce::Label::textColourId, muted);
        l->setBorderSize(juce::BorderSize<int>(0));
    }
    status.setJustificationType(juce::Justification::right);
    for (auto& b : power)
        b.beforeClick = [this] { remember(); };
    for (auto* b : {&bypass, &freeze, &mutate, &capture, &sync})
        b->beforeClick = [this] { remember(); };
    for (auto* c : {&preset, &choice, &root, &modTarget})
        c->beforeEdit = [this] { remember(); };
    for (auto& f : fields)
        f.beforeEdit = [this] { remember(); };
    for (auto* f : {&mix, &output, &depth, &rate, &attack, &fm})
        f->beforeEdit = [this] { remember(); };
    setSize(960, 700);
    chooseModule(p.getEditorModule());
    updateLanguage();
    startTimerHz(30);
}
ExperimentalEditor::~ExperimentalEditor()
{
    stopTimer();
    play.stop();
    setLookAndFeel(nullptr);
}
void ExperimentalEditor::chooseModule(int m)
{
    selected = std::clamp(m, JucePlugin_IsSynth ? 0 : 1, 5);
    processor.setEditorModule(selected);
    setSize(selected == 0 ? 1120 : 960, selected == 0 ? 760 : expanded ? 840 : 700);
    configure();
}
void ExperimentalEditor::configure()
{
    static const std::array<std::array<const char*, 4>, 6> ids{
        {{"synthTone", "synthSpread", "synthRelease", "synthLevel"},
         {"grainSize", "grainDensity", "grainPitch", "grainPosition"},
         {"permuteClock", "permuteDepth", "permuteFeedback", nullptr},
         {"morphBlend", "morphFocus", "morphMotion", nullptr},
         {"spectralWarp", "spectralSmear", "spectralDynamics", nullptr},
         {"pitchAmount", "transpose", nullptr, nullptr}}};
    for (int i = 0; i < 4; ++i)
    {
        auto* id = ids[(size_t)selected][(size_t)i];
        fieldAttachments[(size_t)i].reset();
        fields[(size_t)i].setVisible(id != nullptr);
        captions[(size_t)i].setVisible(id != nullptr);
        if (id)
            setupField(fields[(size_t)i], id, fieldAttachments[(size_t)i]);
    }
    for (int i = 0; i < 6; ++i)
        select[(size_t)i].setToggleState(i == selected, juce::dontSendNotification);
    choiceAttachment.reset();
    choice.clear(juce::dontSendNotification);
    choice.setVisible(selected == 2 || selected == 3 || selected == 5);
    if (selected == 2)
    {
        choice.addItemList({"XOR", "Rotate", "Reverse", "Stutter"}, 1);
        choiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.parameters, "permuteOperator", choice);
    }
    if (selected == 3)
    {
        choice.addItemList({"Sidechain", "Audio file"}, 1);
        choiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.parameters, "donorSource", choice);
    }
    if (selected == 5)
    {
        choice.addItemList({"Chromatic", "Major", "Minor", "Pentatonic", "Whole tone", "Diminished",
                            "Harmonic minor", "Free"},
                           1);
        choiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.parameters, "scale", choice);
    }
    root.setVisible(selected == 5);
    load.setVisible(selected == 3);
    capture.setVisible(selected == 0);
    play.setVisible(selected == 0);
    freezeAttachment.reset();
    freeze.setVisible(selected == 1 || selected == 4);
    if (selected == 1 || selected == 4)
        freezeAttachment = std::make_unique<QuietButtonAttachment>(
            *processor.parameters.getParameter(selected == 1 ? "grainFreeze" : "freeze"), freeze);
    for (auto* c : std::array<juce::Component*, 4>{&depth, &rate, &modTarget, &sync})
        c->setVisible(expanded || selected == 0);
    for (auto* c : std::array<juce::Component*, 6>{&oscillator, &envelope, &lfo, &waveSource, &waveView, &harmonicView})
        c->setVisible(selected == 0);
    more.setVisible(selected != 0);
    attack.setVisible(selected == 0);
    fm.setVisible(selected == 0);
    piano.setVisible(selected == 0);
    updateLanguage();
    resized();
    timerCallback();
}
void ExperimentalEditor::updateLanguage()
{
    language.setButtonText(russian ? "RU" : "EN");
    more.setButtonText(tr(expanded ? "Less" : "More", expanded ? u8"Меньше" : u8"Ещё"));
    bypass.setButtonText(tr("Bypass", u8"Обход"));
    freeze.setButtonText(tr("Freeze", u8"Удержать"));
    undo.setButtonText(tr("Undo", u8"Отмена"));
    mutate.setButtonText(tr("Mutate", u8"Изменить"));
    capture.setButtonText(tr("Capture", u8"Захват"));
    load.setButtonText(tr("Load", u8"Загрузить"));
    play.setButtonText(tr("Play", u8"Играть"));
    canvas.configure(selected, russian, selected == 0);
    for (auto* graph : {&oscillator, &envelope, &lfo}) graph->setLanguage(russian);
    waveView.setButtonText(tr("Wave", u8"Волна"));
    harmonicView.setButtonText(tr("Harmonics", u8"Гармоники"));
    const int sourceId = waveSource.getSelectedId();
    waveSource.clear(juce::dontSendNotification);
    waveSource.addItemList({tr("Sine", u8"Синус"), tr("Saw harmonics", u8"Пила"),
                           tr("Square harmonics", u8"Прямоугольник"), tr("Soft", u8"Мягкая")}, 1);
    waveSource.setSelectedId(sourceId, juce::dontSendNotification);
    waveSource.setTextWhenNothingSelected(tr("Custom timbre", u8"Свой тембр"));
    preset.setTextWhenNothingSelected(tr("Presets", u8"Пресеты"));
    const std::array<juce::StringArray, 6> labels{
        {{tr("Brightness", u8"Яркость"), tr("Spread", u8"Разброс"), tr("Release", u8"Затухание"),
          tr("Level", u8"Уровень")},
         {tr("Size", u8"Длина"), tr("Density", u8"Плотность"), tr("Pitch", u8"Высота"),
          tr("Position", u8"Позиция")},
         {tr("Clock", u8"Частота"), tr("Disorder", u8"Искажение"), tr("Feedback", u8"Возврат")},
         {tr("Blend", u8"Смешение"), tr("Focus", u8"Фокус"), tr("Motion", u8"Движение")},
         {tr("Warp", u8"Деформация"), tr("Smear", u8"Размытие"), tr("Dynamics", u8"Динамика")},
         {tr("Amount", u8"Сила"), tr("Transpose", u8"Сдвиг")}}};
    for (int i = 0; i < labels[(size_t)selected].size(); ++i)
        captions[(size_t)i].setText(labels[(size_t)selected][i], juce::dontSendNotification);
    for (int i = 0; i < 6; ++i)
    {
        if (!JucePlugin_IsSynth && i == 0) continue;
        select[(size_t)i].setTooltip(
            tr("Open parameters; leaves the sound unchanged.", u8"Открыть параметры. Звук не меняется."));
        power[(size_t)i].setTooltip(
            tr("Enable or bypass this module.", u8"Включить или обойти этот модуль."));
    }
    mutate.setTooltip(tr("Vary this module. Other modules keep their settings.",
                         u8"Изменить этот модуль. Остальные сохраняют настройки."));
    capture.setTooltip(tr("Use the current sound as the synth timbre.",
                          u8"Снять текущий спектр входа для тембра синтезатора."));
    play.setTooltip(
        tr("Hold to play C4. Release to stop.", u8"Удерживай для ноты C4. Отпусти для остановки."));
    bypass.setTooltip(tr("Compare against the input; delay compensation stays the same.",
                         u8"Сравнить с исходным сигналом. Задержка сохраняется."));
    repaint();
}
void ExperimentalEditor::timerCallback()
{
    undo.setEnabled(!history.empty());
    play.setEnabled(processor.value("synthOn") > .5f);
    piano.setEnabled(processor.value("synthOn") > .5f);
    canvas.refresh();
    if (selected == 0)
    {
        oscillator.repaint(); envelope.repaint(); lfo.repaint();
        const auto seed = processor.visualTimbre();
        int shape = 0;
        for (int candidate = 1; candidate <= 4; ++candidate)
        {
            std::array<float, 32> reference{};
            float sum = 0.f;
            for (size_t i = 0; i < reference.size(); ++i)
            {
                const float n = (float)i + 1.f;
                reference[i] = candidate == 1 ? (i == 0 ? 1.f : 0.f) : candidate == 2 ? 1.f / n :
                    i % 2 == 0 ? (candidate == 3 ? 1.f / n : 1.f / (n * n)) : 0.f;
                sum += reference[i];
            }
            bool matches = true;
            for (size_t i = 0; i < reference.size(); ++i)
                matches = matches && std::abs(reference[i] / sum - seed[i]) < .00001f;
            if (matches) { shape = candidate; break; }
        }
        waveSource.setSelectedId(shape, juce::dontSendNotification);
    }
    rate.setEnabled(processor.value("sync") < .5f);
    const bool active = processor.value(exp3::enableIds[selected]) > .5f;
    static constexpr const char* hints[]{"MIDI → Synth → FX",
                                         "Four live grain heads",
                                         "Clocked delay + bit operators",
                                         "Main + donor spectrum",
                                         "Freeze + spectral transformations",
                                         "Spectral note mapping"};
    static constexpr const char* ruHints[]{u8"MIDI → Synth → FX",
                                           u8"Четыре головы живого буфера",
                                           u8"Цифровая задержка и битовые операции",
                                           u8"Основной спектр и донор",
                                           u8"Удержание и преобразование спектра",
                                           u8"Перенос составляющих спектра к нотам"};
    juce::String message = tr(hints[selected], ruHints[selected]);
    if (selected == 0)
        message = processor.voices.load() > 0
                      ? juce::String(processor.voices.load()) + tr(" voices", u8" голосов")
                      : tr("Send MIDI or hold Play", u8"Подай MIDI или удерживай «Играть»");
    if (selected == 3)
        message = processor.value("donorSource") < .5f
                      ? (processor.hasSidechain.load() ? tr("Donor: sidechain", u8"Донор: сайдчейн")
                                                       : tr("Connect a sidechain or load audio",
                                                            u8"Подключи сайдчейн или загрузи аудио"))
                      : processor.donorName();
    if (!active)
        message += tr(" · bypassed", u8" · выключен");
    if (lastError.isNotEmpty())
        message = lastError;
    hint.setText(message, juce::dontSendNotification);
    status.setText(processor.value("bypass") > .5f
                       ? tr("Bypass", u8"Обход")
                       : juce::String(processor.getLatencySamples() * 1000. / std::max(8000., processor.getSampleRate()), 1) +
                             " ms",
                   juce::dontSendNotification);
    repaint();
}
void ExperimentalEditor::paint(juce::Graphics& g)
{
    if (selected == 0)
    {
        g.fillAll(bg);
        text(g, "Exp3r1ment4l", {28, 20, 250, 30}, 22, ink);
        text(g, tr("Synth", u8"Синтезатор"), {282, 23, 180, 24}, 14);
        g.setColour(line);
        g.drawHorizontalLine(66, 28, 1092);
        g.drawHorizontalLine(136, 28, 1092);
        g.setColour(paper);
        for (auto rectangle : {juce::Rectangle<float>(28, 150, 664, 282),
                               juce::Rectangle<float>(708, 150, 384, 282),
                               juce::Rectangle<float>(28, 446, 664, 184),
                               juce::Rectangle<float>(708, 446, 384, 184)})
            g.fillRoundedRectangle(rectangle, 6.f);
        text(g, "OSC 1", {44, 158, 96, 24}, 16, ink);
        text(g, tr("ENV 1 → amplitude", u8"ENV 1 → громкость"), {724, 158, 320, 24}, 16, ink);
        text(g, "LFO 1", {44, 454, 100, 24}, 16, ink);
        text(g, tr("Modulation", u8"Модуляция"), {154, 454, 230, 24}, 13);
        text(g, tr("Output", u8"Выход"), {724, 454, 100, 24}, 16, ink);
        text(g, tr("Spectrum · waveform", u8"Спектр · форма волны"), {836, 456, 240, 22}, 12);
        g.setColour(line);
        g.drawHorizontalLine(344, 44, 676);
        g.drawHorizontalLine(344, 724, 1076);
        text(g, "FM", {366, 352, 142, 20});
        text(g, tr("Attack", u8"Атака"), {730, 352, 146, 20});
        text(g, tr("Depth", u8"Глубина"), {346, 488, 136, 20});
        text(g, tr("Rate", u8"Частота"), {516, 488, 136, 20});
        text(g, tr("Destination", u8"Назначение"), {346, 557, 136, 18}, 12);
        const int footer = getHeight() - 64;
        g.setColour(line);
        g.drawHorizontalLine(footer, 28, 1092);
        text(g, tr("Mix", u8"Смешение"), {32, footer + 18, 80, 20});
        text(g, tr("Output", u8"Выход"), {275, footer + 18, 90, 20});
        text(g, juce::String(processor.voices.load()) + tr(" / 8 voices", u8" / 8 голосов"),
             {780, footer + 18, 145, 20}, 13);
        const float outputLevel = std::clamp(processor.outputPeak.load(), 0.f, 1.f);
        g.setColour(line); g.fillRoundedRectangle(958.f, (float)footer + 26.f, 120.f, 5.f, 2.f);
        g.setColour(ink); g.fillRoundedRectangle(958.f, (float)footer + 26.f, 120.f * outputLevel, 5.f, 2.f);
        return;
    }
    g.fillAll(bg);
    text(g, "Exp3r1ment4l", {28, 20, 250, 30}, 22, ink);
    g.setColour(line);
    g.drawHorizontalLine(66, 28, 932);
    g.drawHorizontalLine(136, 28, 932);
    g.setColour(paper);
    g.fillRoundedRectangle(juce::Rectangle<float>(28, 150, 904, 446), 8);
    text(g, exp3::moduleNames[selected], {44, 158, 250, 28}, 19, ink);
    text(g, tr("Swipe · Shift: fine", u8"Свайп · Shift — точно"),
         {236, 160, 240, 26}, 13, muted);
    if (selected == 2)
        text(g, tr("Operator", u8"Оператор"), {710, 447, 184, 20});
    if (selected == 3)
        text(g, tr("Donor", u8"Донор"), {710, 447, 184, 20});
    if (selected == 5)
    {
        text(g, tr("Root", u8"Тоника"), {490, 447, 184, 20});
        text(g, tr("Scale", u8"Гамма"), {710, 447, 184, 20});
    }
    if (selected == 0)
    {
        text(g, tr("Attack", u8"Атака"), {50, 522, 145, 20});
        text(g, "FM", {270, 522, 145, 20});
        text(g, tr("Play notes", u8"Играй ноты"), {490, 522, 230, 18}, 13);
    }
    if (expanded)
    {
        g.setColour(paper);
        g.fillRoundedRectangle(juce::Rectangle<float>(28, 606, 904, 130), 8);
        text(g, tr("Modulation", u8"Модуляция"), {44, 618, 150, 22}, 16, ink);
        text(g, tr("Depth", u8"Глубина"), {50, 646, 140, 20});
        text(g, tr("Rate", u8"Частота"), {270, 646, 140, 20});
        text(g, tr("Target", u8"Назначение"), {490, 646, 160, 20});
    }
    const int footer = getHeight() - 64;
    g.setColour(line);
    g.drawHorizontalLine(footer, 28, 932);
    text(g, tr("Mix", u8"Смешение"), {32, footer + 18, 80, 20});
    text(g, tr("Output", u8"Выход"), {275, footer + 18, 90, 20});
    const float in = std::clamp(processor.inputPeak.load(), 0.f, 1.f),
                out = std::clamp(processor.outputPeak.load(), 0.f, 1.f);
    for (int i = 0; i < 2; ++i)
    {
        text(g, i ? "OUT" : "IN", {846, footer + 12 + i * 20, 32, 16}, 10);
        g.setColour(line);
        g.fillRoundedRectangle(880.f, (float)(footer + 17 + i * 20), 46.f, 5.f, 2.f);
        g.setColour(juce::Colour(0xff202127));
        g.fillRoundedRectangle(880.f, (float)(footer + 17 + i * 20), 46.f * (i ? out : in), 5.f, 2.f);
    }
}
void ExperimentalEditor::resized()
{
    if (selected == 0)
    {
        preset.setBounds(596, 18, 176, 34);
        undo.setBounds(790, 18, 72, 34);
        language.setBounds(880, 18, 44, 34);
        mutate.setBounds(946, 18, 138, 34);
        int x = 28;
        const int widths[]{166, 173, 180, 167, 182, 196};
        for (int i = 0; i < 6; ++i)
        {
            power[(size_t)i].setBounds(x, 84, 30, 40);
            select[(size_t)i].setBounds(x + 29, 84, widths[i] - 29, 40);
            x += widths[i];
        }
        waveSource.setBounds(146, 154, 220, 32);
        waveView.setBounds(380, 154, 84, 32);
        harmonicView.setBounds(470, 154, 122, 32);
        capture.setBounds(600, 154, 80, 32);
        oscillator.setBounds(44, 192, 632, 148);
        envelope.setBounds(724, 192, 352, 148);
        lfo.setBounds(44, 490, 282, 120);
        canvas.setBounds(724, 490, 352, 120);
        fields[0].setBounds(44, 375, 142, 50);
        fields[1].setBounds(204, 375, 142, 50);
        fm.setBounds(364, 375, 142, 50);
        fields[3].setBounds(524, 375, 152, 50);
        fields[2].setBounds(914, 375, 162, 50);
        captions[0].setBounds(50, 352, 142, 20);
        captions[1].setBounds(210, 352, 142, 20);
        captions[2].setBounds(920, 352, 156, 20);
        captions[3].setBounds(530, 352, 146, 20);
        attack.setBounds(724, 375, 162, 50);
        depth.setBounds(340, 510, 148, 42);
        rate.setBounds(510, 510, 166, 42);
        modTarget.setBounds(340, 578, 336, 34);
        sync.setBounds(560, 452, 116, 32);
        play.setBounds(28, 654, 132, 38);
        piano.setBounds(188, 654, 904, 40);
        hint.setBounds(32, 634, 780, 18);
        status.setBounds(926, 634, 160, 18);
        const int footer = getHeight() - 64;
        mix.setBounds(118, footer + 4, 134, 52);
        output.setBounds(370, footer + 4, 144, 52);
        bypass.setBounds(610, footer + 10, 130, 40);
        return;
    }
    preset.setBounds(606, 18, 148, 34);
    undo.setBounds(774, 18, 74, 34);
    language.setBounds(880, 18, 44, 34);
    const int widths[]{145, 180, 182, 170, 182, 190};
    int x = 28;
    for (int i = 0; i < 6; ++i)
    {
        if (!JucePlugin_IsSynth && i == 0) continue;
        power[(size_t)i].setBounds(x, 84, 30, 40);
        select[(size_t)i].setBounds(x + 29, 84, widths[i] - 29, 40);
        x += widths[i];
    }
    more.setBounds(842, 156, 74, 34);
    mutate.setBounds(746, 156, 90, 34);
    canvas.setBounds(44, 202, 872, 232);
    for (int i = 0; i < 4; ++i)
    {
        captions[(size_t)i].setBounds(50 + i * 220, 447, 184, 20);
        fields[(size_t)i].setBounds(44 + i * 220, 470, 190, 50);
    }
    choice.setBounds(704, 470, 206, 46);
    root.setBounds(484, 470, 190, 46);
    capture.setBounds(704, 558, 124, 30);
    play.setBounds(840, 558, 72, 30);
    freeze.setBounds(44, 548, 124, 34);
    load.setBounds(704, 548, 134, 34);
    depth.setBounds(44, 674, 190, 50);
    rate.setBounds(264, 674, 190, 50);
    modTarget.setBounds(484, 676, 278, 40);
    sync.setBounds(796, 676, 114, 40);
    attack.setBounds(44, 542, 190, 50);
    fm.setBounds(264, 542, 190, 50);
    piano.setBounds(484, 546, 420, 44);
    // Capture/Play remain next to the Synth heading, clear of the keyboard.
    if (selected == 0)
    {
        capture.setBounds(492, 156, 124, 34);
        play.setBounds(626, 156, 72, 34);
    }
    const int footer = getHeight() - 64;
    hint.setBounds(32, footer - 30, 730, 24);
    status.setBounds(796, footer - 30, 130, 24);
    mix.setBounds(118, footer + 4, 134, 52);
    output.setBounds(370, footer + 4, 144, 52);
    bypass.setBounds(610, footer + 10, 130, 40);
}
bool ExperimentalEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1 && juce::File(files[0]).hasFileExtension("wav;aif;aiff;flac;ogg");
}
void ExperimentalEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (files.size() == 1)
    {
        loadFile(juce::File(files[0]));
        chooseModule(3);
    }
}
void ExperimentalEditor::openFile()
{
    fileChooser = std::make_unique<juce::FileChooser>(tr("Choose donor audio", u8"Выбрать аудио донора"),
                                                      juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    auto safe = juce::Component::SafePointer<ExperimentalEditor>(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                             [safe](const juce::FileChooser& c)
                             {
                                 if (safe && c.getResult().existsAsFile())
                                     safe->loadFile(c.getResult());
                             });
}
void ExperimentalEditor::loadFile(const juce::File& file)
{
    remember();
    auto r = processor.loadDonor(file);
    lastError = r.failed() ? r.getErrorMessage() : juce::String();
    timerCallback();
}

void ExperimentalEditor::remember()
{
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    if (!history.empty() && history.back() == state)
        return;
    if (history.size() >= 32)
        history.erase(history.begin());
    history.push_back(std::move(state));
}
void ExperimentalEditor::undoAction()
{
    if (history.empty())
        return;
    auto state = std::move(history.back());
    history.pop_back();
    processor.setStateInformation(state.getData(), (int)state.getSize());
    lastError.clear();
    russian = processor.parameters.state.getProperty("language", "en").toString() == "ru";
    chooseModule(processor.getEditorModule());
}

