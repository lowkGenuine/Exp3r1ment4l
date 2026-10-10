// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "SoundCanvas.h"
#include "SynthGraph.h"
#include <juce_audio_utils/juce_audio_utils.h>

class ScrubField final : public juce::Slider
{
  public:
    std::function<void()> beforeEdit;
    ScrubField();
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

  private:
    juce::Point<float> last, origin;
    bool pending = false, horizontal = true;
    std::unique_ptr<juce::Slider::ScopedDragNotification> gesture;
};
class ActionButton final : public juce::TextButton
{
  public:
    using juce::TextButton::TextButton;
    std::function<void()> beforeClick;
    void clicked(const juce::ModifierKeys&) override
    {
        if (beforeClick)
            beforeClick();
    }
};
class ActionChoice final : public juce::ComboBox
{
  public:
    std::function<void()> beforeEdit;
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (isEnabled() && beforeEdit)
            beforeEdit();
        juce::ComboBox::mouseDown(e);
    }
    bool keyPressed(const juce::KeyPress& k) override
    {
        if (isEnabled() && beforeEdit)
            beforeEdit();
        return juce::ComboBox::keyPressed(k);
    }
};
// Parameter changes update the UI quietly. Only actual button actions reach
// clicked()/onClick, so restoring state cannot record another Undo action.
class QuietButtonAttachment final : private juce::Button::Listener
{
  public:
    QuietButtonAttachment(juce::RangedAudioParameter& parameter, juce::Button& b,
                          juce::UndoManager* undo = nullptr)
        : button(b), attachment(
                         parameter, [this](float value)
                         { button.setToggleState(value >= .5f, juce::dontSendNotification); }, undo)
    {
        attachment.sendInitialUpdate();
        button.addListener(this);
    }
    ~QuietButtonAttachment() override
    {
        button.removeListener(this);
    }

  private:
    juce::Button& button;
    juce::ParameterAttachment attachment;
    void buttonClicked(juce::Button*) override
    {
        attachment.setValueAsCompleteGesture(button.getToggleState() ? 1.f : 0.f);
    }
};
class ExperimentalSkin final : public juce::LookAndFeel_V4
{
  public:
    ExperimentalSkin();
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawLinearSlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider::SliderStyle,
                          juce::Slider&) override;
    void drawLabel(juce::Graphics&, juce::Label&) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
};
class ExperimentalEditor final : public juce::AudioProcessorEditor,
                                 private juce::Timer,
                                 public juce::FileDragAndDropTarget
{
  public:
    explicit ExperimentalEditor(ExperimentalProcessor&);
    ~ExperimentalEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

  private:
    class Audition final : public juce::TextButton
    {
      public:
        ExperimentalProcessor& p;
        bool held = false;
        explicit Audition(ExperimentalProcessor& processor) : TextButton("Play"), p(processor) {}
        void mouseDown(const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseDown(e);
            if (!e.mods.isPopupMenu() && isEnabled())
            {
                held = true;
                p.keyboard.noteOn(1, 60, .7f);
            }
        }
        void mouseUp(const juce::MouseEvent& e) override
        {
            stop();
            juce::TextButton::mouseUp(e);
        }
        void focusLost(FocusChangeType c) override
        {
            stop();
            juce::TextButton::focusLost(c);
        }
        bool keyPressed(const juce::KeyPress& k) override
        {
            if (isEnabled() &&
                (k.getKeyCode() == juce::KeyPress::spaceKey || k.getKeyCode() == juce::KeyPress::returnKey))
            {
                if (!held)
                {
                    held = true;
                    p.keyboard.noteOn(1, 60, .7f);
                }
                return true;
            }
            return juce::TextButton::keyPressed(k);
        }
        bool keyStateChanged(bool down) override
        {
            if (!down)
                stop();
            return held;
        }
        void stop()
        {
            if (held)
            {
                p.keyboard.noteOff(1, 60, 0);
                held = false;
            }
        }
        ~Audition() override
        {
            stop();
        }
    };
    ExperimentalProcessor& processor;
    ExperimentalSkin skin;
    juce::TooltipWindow tooltip{this, 700};
    std::array<juce::TextButton, 6> select;
    std::array<ActionButton, 6> power;
    std::array<std::unique_ptr<QuietButtonAttachment>, 6> powerAttachments;
    std::array<ScrubField, 4> fields;
    std::array<juce::Label, 4> captions;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 4> fieldAttachments;
    ScrubField mix, output, depth, rate, attack, fm;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment, outputAttachment,
        depthAttachment, rateAttachment, attackAttachment, fmAttachment;
    ActionButton bypass{"Bypass"}, freeze{"Freeze"}, mutate{"Mutate"}, capture{"Capture"}, sync{"Sync"};
    juce::TextButton more{"More"}, load{"Load"}, undo{"Undo"}, language{"EN"};
    Audition play;
    SoundCanvas canvas;
    SynthGraph oscillator, envelope, lfo;
    juce::TextButton waveView{"Wave"}, harmonicView{"Harmonics"};
    ActionChoice waveSource;
    juce::MidiKeyboardComponent piano;
    ActionChoice preset, choice, root, modTarget;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> choiceAttachment, rootAttachment,
        targetAttachment;
    std::unique_ptr<QuietButtonAttachment> bypassAttachment, freezeAttachment, syncAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Label hint, status;
    int selected = 1;
    bool expanded = false, russian = false;
    juce::String lastError;
    std::vector<juce::MemoryBlock> history;
    void remember();
    void undoAction();
    void chooseModule(int);
    void configure();
    void timerCallback() override;
    void updateLanguage();
    void openFile();
    void loadFile(const juce::File&);
    juce::String tr(const char*, const char*) const;
    void setupField(ScrubField&, const char*,
                    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExperimentalEditor)
};
