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
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout()), analyzer (*this)
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

    refTransport.setSharedStateSink (&sharedState);
}

//==============================================================================
Processor::~Processor()
{
    // Meters/ring declared before the thread: the thread must not outlive them.
    analyzer.stopPumping();
}

//==============================================================================
// --- analysis ring (lock-free SPSC, audio -> analysis thread) -----------------

void Processor::AnalysisRing::init (int channels, int newCapacity)
{
    numChannels = channels;
    capacity = newCapacity;
    for (auto& d : data)
    {
        d.assign ((size_t) capacity, 0.0f);
    }
    writePos.store (0, std::memory_order_relaxed);
    readPos.store (0, std::memory_order_relaxed);
}

int Processor::AnalysisRing::availableToRead() const noexcept
{
    return (int) (writePos.load (std::memory_order_acquire)
                - readPos.load (std::memory_order_acquire));
}

int Processor::AnalysisRing::push (const float* const* src, int n) noexcept
{
    const int space = capacity - availableToRead();
    const int toWrite = juce::jmin (n, space);
    if (toWrite <= 0)
        return 0;

    const auto w = writePos.load (std::memory_order_relaxed);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* dst = data[(size_t) ch].data();
        for (int i = 0; i < toWrite; ++i)
            dst[(size_t) ((w + (unsigned int) i) & kMask)] = src[ch][i];
    }
    writePos.store (w + (unsigned int) toWrite, std::memory_order_release);
    return toWrite;
}

int Processor::AnalysisRing::pop (float* const* dst, int max) noexcept
{
    const int have = availableToRead();
    const int toRead = juce::jmin (max, have);
    if (toRead <= 0)
        return 0;

    const auto r = readPos.load (std::memory_order_relaxed);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* src = data[(size_t) ch].data();
        for (int i = 0; i < toRead; ++i)
            dst[ch][i] = src[(size_t) ((r + (unsigned int) i) & kMask)];
    }
    readPos.store (r + (unsigned int) toRead, std::memory_order_release);
    return toRead;
}

//==============================================================================
void Processor::configureMeters (double sampleRate)
{
    loudnessMeter.prepare (sampleRate);
    truePeakMeter.prepare (sampleRate);
}

//==============================================================================
// --- analyzer thread ----------------------------------------------------------

Processor::InputAnalyzerThread::InputAnalyzerThread (Processor& o)
    : juce::Thread ("masteal::InputAnalyzer"),
      owner (o)
{
}

void Processor::InputAnalyzerThread::startPumping()
{
    // Normal priority: at ~30 Hz this costs well under 1 % CPU, but "background"
    // priority starves it under load (meter freezes) and makes drop behaviour
    // unpredictable in tests. Never realtime: it must not race the audio thread
    // for a core.
    startThread (juce::Thread::Priority::normal);
}

void Processor::InputAnalyzerThread::stopPumping()
{
    stopThread (-1);
}

void Processor::InputAnalyzerThread::run()
{
    while (! threadShouldExit())
    {
        wait (33);   // ~30 Hz cadence (AGENTS.md: analysis refresh rate)

        if (threadShouldExit())
            break;

        owner.runAnalysisPassForTesting();
    }
}

void Processor::runAnalysisPassForTesting()
{
    const auto bufferSize = (int) drainBufferL.size();
    if (bufferSize <= 0)
        return;

    float* chans[2] = { drainBufferL.data(), drainBufferR.data() };
    int got = 0;

    while (true)
    {
        const int n = analysisRing.pop (chans, bufferSize);
        if (n <= 0)
            break;

        loudnessMeter.process (chans[0], chans[1], n);
        truePeakMeter.process (chans[0], chans[1], n);
        got += n;
    }

    // Publish (balanced atomics, best-effort: the UI reads what it sees).
    if (got > 0)
    {
        auto& s = sharedState;
        const bool iValid = loudnessMeter.hasIntegrated();
        s.inputLufsMomentary.store (loudnessMeter.getMomentaryLufs(),
                                    std::memory_order_relaxed);
        s.inputLufsShortTerm.store (loudnessMeter.getShortTermLufs(),
                                    std::memory_order_relaxed);
        s.inputLufsIntegrated.store (iValid ? loudnessMeter.getIntegratedLufs()
                                            : SharedState::kSilenceLufs,
                                     std::memory_order_relaxed);
        s.inputTruePeakDbtp.store (truePeakMeter.getTruePeakDbtp(),
                                   std::memory_order_relaxed);
    }
}

//==============================================================================
void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    analyzer.stopPumping();   // meters are single-owner-thread: pause while (re)configuring

    // No DSP yet, no latency. The lookahead limiter (issue #8) will update this.
    setLatencySamples (0);
    refTransport.prepareForAudioProcessing (sampleRate);

    // Analysis ring + meters (allocations happen here, off the audio thread).
    const int drainSize = juce::jmax (samplesPerBlock, 8192);
    analysisRing.init (2, AnalysisRing::kCapacity);
    drainBufferL.assign ((size_t) drainSize, 0.0f);
    drainBufferR.assign ((size_t) drainSize, 0.0f);
    configureMeters (sampleRate);

    analyzer.startPumping();
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

    // === pre-DSP analysis tap (ALWAYS first: measure the untouched input) ====
    if (buffer.getNumSamples() > 0 && buffer.getNumChannels() > 0)
    {
        // mono-bus (FL can open the plugin on mono->stereo): duplicate ch0 so
        // the meters still see full-scale content.
        const float* src[2] = { buffer.getReadPointer (0),
                                buffer.getNumChannels() > 1
                                    ? buffer.getReadPointer (1)
                                    : buffer.getReadPointer (0) };
        analysisRing.push (src, buffer.getNumSamples());   // lock-free, drops on overflow
    }

    // Realtime-safe parameter reads (cached atomics, no APVTS tree lookups).
    const bool bypassed  = bypassParam->load (std::memory_order_relaxed) > 0.5f;
    const bool abMonitor = abMonitorParam->load (std::memory_order_relaxed) > 0.5f;

    if (bypassed) // bit-exact passthrough
        return;

    refTransport.setAbMonitorWanted (abMonitor);

    // A/B: replaces the input with the reference through a 10 ms ramp.
    applyReferenceMonitor (buffer);

    // (Match chain arrives with issue #8+; it always sits AFTER the pre-DSP tap.)
}

//==============================================================================
void Processor::applyReferenceMonitor (juce::AudioBuffer<float>& buffer)
{
    refTransport.mixReferenceInto (buffer);

    // Keep the UI honest on what the output currently is.
    sharedState.abMonitorActive.store (refTransport.isPlaying(),
                                       std::memory_order_relaxed);
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
