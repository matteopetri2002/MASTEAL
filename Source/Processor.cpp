#include "Processor.h"
#include "Editor.h"

namespace masteal
{

//==============================================================================
Processor::Processor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

//==============================================================================
void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
    // Passthrough skeleton: no DSP yet, no latency.
    setLatencySamples (0);
}

void Processor::releaseResources()
{
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto mainIn = layouts.getMainInputChannelSet();
    return mainIn.isDisabled() || mainIn == juce::AudioChannelSet::stereo();
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

    // Passthrough: the input buffer is already the output buffer.
    // (Analysis and DSP chain arrive with issues #4+; analysis taps the
    //  input *before* the chain — see ARCHITECTURE.md "Regole fisse".)
}

//==============================================================================
juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

//==============================================================================
} // namespace masteal

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new masteal::Processor();
}
