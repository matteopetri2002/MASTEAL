#include "RefTransport.h"

namespace masteal
{

//==============================================================================
void RefClip::buildPeaks()
{
    // One bucket per ~0.25 s, minimum 256 samples: cheap to build, cheap to draw.
    const int n = buffer.getNumSamples();
    peakBucketSamples = juce::jmax (256, (int) ((sampleRate > 0.0 ? sampleRate : 44100.0) * 0.25));
    peaks.clear();
    peaks.reserve ((size_t) juce::jmax (1, (n + peakBucketSamples - 1) / peakBucketSamples));

    const auto* L = buffer.getReadPointer (0);
    const auto* R = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr;

    for (int start = 0; start < n; start += peakBucketSamples)
    {
        const int end = juce::jmin (n, start + peakBucketSamples);
        float m = 0.0f;
        for (int i = start; i < end; ++i)
        {
            const float a = std::abs (L[i]);
            const float b = R != nullptr ? std::abs (R[i]) : 0.0f;
            m = juce::jmax (m, juce::jmax (a, b));
        }
        peaks.push_back (m);
    }

    regionStart = 0;
    regionEnd = n;
}

//==============================================================================
RefTransport::RefTransport()
    : formatManager()
{
    formatManager.registerBasicFormats(); // WAV/AIFF always; FLAC/MP3 per JUCE options
    loaderPool = std::make_unique<juce::ThreadPool> (1); // serial decode, one file at a time
}

RefTransport::~RefTransport()
{
    cancelPendingUpdate();
    stopPlayback();

    // Block until any pending decode finishes (bounded): never let a job
    // touch juce members after this object is gone.
    if (loaderPool != nullptr)
        loaderPool->removeAllJobs (true, 30000);
    loaderPool.reset();
}

//==============================================================================
void RefTransport::loadFile (const juce::File& file)
{
    loading.store (true, std::memory_order_relaxed);

    // Owned pool: jobs are joined in the destructor (bounds-limited), so the
    // transport can be destroyed safely while a load is in flight.
    if (loaderPool != nullptr)
    {
        loaderPool->addJob ([this, file]
        {
            RefClipPtr clip;
            decodeInto (file, clip);
            installClip (std::move (clip));
            loading.store (false, std::memory_order_relaxed);
        });
    }
    else // deck problem: fall back to synchronous
        loadFileSync (file);
}

void RefTransport::loadFileSync (const juce::File& file)
{
    loading.store (true, std::memory_order_relaxed);
    RefClipPtr clip;
    decodeInto (file, clip);
    installClip (std::move (clip));
    loading.store (false, std::memory_order_relaxed);
}

//==============================================================================
void RefTransport::decodeInto (const juce::File& file, RefClipPtr& outClip)
{
    const juce::ScopedLock errorScope (errorLock); // lastError shared with the UI only

    if (! file.existsAsFile())
    {
        lastError = "File not found: " + file.getFileName();
        outClip = nullptr;
        return;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
    {
        lastError = "Unsupported or unreadable audio file: " + file.getFileName();
        outClip = nullptr;
        return;
    }

    const auto numFrames = reader->lengthInSamples;
    if (numFrames <= 0)
    {
        lastError = "Empty audio file: " + file.getFileName();
        outClip = nullptr;
        return;
    }

    if (numFrames > (int64_t) 44100 * 60 * 30) // guard roof: ~30 min stereo int16
    {
        lastError = "File too long (>30 min): " + file.getFileName();
        outClip = nullptr;
        return;
    }

    auto clip = std::make_shared<RefClip>();
    const auto samples = (int) juce::jmin (numFrames, (int64_t) std::numeric_limits<int>::max());
    clip->buffer.setSize (2, samples, false, false, true);

    if (reader->numChannels >= 2)
    {
        reader->read (&clip->buffer, 0, samples, 0, true, true);
    }
    else // mono source: read ch0, copy to ch1 so the clip is always stereo
    {
        juce::AudioBuffer<float> mono (1, samples);
        mono.clear();
        reader->read (&mono, 0, samples, 0, true, false);
        clip->buffer.copyFrom (0, 0, mono, 0, 0, samples);
        clip->buffer.copyFrom (1, 0, mono, 0, 0, samples);
    }

    clip->sampleRate = reader->sampleRate;
    clip->name = file.getFileName();
    clip->buildPeaks();

    outClip = std::move (clip);
}

//==============================================================================
void RefTransport::installClip (RefClipPtr newClip)
{
    if (newClip == nullptr)
    {
        // Failed decode: publish the error to the message thread consumer.
        const juce::SpinLock::ScopedLockType lock (clipLock);
        if (currentClip == nullptr)
            clipAvailable.store (false, std::memory_order_release);
        return;
    }

    {
        // Brief spinlock: pointer swap only. The retired clips are added to
        // tombstones (not deleted) so any block that grabbed a pointer before
        // this swap can never observe freed memory.
        const juce::SpinLock::ScopedLockType lock (clipLock);
        if (currentClip != nullptr)
            tombstonedClips.push_back (std::move (currentClip));

        currentClip = std::move (newClip);
        while (tombstonedClips.size() > 8)
            tombstonedClips.erase (tombstonedClips.begin());

        const auto* c = currentClip.get();
        regionStartAtomic.store (c->regionStart, std::memory_order_relaxed);
        regionLenAtomic.store (c->regionEnd - c->regionStart, std::memory_order_relaxed);
        playhead.store (c->regionStart, std::memory_order_relaxed);
        playing.store (false, std::memory_order_relaxed);
        clipAvailable.store (true, std::memory_order_release);

        if (state != nullptr)
            state->referenceLoaded.store (true, std::memory_order_release);
    }

    triggerAsyncUpdate(); // informational for UI-side consumers (issue #7)
}

void RefTransport::handleAsyncUpdate()
{
    // Reserved for metadata publication (issue #7).
}

//==============================================================================
RefClip* RefTransport::getClipForProcessing() noexcept
{
    const juce::SpinLock::ScopedLockType lock (clipLock);
    return currentClip.get();
}

//==============================================================================
void RefTransport::startPlayback()
{
    const juce::SpinLock::ScopedLockType lock (clipLock);
    if (currentClip == nullptr)
        return;

    playhead.store (regionStartAtomic.load (std::memory_order_relaxed), std::memory_order_relaxed);
    playing.store (true, std::memory_order_release);
}

void RefTransport::stopPlayback() noexcept
{
    playing.store (false, std::memory_order_release);
}

void RefTransport::setLooping (bool on) noexcept
{
    looping.store (on, std::memory_order_relaxed);
}

void RefTransport::resetRegion()
{
    juce::int64 n = 0;
    {
        const juce::SpinLock::ScopedLockType lock (clipLock);
        if (currentClip == nullptr)
            return;

        n = (int64_t) currentClip->buffer.getNumSamples();
    }

    // Out of the lock: setRegion locks on its own (juce::SpinLock is not recursive).
    setRegion (0, n);
}

void RefTransport::setRegion (juce::int64 start, juce::int64 end)
{
    const juce::SpinLock::ScopedLockType lock (clipLock);
    if (currentClip == nullptr)
        return;

    const auto n = (int64_t) currentClip->buffer.getNumSamples();
    const auto s = juce::jlimit<int64_t> (0, n, start);
    const auto e = juce::jlimit (s + 1, n, end);

    currentClip->regionStart = s;   // message-thread-owned fields
    currentClip->regionEnd = e;
    regionStartAtomic.store (s, std::memory_order_relaxed);
    regionLenAtomic.store (e - s, std::memory_order_relaxed);

    const auto pos = playhead.load (std::memory_order_relaxed);
    if (pos < s || pos >= e)
        playhead.store (s, std::memory_order_relaxed);
}

juce::String RefTransport::getClipName() const
{
    const juce::SpinLock::ScopedLockType lock (clipLock);
    return currentClip != nullptr ? currentClip->name : juce::String();
}

juce::String RefTransport::getLoadErrorText() const
{
    const juce::ScopedLock errorScope (errorLock);
    return lastError; // copy under lock: juce::String is not thread-safe cross-thread
}

//==============================================================================
void RefTransport::prepareForAudioProcessing (double sampleRate) noexcept
{
    // Pre-start only: not called while audio is running.
    setSampleRateHint (sampleRate);
    playing.store (false, std::memory_order_relaxed);
    abLevelSmooth = 0.0f;
}

//==============================================================================
void RefTransport::mixReferenceInto (juce::AudioBuffer<float>& buffer) noexcept
{
    const int n = buffer.getNumSamples();
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);

    const bool abWanted   = abMonitorWanted.load (std::memory_order_relaxed);
    const bool playActive = playing.load (std::memory_order_acquire);
    const float srF       = (float) sampleRateHint.load (std::memory_order_relaxed);

    // Skip entirely (bit-exact passthrough) once the fade-out is finished.
    if (! abWanted && abLevelSmooth <= 0.0f)
    {
        abLevelSmooth = 0.0f;
        return;
    }

    RefClip* clip = getClipForProcessing();
    if (clip == nullptr || n <= 0)
    {
        abLevelSmooth = 0.0f;
        return;
    }

    const auto* refL = clip->buffer.getReadPointer (0);
    const auto* refR = clip->buffer.getReadPointer (1);
    const int clipLen = clip->buffer.getNumSamples();
    if (refR == nullptr)
        refR = refL;

    const auto rs = juce::jlimit<int64_t> (0, juce::jmax<int64_t> (clipLen - 1, 0),
                                           regionStartAtomic.load (std::memory_order_relaxed));
    const auto rl = juce::jlimit<int64_t> (1, juce::jmax<int64_t> (clipLen - rs, 1),
                                           regionLenAtomic.load (std::memory_order_relaxed));
    const auto re = rs + rl;

    int64_t pos = playhead.load (std::memory_order_relaxed);
    if (pos < rs || pos >= re)
        pos = rs;

    const float step   = 1.0f / (0.010f * juce::jmax (22050.0f, srF)); // 10 ms ramp
    const float target = (abWanted && playActive) ? 1.0f : 0.0f;
    const bool  loop   = looping.load (std::memory_order_relaxed);
    float level = abLevelSmooth;

    for (int i = 0; i < n; ++i)
    {
        const float rlS = refL[(int) juce::jlimit<int64_t> (0, clipLen - 1, pos)];
        const float rrS = refR[(int) juce::jlimit<int64_t> (0, clipLen - 1, pos)];

        outL[i] = outL[i] * (1.0f - level) + rlS * level;
        outR[i] = outR[i] * (1.0f - level) + rrS * level;

        if (level < target) level = juce::jmin (target, level + step);
        else if (level > target) level = juce::jmax (target, level - step);

        ++pos;
        if (pos >= re)
        {
            if (loop)
            {
                pos = rs;
            }
            else
            {
                playing.store (false, std::memory_order_release);
                playhead.store (rs, std::memory_order_relaxed);
                break;
            }
        }
    }

    if (playing.load (std::memory_order_relaxed))
        playhead.store (pos, std::memory_order_relaxed);

    abLevelSmooth = level;
    juce::ignoreUnused (srF);
}

} // namespace masteal
