#include "Editor.h"

namespace masteal
{

namespace
{
    void setupReadout (juce::Label& l, const juce::String& name)
    {
        l.setText (name + ": —", juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centredLeft);
        l.setColour (juce::Label::textColourId, juce::Colours::whitesmoke);
    }

    void setupToggle (juce::ToggleButton& b, const juce::String& name)
    {
        b.setButtonText (name);
        b.setColour (juce::ToggleButton::textColourId, juce::Colours::whitesmoke);
    }
}

//==============================================================================
Editor::Editor (Processor& p)
    : AudioProcessorEditor (p),
      proc (p),
      waveform (p.getRefTransport())
{
    setTitle ("MASTEAL editor");
    setResizable (true, true);
    setResizeLimits (620, 500, 1600, 1200);
    setSize (760, 620);

    auto& apvts = proc.getValueTreeState();

    // --- transport UI -----------------------------------------------------------
    loadButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load reference audio",
            juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        auto flags = juce::FileBrowserComponent::openMode
                   | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile())
                proc.getRefTransport().loadFile (f);
        });
    };

    playButton.onClick = [this] { proc.getRefTransport().startPlayback(); };
    stopButton.onClick = [this] { proc.getRefTransport().stopPlayback(); };
    loopToggle.setToggleState (true, juce::dontSendNotification);
    loopToggle.onClick = [this]
    {
        proc.getRefTransport().setLooping (loopToggle.getToggleState());
    };

    // --- parameter attachments --------------------------------------------------
    amountSlider.setRange (0.0, 100.0);
    amountAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "amount", amountSlider);
    amountSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);

    setupToggle (eqToggle,       "Match EQ");
    setupToggle (stereoToggle,   "Match Stereo");
    setupToggle (dynToggle,      "Match Dynamics");
    setupToggle (loudnessToggle, "Match Loudness");
    eqAttach       = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "eqOn", eqToggle);
    stereoAttach   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "stereoOn", stereoToggle);
    dynAttach      = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "dynOn", dynToggle);
    loudnessAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "loudnessOn", loudnessToggle);

    modeCombo.addItem ("FULL", 1);
    modeCombo.addItem ("STEM", 2);
    modeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "mode", modeCombo);

    targetLufsSlider.setRange (-40.0, 0.0);
    targetLufsAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "targetLufs", targetLufsSlider);
    targetTpSlider.setRange (-20.0, 0.0);
    targetTpAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "targetTp", targetTpSlider);

    setupToggle (bypassToggle, "Bypass Match");
    setupToggle (abToggle,     "A/B Monitor Ref");
    bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "bypass", bypassToggle);
    abAttach     = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "abMonitor", abToggle);

    // --- readouts ---------------------------------------------------------------
    setupReadout (inputLufsLabel, "Input LUFS (S)");
    setupReadout (refLufsLabel,   "Ref LUFS (I)");
    setupReadout (inputTpLabel,   "Input True Peak");
    setupReadout (refTpLabel,     "Ref True Peak");
    statusLabel.setText ("No reference loaded", juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8a8f9c));

    for (auto* c : { static_cast<juce::Component*> (&loadButton),
                     static_cast<juce::Component*> (&playButton),
                     static_cast<juce::Component*> (&stopButton),
                     static_cast<juce::Component*> (&loopToggle),
                     static_cast<juce::Component*> (&waveform),
                     static_cast<juce::Component*> (&amountSlider),
                     static_cast<juce::Component*> (&eqToggle),
                     static_cast<juce::Component*> (&stereoToggle),
                     static_cast<juce::Component*> (&dynToggle),
                     static_cast<juce::Component*> (&loudnessToggle),
                     static_cast<juce::Component*> (&modeCombo),
                     static_cast<juce::Component*> (&targetLufsSlider),
                     static_cast<juce::Component*> (&targetTpSlider),
                     static_cast<juce::Component*> (&bypassToggle),
                     static_cast<juce::Component*> (&abToggle),
                     static_cast<juce::Component*> (&inputLufsLabel),
                     static_cast<juce::Component*> (&refLufsLabel),
                     static_cast<juce::Component*> (&inputTpLabel),
                     static_cast<juce::Component*> (&refTpLabel),
                     static_cast<juce::Component*> (&statusLabel) })
        addAndMakeVisible (c);

    startTimerHz (30); // UI refresh from SharedState (~30 Hz, never from audio thread)
}

//==============================================================================
void Editor::timerCallback()
{
    updateMeasurementLabels();

    // Transport readouts that live outside SharedState (playhead refresh).
    const auto& transport = proc.getRefTransport();
    const juce::String status = transport.isLoading()
        ? "Loading reference..."
        : transport.hasClip()
            ? (transport.isPlaying() ? "Playing: " : "Loaded: ") + transport.getClipName()
            : (transport.getLoadErrorText().isNotEmpty()
                ? "Load error: " + transport.getLoadErrorText()
                : juce::String ("No reference loaded"));
    statusLabel.setText (status, juce::dontSendNotification);

    waveform.repaint();
}

void Editor::updateMeasurementLabels()
{
    auto& s = proc.getSharedState();

    const bool refLoaded = s.referenceLoaded.load (std::memory_order_relaxed);
    const float inLufs   = s.inputLufsShortTerm.load (std::memory_order_relaxed);
    const float refLufs  = s.refLufsIntegrated.load (std::memory_order_relaxed);
    const float inTp     = s.inputTruePeakDbtp.load (std::memory_order_relaxed);
    const float refTp    = s.refTruePeakDbtp.load (std::memory_order_relaxed);

    inputLufsLabel.setText ("Input LUFS (S): " + juce::String (inLufs, 1), juce::dontSendNotification);
    inputTpLabel.setText   ("Input True Peak: " + juce::String (inTp, 1) + " dBTP", juce::dontSendNotification);

    refLufsLabel.setText (refLoaded ? "Ref LUFS (I): " + juce::String (refLufs, 1)
                                    : "Ref LUFS (I): —", juce::dontSendNotification);
    refTpLabel.setText   (refLoaded ? "Ref True Peak: " + juce::String (refTp, 1) + " dBTP"
                                    : "Ref True Peak: —", juce::dontSendNotification);
}

//==============================================================================
bool Editor::isInterestedInFileDrag (const juce::StringArray& files)
{
    (void) files;
    return true;
}

void Editor::filesDropped (const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused (x, y);
    if (files.isEmpty())
        return;

    proc.getRefTransport().loadFile (juce::File (files[0]));
}

//==============================================================================
void Editor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1d22)); // dark theme placeholder (LookAndFeel: issue #7)

    g.setColour (juce::Colours::whitesmoke);
    g.setFont (juce::Font (juce::FontOptions (22.0f).withStyle ("Bold")));
    g.drawText ("MASTEAL", getLocalBounds().removeFromTop (44).reduced (16),
                juce::Justification::centredLeft);
}

void Editor::resized()
{
    auto area = getLocalBounds().removeFromTop (getHeight());
    area.removeFromTop (48); // title

    auto body = area.reduced (16);

    // Transport row.
    auto transportRow = body.removeFromTop (30);
    loadButton.setBounds (transportRow.removeFromLeft (80));
    playButton.setBounds (transportRow.removeFromLeft (64));
    stopButton.setBounds (transportRow.removeFromLeft (64));
    loopToggle.setBounds (transportRow.removeFromLeft (72));

    // Waveform.
    body.removeFromTop (6);
    waveform.setBounds (body.removeFromTop (body.getHeight() * 0.42f));

    // Readouts (right column of the remaining space).
    body.removeFromTop (10);
    inputLufsLabel.setBounds (body.removeFromTop (26));
    refLufsLabel.setBounds   (body.removeFromTop (26));
    inputTpLabel.setBounds   (body.removeFromTop (26));
    refTpLabel.setBounds     (body.removeFromTop (26));
    body.removeFromTop (6);

    auto bottom = body;
    auto controls = bottom.removeFromLeft (300);
    modeCombo.setBounds (controls.removeFromTop (26));
    controls.removeFromTop (6);
    for (auto* t : { &eqToggle, &stereoToggle, &dynToggle, &loudnessToggle })
    {
        t->setBounds (controls.removeFromTop (26));
        controls.removeFromTop (2);
    }
    controls.removeFromTop (6);

    bypassToggle.setBounds (controls.removeFromTop (26));
    abToggle.setBounds (controls.removeFromTop (26));

    bottom.removeFromLeft (16);
    amountSlider.setBounds (bottom.removeFromTop (110));
    targetLufsSlider.setBounds (bottom.removeFromTop (30));
    targetTpSlider.setBounds (bottom.removeFromTop (30));
}

//==============================================================================
} // namespace masteal
