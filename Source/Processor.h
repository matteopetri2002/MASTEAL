#pragma once

#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include "SharedState.h"
#include "reference/RefTransport.h"
#include "analysis/LoudnessMeter.h"
#include "analysis/TruePeakMeter.h"

namespace masteal
{

//==============================================================================
/** MASTEAL processor: owns the parameter tree (APVTS), the reference
    transport and the lock-free SharedState published to the UI.

    Rules (see AGENTS.md / ARCHITECTURE.md):
    - processBlock must stay realtime-safe: no allocations, locks or I/O.
      Parameters are read through cached std::atomic<float>* pointers; the
      only lock touched is the transport's pointer-swap spinlock.
    - Any latency introduced must be declared via setLatencySamples().
    - A/B (issue #3) replaces the input with the reference clip when the
      abMonitor parameter is on and the transport is playing; a 10 ms ramp
      prevents clicks. This is a real-time passthrough until issue #8+ adds
      the match chain, which will always sit AFTER the (pre-DSP) analysis tap.
*/
class Processor : public juce::AudioProcessor
{
public:
    Processor();
    ~Processor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return "MASTEAL"; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    //==============================================================================
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    //==============================================================================
    SharedState& getSharedState() { return sharedState; }
    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }
    RefTransport& getRefTransport() { return refTransport; }

    /** Test hook: forces one analysis pass synchronously on the calling thread
        (the XML dumps what the realtime analyzer would publish). */
    void runAnalysisPassForTesting();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void applyReferenceMonitor (juce::AudioBuffer<float>& buffer);   // A/B crossfade path

    //==============================================================================
    // --- pre-DSP analysis ring (issue #4) --------------------------------------
    // Single-producer/single-consumer lock-free ring: pushed from processBlock
    // (audio thread), drained at ~30 Hz by the analyzer thread. Plain atomics,
    // no juce::AbstractFifo allocation or locks.
    struct AnalysisRing
    {
        static constexpr int kCapacity = 32768;    // ~740 ms @ 44.1 k, power of 2
        static constexpr int kMask = kCapacity - 1;

        void init (int channels, int capacity);
        int push (const float* const* src, int n) noexcept;    // audio thread
        int pop (float* const* dst, int max) noexcept;         // analysis thread
        int availableToRead() const noexcept;

        std::vector<float> data[2];
        int capacity = 0;
        std::atomic<unsigned int> writePos { 0 };
        std::atomic<unsigned int> readPos  { 0 };
        int numChannels = 0;
    };

    void configureMeters (double sampleRate);

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    RefTransport refTransport;
    SharedState sharedState;

    // Cached raw parameter pointers: realtime-safe atomic reads in processBlock.
    std::atomic<float>* amountParam      = nullptr;
    std::atomic<float>* eqOnParam        = nullptr;
    std::atomic<float>* stereoOnParam    = nullptr;
    std::atomic<float>* dynOnParam       = nullptr;
    std::atomic<float>* loudnessOnParam  = nullptr;
    std::atomic<float>* modeParam        = nullptr;
    std::atomic<float>* targetLufsParam  = nullptr;
    std::atomic<float>* targetTpParam    = nullptr;
    std::atomic<float>* bypassParam      = nullptr;
    std::atomic<float>* abMonitorParam   = nullptr;

    // --- analysis (issue #4) ----------------------------------------------------
    // Meters are touched ONLY by the analyzer thread; processBlock only pushes
    // raw pre-DSP samples into the ring. Declared BEFORE the thread so the
    // thread is destroyed first in reverse-order teardown.
    AnalysisRing      analysisRing;
    LoudnessMeter     loudnessMeter;
    TruePeakMeter     truePeakMeter;
    std::vector<float> drainBufferL, drainBufferR;   // preallocated drains

    /** ~30 Hz pre-DSP analyzer (own thread, never inside processBlock). */
    class InputAnalyzerThread : private juce::Thread
    {
    public:
        explicit InputAnalyzerThread (Processor& owner);
        void startPumping();
        void stopPumping();

    private:
        void run() override;

        Processor& owner;
    };

    InputAnalyzerThread analyzer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace masteal
