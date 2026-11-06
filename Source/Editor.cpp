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
        juce::Component::SafePointer<Editor> safeThis { this };

        fileChooser = std::make_unique<juce::FileChooser> ("Load reference audio",
            juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        auto flags = juce::FileBrowserComponent::openMode
                   | juce::FileBrowserComponent::canSelectFiles;

        // SafePointer: if the editor closes while the chooser is open, the
        // callback becomes a no-op instead of dereferencing freed memory.
        fileChooser->launchAsync (flags, [safeThis] (const juce::FileChooser& fc)
        {
            if (safeThis == nullptr)
                return;

            const auto f = fc.getResult();
            if (f.existsAsFile())
                safeThis->proc.getRefTransport().loadFile (f);
        });
    };

    // Play implies monitoring the reference (usable without toggling A/B too):
    // it raises the abMonitor parameter so automation and host state stay honest.
    playButton.onClick = [this]
    {
        if (auto* ab = dynamic_cast<juce::AudioParameterBool*> (
                proc.getValueTreeState().getParameter ("abMonitor")))
            ab->setValueNotifyingHost (1.0f);
        proc.getRefTransport().startPlayback();
    };
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
    setupReadout (inLufsMLabel, "In M");
    setupReadout (inLufsSLabel, "In S");
    setupReadout (inLufsILabel, "In I");
    setupReadout (inTpLabel,    "In TP");
    setupReadout (refLufsLabel, "Ref I");
    setupReadout (refTpLabel,   "Ref TP");
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
                     static_cast<juce::Component*> (&inLufsMLabel),
                     static_cast<juce::Component*> (&inLufsSLabel),
                     static_cast<juce::Component*> (&inLufsILabel),
                     static_cast<juce::Component*> (&inTpLabel),
                     static_cast<juce::Component*> (&refLufsLabel),
                     static_cast<juce::Component*> (&refTpLabel),
                     static_cast<juce::Component*> (&statusLabel) })
        addAndMakeVisible (c);

    startTimerHz (30); // UI refresh from SharedState (~30 Hz, never from audio thread)
}

//==============================================================================
Editor::~Editor()
{
    // Order matters: no timer, chooser or modal callbacks outlive this object.
    stopTimer();
    fileChooser.reset();
    juce::ModalComponentManager::getInstance()->cancelAllModalComponents();
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

    const auto fmt1 = [] (float v)
    {
        return juce::String (v, 1);
    };

    inLufsMLabel.setText ("In M: "  + fmt1 (s.inputLufsMomentary.load (std::memory_order_relaxed)),
                          juce::dontSendNotification);
    inLufsSLabel.setText ("In S: "  + fmt1 (s.inputLufsShortTerm.load (std::memory_order_relaxed)),
                          juce::dontSendNotification);
    inLufsILabel.setText ("In: "    + fmt1 (s.inputLufsIntegrated.load (std::memory_order_relaxed)),
                          juce::dontSendNotification);
    inTpLabel.setText    ("In TP: " + fmt1 (s.inputTruePeakDbtp.load (std::memory_order_relaxed)),
                          juce::dontSendNotification);

    const float refLufs = s.refLufsIntegrated.load (std::memory_order_relaxed);
    const float refTp   = s.refTruePeakDbtp.load (std::memory_order_relaxed);
    refLufsLabel.setText (refLoaded ? "Ref I: "   + fmt1 (refLufs) : "Ref I: —",
                          juce::dontSendNotification);
    refTpLabel.setText   (refLoaded ? "Ref TP: "  + fmt1 (refTp)   : "Ref TP: —",
                          juce::dontSendNotification);
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
    // Layout budget with clamps (PR #18 fix): vertical overflow used to hand
    // zero-size bounds to the LAST components in the stack (the "A/B toggle
    // vanished" bug) and shrinking the window dropped whole sections. All
    // bands are clamped so every child keeps a non-zero size across the whole
    // resize range (620x500 .. 1600x1200).
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (44); // title band (also drawn in paint)

    // --- transport row ----------------------------------------------------------
    auto row = area.removeFromTop (26);
    loadButton.setBounds  (row.removeFromLeft (76));
    playButton.setBounds  (row.removeFromLeft (58));
    stopButton.setBounds  (row.removeFromLeft (58));
    loopToggle.setBounds  (row.removeFromLeft (64));
    row.removeFromLeft (10);
    modeCombo.setBounds   (row.removeFromLeft (90));

    // --- waveform ---------------------------------------------------------------
    area.removeFromTop (6);
    const int waveH = juce::jlimit (96, 340, (int) ((float) juce::jmax (0, area.getHeight() - 100) * 0.42f));
    waveform.setBounds (area.removeFromTop (waveH));

    // --- readouts: two rows of four cells ----------------------------------------
    // cells: In M / In S / In I / In TP — Ref I / Ref TP / status (wide)
    area.removeFromTop (8);
    auto read1 = area.removeFromTop (22);
    const int cell = juce::jmax (60, read1.getWidth() / 4);
    inLufsMLabel.setBounds (read1.removeFromLeft (cell));
    inLufsSLabel.setBounds (read1.removeFromLeft (cell));
    inLufsILabel.setBounds (read1.removeFromLeft (cell));
    inTpLabel.setBounds    (read1);                      // remainder keeps full width use

    auto read2 = area.removeFromTop (22);
    refLufsLabel.setBounds (read2.removeFromLeft (cell));
    refTpLabel.setBounds   (read2.removeFromLeft (cell));
    read2.removeFromLeft (10);
    statusLabel.setBounds  (read2);

    // --- controls: left toggles column, right sliders column ----------------------
    area.removeFromTop (8);
    auto controlsLeft = area.removeFromLeft (200);
    for (auto* t : { &eqToggle, &stereoToggle, &dynToggle, &loudnessToggle })
    {
        t->setBounds (controlsLeft.removeFromTop (24));
        controlsLeft.removeFromTop (2);
    }
    controlsLeft.removeFromTop (4);
    bypassToggle.setBounds (controlsLeft.removeFromTop (24));
    abToggle.setBounds     (controlsLeft.removeFromTop (24));

    area.removeFromLeft (16);
    const int amountH = juce::jlimit (90, 150, area.getHeight() - 56);
    amountSlider.setBounds     (area.removeFromTop (amountH));
    area.removeFromTop (4);
    targetLufsSlider.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);
    targetTpSlider.setBounds   (area.removeFromTop (24));
}

//==============================================================================
} // namespace masteal
