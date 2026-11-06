#include "Editor.h"

namespace masteal
{

//==============================================================================
Editor::Editor (Processor& p)
    : AudioProcessorEditor (p)
{
    setTitle ("MASTEAL editor");
    setResizable (true, true);
    setResizeLimits (480, 320, 1600, 1000);
    setSize (720, 440);

    titleLabel.setText ("MASTEAL — reference matching (skeleton)",
                        juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (titleLabel);
}

void Editor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1d22)); // dark theme placeholder
}

void Editor::resized()
{
    titleLabel.setBounds (getLocalBounds().removeFromTop (60).reduced (16));
}

//==============================================================================
} // namespace masteal
