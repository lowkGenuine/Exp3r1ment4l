// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char* text)
{
    if (!ok)
        throw std::runtime_error(text);
}
static juce::Component* find(juce::Component& p, const juce::String& id)
{
    if (p.getComponentID() == id)
        return &p;
    for (auto* c : p.getChildren())
        if (auto* r = find(*c, id))
            return r;
    return nullptr;
}
// The editor updates dependent controls at 15 Hz. Drive its real message loop,
// rather than call private timer/configuration methods from the test.
static void dispatch()
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(90);
}
static void click(juce::Component& p, const juce::String& id)
{
    auto* b = dynamic_cast<juce::Button*>(find(p, id));
    require(b, "Missing button");
    b->triggerClick();
    dispatch();
}
static ScrubField& field(juce::Component& e, const char* id)
{
    auto* f = dynamic_cast<ScrubField*>(find(e, id));
    require(f, "Missing scrub field");
    return *f;
}
static void processAudio(ExperimentalProcessor& p, int blocks = 1)
{
    juce::AudioBuffer<float> audio(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()),
                                   256);
    juce::MidiBuffer midi;
    for (int i = 0; i < blocks; ++i)
    {
        audio.clear();
        p.processBlock(audio, midi);
    }
}
static std::unique_ptr<juce::XmlElement> stateXml(ExperimentalProcessor& p)
{
    juce::MemoryBlock data;
    p.getStateInformation(data);
    auto xml = juce::AudioProcessor::getXmlFromBinary(data.getData(), (int)data.getSize());
    require(xml != nullptr, "Processor state was not valid XML");
    return xml;
}
static void checkHistoryIsolation()
{
    // Use a new instance so an earlier interaction cannot conceal an extra
    // history entry created by an attachment callback during state restoration.
    ExperimentalProcessor p;
    p.setRateAndBufferSizeDetails(48000, 256);
    p.prepareToPlay(48000, 256);
    std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor());
    require(e != nullptr, "History fixture editor creation failed");
    dispatch();
    auto* undo = dynamic_cast<juce::Button*>(find(*e, "undo"));
    auto* spectralPower = dynamic_cast<juce::Button*>(find(*e, "power4"));
    require(undo && spectralPower && !undo->isEnabled(), "A fresh editor must start with empty Undo history");
    const int originalModule = p.getEditorModule();
    for (float enabled : {1.f, 0.f, 1.f})
    {
        p.setParameter("spectralOn", enabled);
        dispatch();
        require(spectralPower->getToggleState() == (enabled > .5f),
                "Host automation must update the module power indicator");
        require(p.getEditorModule() == originalModule,
                "Host automation must not select a different editor module");
        require(!undo->isEnabled(), "Host automation must not add a user action to Undo history");
    }

    auto& size = field(*e, "grainSize");
    auto& density = field(*e, "grainDensity");
    const float originalSize = p.value("grainSize"), originalDensity = p.value("grainDensity");
    require(size.keyPressed(juce::KeyPress(juce::KeyPress::upKey)), "First keyboard edit was rejected");
    dispatch();
    const float editedSize = p.value("grainSize");
    require(editedSize > originalSize, "First keyboard edit did not change Size");
    require(density.keyPressed(juce::KeyPress(juce::KeyPress::upKey)), "Second keyboard edit was rejected");
    dispatch();
    require(p.value("grainDensity") > originalDensity && undo->isEnabled(),
            "Second keyboard edit did not create an undoable Density change");
    click(*e, "undo");
    processAudio(p);
    dispatch();
    require(std::abs(p.value("grainDensity") - originalDensity) < .001f &&
                std::abs(p.value("grainSize") - editedSize) < .01f,
            "The first Undo must revert only the most recent field edit");
    require(undo->isEnabled(), "The earlier field edit must remain available to Undo");
    click(*e, "undo");
    processAudio(p);
    dispatch();
    require(std::abs(p.value("grainSize") - originalSize) < .01f &&
                std::abs(p.value("grainDensity") - originalDensity) < .001f,
            "The second Undo must restore both original field values");
    require(p.value("spectralOn") == 1 && !undo->isEnabled(),
            "Two Undos must empty the two-edit history without undoing host automation or adding restore "
            "callbacks");

    auto beforePreset = stateXml(p);
    auto* presets = dynamic_cast<ActionChoice*>(find(*e, "presets"));
    require(presets && presets->beforeEdit, "Preset action fixture was unavailable");
    // Opening the choice records a user action before its selection changes.
    // Invoke that public input hook because this harness selects the menu item
    // directly instead of depending on an operating-system popup menu.
    presets->beforeEdit();
    presets->setSelectedId(2, juce::sendNotificationSync);
    dispatch();
    processAudio(p);
    auto afterPreset = stateXml(p);
    require(!juce::ValueTree::fromXml(*beforePreset).isEquivalentTo(juce::ValueTree::fromXml(*afterPreset)) &&
                undo->isEnabled(),
            "Choosing a preset must change the saved sound and record one user action");
    click(*e, "undo");
    processAudio(p);
    dispatch();
    auto restored = stateXml(p);
    require(juce::ValueTree::fromXml(*beforePreset).isEquivalentTo(juce::ValueTree::fromXml(*restored)),
            "Preset Undo must restore the complete previous state");
    require(!undo->isEnabled(),
            "Preset Undo must leave empty history rather than add reentrant attachment actions");
    e.reset();
    p.releaseResources();
}
static void checkUndo(ExperimentalProcessor& p, juce::Component& e)
{
    click(e, "select1");
    const float original = p.value("grainSize");
    require(p.value("grainsOn") == 1, "Undo fixture should start with an enabled grain module");
    click(e, "power1");
    require(p.value("grainsOn") == 0, "Power click should disable grain processing");
    click(e, "undo");
    processAudio(p);
    require(p.value("grainsOn") == 1 && std::abs(p.value("grainSize") - original) < .01f,
            "Undo should restore activation without erasing the configured sound");
    auto& size = field(e, "grainSize");
    require(size.keyPressed(juce::KeyPress(juce::KeyPress::upKey)),
            "Scrub field did not accept a keyboard edit");
    dispatch();
    require(p.value("grainSize") > original, "Keyboard edit did not reach the grain parameter");
    click(e, "undo");
    processAudio(p);
    require(std::abs(p.value("grainSize") - original) < .01f && p.value("grainsOn") == 1,
            "Undo should restore the previous field value and keep the module enabled");
}
static void checkMutationRollback(ExperimentalProcessor& p, juce::Component& e)
{
    click(e, "select0");
    processAudio(p);
    auto before = stateXml(p);
    const auto originalSeed = before->getStringAttribute("seed");
    require(originalSeed.isNotEmpty(), "Saved synth seed was empty");
    click(e, "mutate");
    processAudio(p, 2);
    dispatch();
    auto mutated = stateXml(p);
    require(mutated->getStringAttribute("seed") != originalSeed,
            "Mutate must reach the audio engine and change its saved synth seed");
    click(e, "undo");
    processAudio(p, 2);
    dispatch();
    auto restored = stateXml(p);
    require(restored->getStringAttribute("seed") == originalSeed,
            "Undo must restore the synth seed after queued audio-thread mutation");
    require(juce::ValueTree::fromXml(*before).isEquivalentTo(juce::ValueTree::fromXml(*restored)),
            "Mutation Undo must restore the complete saved state, not only visible fields");
}
static void checkPercentSyncAndPlay(ExperimentalProcessor& p, juce::Component& e)
{
    auto& mix = field(e, "mix");
    const auto full = mix.getTextFromValue(1);
    require(full.contains("100") && full.contains("%"),
            "A full normalized amount must display as 100 percent");
    mix.setValue(mix.getValueFromText("50 %"), juce::sendNotificationSync);
    require(std::abs(p.value("mix") - .5f) < .0001f, "Entering 50 percent must set a half wet mix");
    mix.setValue(mix.getValueFromText("100 %"), juce::sendNotificationSync);
    require(std::abs(p.value("mix") - 1.f) < .0001f,
            "Entering 100 percent must set unity, not an out-of-range amount");

    click(e, "more");
    auto& rate = field(e, "rate");
    const float originalRate = p.value("rate");
    click(e, "sync");
    require(p.value("sync") == 1 && !rate.isEnabled(), "Host Sync must disable the manual rate field");
    require(!rate.keyPressed(juce::KeyPress(juce::KeyPress::upKey)),
            "A disabled rate field must reject keyboard edits");
    require(std::abs(p.value("rate") - originalRate) < .0001f,
            "A keyboard edit must not overwrite the stored manual rate while synced");
    click(e, "sync");
    require(p.value("sync") == 0 && rate.isEnabled(), "Disabling Sync must restore manual rate editing");
    click(e, "more");

    click(e, "power0");
    dispatch();
    auto* play = find(e, "play");
    require(p.value("synthOn") == 1 && play && play->isVisible() && play->isEnabled(),
            "Enabling Synth must make the Play action available");
    require(play->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)), "Play must accept a held Space key");
    processAudio(p, 4);
    require(p.voices.load() > 0, "Holding Play must create a real MIDI synth voice");
    play->keyStateChanged(false);
    processAudio(p, 320);
    require(p.voices.load() == 0, "Releasing Space must release the audition voice without a stuck note");
    click(e, "power0");
    click(e, "select1");
}
static void save(juce::Component& e, const juce::File& file)
{
    auto image = e.createComponentSnapshot(e.getLocalBounds(), true, 1.5f);
    juce::PNGImageFormat png;
    auto stream = file.createOutputStream();
    require(stream != nullptr, "PNG output unavailable");
    require(png.writeImageToStream(image, *stream), "PNG encoding failed");
}
int main(int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI gui;
        ExperimentalProcessor p;
        p.setRateAndBufferSizeDetails(48000, 256);
        p.prepareToPlay(48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor());
        require(e != nullptr, "Editor creation failed");
        juce::DocumentWindow w("Exp3r1ment4l", juce::Colours::white, juce::DocumentWindow::allButtons);
        w.setContentNonOwned(e.get(), true);
        w.setVisible(true);
        auto directory = juce::File(argc > 1 ? argv[1] : "assets");
        directory.createDirectory();
        for (int i = 0; i < 6; ++i)
        {
            click(*e, "select" + juce::String(i));
            for (int j = 0; j < 6; ++j)
                require(p.value(exp3::enableIds[j]) == 0, "Selection changed module activation");
        }
        click(*e, "power1");
        require(p.value("grainsOn") == 1 && p.getEditorModule() == 1, "Power should enable and select");
        click(*e, "select4");
        require(p.value("grainsOn") == 1 && p.value("spectralOn") == 0,
                "Selection changed the audible chain");
        click(*e, "select1");
        auto* size = dynamic_cast<juce::Slider*>(find(*e, "grainSize"));
        require(size, "Grain Size field missing");
        size->setValue(143, juce::sendNotificationSync);
        require(std::abs(p.value("grainSize") - 143) < .01, "Field automation failed");
        click(*e, "power1");
        require(p.value("grainsOn") == 0 && std::abs(p.value("grainSize") - 143) < .01,
                "Disabling erased module settings");
        click(*e, "power1");
        click(*e, "bypass");
        require(p.value("bypass") == 1, "Bypass attachment failed");
        click(*e, "bypass");
        checkHistoryIsolation();
        checkUndo(p, *e);
        checkMutationRollback(p, *e);
        checkPercentSyncAndPlay(p, *e);
        save(*e, directory.getChildFile("preview.png"));
        for (int i = 0; i < 6; ++i)
        {
            click(*e, "select" + juce::String(i));
            save(*e, directory.getChildFile("module-" + juce::String(i) + ".png"));
        }
        click(*e, "select0");
        click(*e, "more");
        require(e->getHeight() > 410, "More did not reveal controls");
        save(*e, directory.getChildFile("preview-more.png"));
        click(*e, "more");
        click(*e, "language");
        save(*e, directory.getChildFile("preview-ru.png"));
        w.setContentNonOwned(nullptr, false);
        w.setVisible(false);
        e.reset();
        p.releaseResources();
        std::cout << "PASS: module selection, activation, persistence, host automation isolation, sequential "
                     "field Undo, preset rollback without reentrant history, complete mutation/seed "
                     "rollback, percent entry, Sync rate guard, held-key MIDI audition, bypass, secondary "
                     "controls, EN/RU and native PNG renders\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "FAIL: " << ex.what() << '\n';
        return 1;
    }
}
