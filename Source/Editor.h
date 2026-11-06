#pragma once

#include "Processor.h"
#include "ui/WaveformComponent.h"

namespace masteal
{

//==============================================================================
/** Editor: control surface bound to APVTS + reference transport UI
    (load / play / stop / loop / region / A/B) + readouts from SharedState
    on a ~30 Hz timer.

    Rule (ARCHITECTURE.md "Stato"): the UI reads SharedState and writes
    parameters; it never calls the DSP chain directly.
*/
class Editor : public juce::AudioProcessorEditor,
               private juce::Timer,
               private juce::FileDragAndDropTarget
{
public:
    explicit Editor (Processor& p);
    ~Editor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateMeasurementLabels();

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    Processor& proc;

    // --- reference transport UI (issue #3) ------------------------------------
    WaveformComponent waveform;
    juce::TextButton loadButton { "Load..." };
    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    juce::ToggleButton loopToggle { "Loop" };

    // --- controls ---------------------------------------------------------------
    juce::Slider       amountSlider { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
    juce::ToggleButton eqToggle, stereoToggle, dynToggle, loudnessToggle;
    juce::ComboBox     modeCombo;
    juce::Slider       targetLufsSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                       targetTpSlider   { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton bypassToggle, abToggle;

    // --- APVTS attachments ---------------------------------------------------------
    // CRITICAL (JUCE lifetime rule): these MUST be declared AFTER the controls
    // they attach to. Members are destroyed in reverse order, and each attachment
    // removes itself from its control's listener list in its destructor — if the
    // control is destroyed first, that is a use-after-free (the FL Studio
    // close-window heap corruption).
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   amountAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   eqAttach, stereoAttach, dynAttach, loudnessAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   targetLufsAttach, targetTpAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   bypassAttach, abAttach;

    // --- measurement readouts (SharedState) ------------------------------------
    juce::Label inLufsMLabel, inLufsSLabel, inLufsILabel, inTpLabel;
    juce::Label refLufsLabel, refTpLabel, statusLabel;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace masteal
