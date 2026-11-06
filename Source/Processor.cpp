#include "Processor.h"
#include "Editor.h"

namespace masteal
{

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout Processor::createParameterLayout()
{
    using Attr = juce::AudioParameterFloatAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "amount", 1 }, "Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); })));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "eqOn", 1 }, "Match EQ", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "stereoOn", 1 }, "Match Stereo", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "dynOn", 1 }, "Match Dynamics", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "loudnessOn", 1 }, "Match Loudness", true));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "mode", 1 }, "Mode",
        juce::StringArray { "FULL", "STEM" }, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "targetLufs", 1 }, "Target LUFS",
        juce::NormalisableRange<float> (-40.0f, 0.0f, 0.1f), -14.0f,
        Attr().withLabel ("LUFS").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "targetTp", 1 }, "Target True Peak",
        juce::NormalisableRange<float> (-20.0f, 0.0f, 0.1f), -1.0f,
        Attr().withLabel ("dBTP").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); })));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 }, "Bypass Match", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "abMonitor", 1 }, "A/B Monitor Ref", false));

    return layout;
}

//==============================================================================
Processor::Processor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Cache atomic pointers once: reading them in processBlock is lock-free.
    amountParam     = apvts.getRawParameterValue ("amount");
    eqOnParam       = apvts.getRawParameterValue ("eqOn");
    stereoOnParam   = apvts.getRawParameterValue ("stereoOn");
    dynOnParam      = apvts.getRawParameterValue ("dynOn");
    loudnessOnParam = apvts.getRawParameterValue ("loudnessOn");
    modeParam       = apvts.getRawParameterValue ("mode");
    targetLufsParam = apvts.getRawParameterValue ("targetLufs");
    targetTpParam   = apvts.getRawParameterValue ("targetTp");
    bypassParam     = apvts.getRawParameterValue ("bypass");
    abMonitorParam  = apvts.getRawParameterValue ("abMonitor");

    jassert (amountParam != nullptr && bypassParam != nullptr && abMonitorParam != nullptr);
}

//==============================================================================
void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
    // No DSP yet, no latency. The lookahead limiter (issue #8) will update this.
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
    juce::ignoreUnused (midi, buffer);
    juce::ScopedNoDenormals noDenormals;

    // Realtime-safe parameter reads (cached atomics, no APVTS tree lookups).
    const bool bypassed    = bypassParam->load (std::memory_order_relaxed) > 0.5f;
    const bool abMonitorOn = abMonitorParam->load (std::memory_order_relaxed) > 0.5f;
    const float amount     = amountParam->load (std::memory_order_relaxed) * 0.01f;

    juce::ignoreUnused (eqOnParam, stereoOnParam, dynOnParam, loudnessOnParam,
                        modeParam, targetLufsParam, targetTpParam, amount);

    // Publish monitoring state for the UI (lock-free).
    sharedState.abMonitorActive.store (abMonitorOn && ! bypassed,
                                       std::memory_order_relaxed);

    // Passthrough skeleton: no reference loaded yet, so A/B and the match
    // chain are inert. Modules arrive with issues #3 (transport/A-B) and
    // #8+ (DSP chain), always after the pre-DSP analysis tap.
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
