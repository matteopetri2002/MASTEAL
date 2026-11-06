#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <juce_events/juce_events.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include "../SharedState.h"

namespace masteal
{

//==============================================================================
/** A decoded reference clip. Written once by the loader thread, then read-only
    (plus the analysis region, owned by the message thread and mirrored into
    atomics for the audio thread).

    Rules (ARCHITECTURE.md):
    - the loader thread builds it off real-time and swaps it in under a spin
      lock held only for the pointer swap;
    - lifetime for the audio thread is guaranteed via tombstoning: the previous
      clip is kept alive by RefTransport (retired, not deleted) so a block that
      started before a load swap can never read freed memory;
    - holds a fixed-size peaks cache (1 bucket per ~0.25 s) for the waveform.
*/
struct RefClip
{
    juce::AudioBuffer<float> buffer;   // always 2 channels (mono sources copied to L/R)
    double sampleRate { 0.0 };
    juce::String name;
    juce::int64 regionStart { 0 };     // fingerprint analysis region (samples)
    juce::int64 regionEnd   { 0 };
    std::vector<float> peaks;          // max abs per bucket (L+R combined)
    int peakBucketSamples { 0 };

    void buildPeaks();
};

using RefClipPtr = std::shared_ptr<RefClip>;

//==============================================================================
/** Loads WAV/AIFF/FLAC/MP3 files into RAM, owns the reference transport
    (play/stop/loop + playhead) and the A/B source used by Processor.

    Threading contract:
    - messages/UI thread: loadFile, transport controls, region editing;
    - audio thread: getClipForProcessing() and playing/playhead state;
    - all cross-thread fields are atomics or swapped under a BRIEF spinlock
      (AGENTS.md "Convenzioni").

    A/B behaviour (v1, simple): when abMonitor is on and playback started,
    the reference plays on a loop over its analysis region, replacing the
    channel input through a 10 ms crossfade. Transport is independent of the
    DAW transport in this issue.
*/
class RefTransport : private juce::AsyncUpdater
{
public:
    RefTransport();
    ~RefTransport() override;

    //==========================================================================
    // /UI thread API/
    void loadFile (const juce::File& file);          // background decode
    void loadFileSync (const juce::File& file);      // synchronous (tests)
    void startPlayback();
    void stopPlayback() noexcept;
    void setLooping (bool on) noexcept;
    void resetRegion();                               // full clip
    void setRegion (juce::int64 start, juce::int64 end); // message thread: region edit from UI

    bool isPlaying()   const noexcept { return playing.load (std::memory_order_relaxed); }
    bool isLooping()   const noexcept { return looping.load (std::memory_order_relaxed); }
    bool hasClip()     const noexcept { return clipAvailable.load (std::memory_order_relaxed); }
    bool isLoading()   const noexcept { return loading.load (std::memory_order_relaxed); }

    juce::int64 getPlayPositionSamples() const noexcept { return playhead.load (std::memory_order_relaxed); }
    juce::int64 getRegionStartSamples()  const noexcept { return regionStartAtomic.load (std::memory_order_relaxed); }
    juce::int64 getRegionLengthSamples() const noexcept { return regionLenAtomic.load (std::memory_order_relaxed); }
    juce::String getClipName() const;                  // message thread only
    juce::String getLoadErrorText() const;             // message thread only

    //==========================================================================
    // Using the audio thread API: parameter mirror / sample rate set by the Processor.
    void setAbMonitorWanted (bool on) noexcept { abMonitorWanted.store (on, std::memory_order_relaxed); }
    void setSampleRateHint (double sr) noexcept { sampleRateHint.store (sr > 0.0 ? sr : 44100.0, std::memory_order_relaxed); }

    //==========================================================================
    // /Audio thread API/
    /** Called from Processor::prepareToPlay (before the audio starts):
        resets fades and transport. Safe: not called mid-processing. */
    void prepareForAudioProcessing (double sampleRate) noexcept;
    /** Pointer to the current clip, valid until the next load. Held alive by
        the retired-clip mechanism; do not delete, do not store. */
    RefClip* getClipForProcessing() noexcept;
    void mixReferenceInto (juce::AudioBuffer<float>& buffer) noexcept;  // A/B crossfade + transport

    //==========================================================================
    void setSharedStateSink (SharedState* s) noexcept { state = s; }

private:
    void handleAsyncUpdate() override;                 // publishes clip metadata to SharedState
    void publishClipToSharedState (const RefClip& c) noexcept;
    void decodeInto (const juce::File& file, RefClipPtr& outClip); // found errors stored in lastError
    void installClip (RefClipPtr newClip);             // message/loader thread

    juce::AudioFormatManager formatManager;

    //==========================================================================
    // clip ownership (Realtime-safe pointer discipline, see above)
    mutable juce::SpinLock clipLock;                   // held only for a pointer swap/read
    RefClipPtr currentClip;                            // locked: swap/read only
    std::vector<RefClipPtr> tombstonedClips;           // retired clips (keeps audio-block pointers alive)
    std::atomic<bool> clipAvailable { false };

    //==========================================================================
    // transport state (atomics: audio <-> message)
    std::atomic<int64_t> playhead { 0 };
    std::atomic<int64_t> regionStartAtomic { 0 };
    std::atomic<int64_t> regionLenAtomic   { 0 };
    std::atomic<bool>    playing { false };
    std::atomic<bool>    looping { true };
    std::atomic<bool>    loading { false };

    //==========================================================================
    // audio-side knobs published by Processor (atomics)
    std::atomic<bool>    abMonitorWanted { false };
    std::atomic<double>  sampleRateHint { 44100.0 };
    float                abLevelSmooth = 0.0f;         // audio thread only

    SharedState* state = nullptr;

    //==========================================================================
    // loader thread pool (owned; jobs joined in the destructor — no detached
    // threads that could outlive the transport and corrupt the heap)
    std::unique_ptr<juce::ThreadPool> loaderPool;

    // loader messaging (message+loader thread, accessed under errorLock)
    juce::String lastError;
    mutable juce::CriticalSection errorLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RefTransport)
};

} // namespace masteal
