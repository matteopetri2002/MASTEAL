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
    : AudioProcessorEditor (p), proc (p)
{
    setTitle ("MASTEAL editor");
    setResizable (true, true);
    setResizeLimits (560, 420, 1600, 1200);
    setSize (720, 480);

    auto& apvts = proc.getValueTreeState();

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

    setupReadout (inputLufsLabel, "Input LUFS (S)");
    setupReadout (refLufsLabel,   "Ref LUFS (I)");
    setupReadout (inputTpLabel,   "Input True Peak");
    setupReadout (refTpLabel,     "Ref True Peak");
    statusLabel.setText ("No reference loaded", juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8a8f9c));

    for (auto* c : { static_cast<juce::Component*> (&amountSlider),
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
}

void Editor::updateMeasurementLabels()
{
    auto& s = proc.getSharedState();

    const bool refLoaded  = s.referenceLoaded.load (std::memory_order_relaxed);
    const bool abActive   = s.abMonitorActive.load (std::memory_order_relaxed);
    const float inLufs    = s.inputLufsShortTerm.load (std::memory_order_relaxed);
    const float refLufs   = s.refLufsIntegrated.load (std::memory_order_relaxed);
    const float inTp      = s.inputTruePeakDbtp.load (std::memory_order_relaxed);
    const float refTp     = s.refTruePeakDbtp.load (std::memory_order_relaxed);

    inputLufsLabel.setText ("Input LUFS (S): " + juce::String (inLufs, 1), juce::dontSendNotification);
    inputTpLabel.setText   ("Input True Peak: " + juce::String (inTp, 1) + " dBTP", juce::dontSendNotification);

    refLufsLabel.setText (refLoaded ? "Ref LUFS (I): " + juce::String (refLufs, 1)
                                    : "Ref LUFS (I): —", juce::dontSendNotification);
    refTpLabel.setText   (refLoaded ? "Ref True Peak: " + juce::String (refTp, 1) + " dBTP"
                                    : "Ref True Peak: —", juce::dontSendNotification);

    statusLabel.setText (abActive ? "A/B: monitoring reference"
                                  : (refLoaded ? "Reference loaded" : "No reference loaded"),
                         juce::dontSendNotification);
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

    // Left column: controls.
    auto left = body.removeFromLeft (260);
    statusLabel.setBounds (left.removeFromTop (28));
    left.removeFromTop (8);

    amountSlider.setBounds (left.removeFromTop (120));
    left.removeFromTop (8);

    modeCombo.setBounds (left.removeFromTop (28));
    left.removeFromTop (8);

    for (auto* t : { &eqToggle, &stereoToggle, &dynToggle, &loudnessToggle })
    {
        t->setBounds (left.removeFromTop (26));
        left.removeFromTop (2);
    }
    left.removeFromTop (6);

    bypassToggle.setBounds (left.removeFromTop (26));
    abToggle.setBounds (left.removeFromTop (26));
    left.removeFromTop (8);

    targetLufsSlider.setBounds (left.removeFromTop (32));
    targetTpSlider.setBounds (left.removeFromTop (32));

    // Right column: measurement readouts (placeholders until issue #4+ meters).
    body.removeFromLeft (16);
    inputLufsLabel.setBounds (body.removeFromTop (28));
    refLufsLabel.setBounds   (body.removeFromTop (28));
    inputTpLabel.setBounds   (body.removeFromTop (28));
    refTpLabel.setBounds     (body.removeFromTop (28));
}

//==============================================================================
} // namespace masteal
