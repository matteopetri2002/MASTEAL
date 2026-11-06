#pragma once

#include "Processor.h"

namespace masteal
{

//==============================================================================
/** Placeholder editor for the skeleton issue. The full UI (waveform, delta
    cards, spectrum) arrives with issues #3 and #7.
*/
class Editor : public juce::AudioProcessorEditor
{
public:
    explicit Editor (Processor& p);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace masteal
