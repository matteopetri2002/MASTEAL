#pragma once

#include "Processor.h"

namespace masteal
{

//==============================================================================
/** Editor: control surface bound to APVTS + measurement readouts from
    SharedState on a ~30 Hz timer.

    Rule (ARCHITECTURE.md "Stato"): the UI reads SharedState and writes
    parameters; it never calls the DSP chain directly.
*/
class Editor : public juce::AudioProcessorEditor,
               private juce::Timer
{
public:
    explicit Editor (Processor& p);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateMeasurementLabels();

    Processor& proc;

    // --- controls (APVTS attachments) -----------------------------------------
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   amountAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   eqAttach, stereoAttach, dynAttach, loudnessAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   targetLufsAttach, targetTpAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   bypassAttach, abAttach;

    juce::Slider      amountSlider   { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
    juce::ToggleButton eqToggle, stereoToggle, dynToggle, loudnessToggle;
    juce::ComboBox    modeCombo;
    juce::Slider      targetLufsSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                      targetTpSlider   { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton bypassToggle, abToggle;

    // --- measurement readouts (SharedState) ------------------------------------
    juce::Label inputLufsLabel, refLufsLabel, inputTpLabel, refTpLabel, statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace masteal
