#include <atomic>
#include <thread>

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Source/analysis/LoudnessMeter.h"
#include "../Source/analysis/TruePeakMeter.h"
#include "../Source/Processor.h"

namespace
{

constexpr double kPi = 3.14159265358979323846;
const double kSr = 48000.0;

//==============================================================================
/** Stereo sine generator: amplitude per channel in linear scale, frequency ctx. */
void fillStereoSine (juce::AudioBuffer<float>& buffer, double freq, double ampL,
                     double ampR, int fromSample)
{
    for (int i = fromSample; i < buffer.getNumSamples(); ++i)
    {
        const double t = 2.0 * kPi * freq * (double) i / kSr;
        buffer.setSample (0, i, (float) (ampL * std::sin (t)));
        buffer.setSample (1, i, (float) (ampR * std::sin (t)));
    }
}

/** Amplitude of a 997 Hz stereo tone so that integrated loudness == target LUFS:
    the standard K-weighting has +0.691 dB gain at ~1 kHz (ITU-R BS.1770-4),
    which cancels the -0.691 offset, so  L = 10*log10(a^2) = 20*log10(a). */
double amplitudeForLufsStereo (double targetLufs)
{
    return std::pow (10.0, targetLufs / 20.0);
}

/** Amplitude of a 997 Hz LEFT-ONLY tone at target LUFS:
    L = 10*log10(a^2/2) -> amp = 10^((target + 10*log10(2))/20). */
double amplitudeForLufsPanned (double targetLufs)
{
    return std::pow (10.0, (targetLufs + 10.0 * std::log10 (2.0)) / 20.0);
}

/** Feeds the whole buffer through the meter in intentionally irregular chunks
    (streaming behaviour like the analyzer thread). */
void feedLoudness (masteal::LoudnessMeter& meter, const juce::AudioBuffer<float>& signal)
{
    const int chunkSizes[] = { 137, 256, 61, 1000, 128, 512, 83 };
    int pos = 0;
    int chunkIdx = 0;
    while (pos < signal.getNumSamples())
    {
        int n = signal.getNumSamples() - pos;
        if (chunkIdx < (int) std::size (chunkSizes))
            n = juce::jmin (n, chunkSizes[chunkIdx]);
        const float* chans[2] = { signal.getReadPointer (0, pos), signal.getReadPointer (1, pos) };
        meter.process (chans[0], chans[1], n);
        pos += n;
        ++chunkIdx;
    }
}

void feedTruePeak (masteal::TruePeakMeter& tp, const juce::AudioBuffer<float>& signal)
{
    int pos = 0;
    while (pos < signal.getNumSamples())
    {
        const int n = juce::jmin (423, signal.getNumSamples() - pos);
        const float* chans[2] = { signal.getReadPointer (0, pos), signal.getReadPointer (1, pos) };
        tp.process (chans[0], chans[1], n);
        pos += n;
    }
}

} // namespace

//==============================================================================
/** LoudnessMeter + TruePeakMeter unit tests (issue #4).

    Conformance anchors (docs/SPEC.md "Misure", +/-0.1 LU target):
    - 997 Hz stereo sine calibrated to -23.0 LUFS -> reading within +/-0.1;
    - panned (left-only) variant at the matching amplitude;
    - absolute gate: leading silence must not pollute integrated;
    - relative gate: a -50 LUFS tag must not drag gated integrated down;
    - true peak: square 0 dBFS -> TP > 0 dBTP; sine reconstruction exact.
*/
class TestLoudness : public juce::UnitTest
{
public:
    TestLoudness() : juce::UnitTest ("MASTEAL Loudness") {}

    void runTest() override
    {
        using masteal::LoudnessMeter;
        using masteal::TruePeakMeter;
        using masteal::Processor;
        using masteal::SharedState;

        beginTest ("stereo 997 Hz sine at -23.0 LUFS: M/S/I exact");
        {
            LoudnessMeter meter;
            meter.prepare (kSr);

            const double amp = amplitudeForLufsStereo (-23.0);
            juce::AudioBuffer<float> signal (2, (int) (kSr * 4.0));  // 4 s
            fillStereoSine (signal, 997.0, amp, amp, 0);
            feedLoudness (meter, signal);

            expect (meter.hasIntegrated());
            expectWithinAbsoluteError (meter.getIntegratedLufs(), -23.0f, 0.1f);
            expectWithinAbsoluteError (meter.getShortTermLufs(),  -23.0f, 0.1f);
            expectWithinAbsoluteError (meter.getMomentaryLufs(),  -23.0f, 0.1f);
        }

        beginTest ("panned 997 Hz sine (left only) at -23.0 LUFS");
        {
            LoudnessMeter meter;
            meter.prepare (kSr);

            const double ampL = amplitudeForLufsPanned (-23.0);
            juce::AudioBuffer<float> signal (2, (int) (kSr * 4.0));
            fillStereoSine (signal, 997.0, ampL, 0.0, 0);
            feedLoudness (meter, signal);

            expect (meter.hasIntegrated());
            expectWithinAbsoluteError (meter.getIntegratedLufs(), -23.0f, 0.1f);
        }

        beginTest ("absolute gate: leading silence does not count");
        {
            LoudnessMeter meter;
            meter.prepare (kSr);

            // 2 s of leading silence, then 4 s of tone at -23 LUFS:
            // silence blocks are below -70 LUFS -> excluded by the gate; the few
            // blocks straddling the edge remain (that is standard BS.1770-4
            // behaviour, they only bias the result by ~0.3 LU here).
            const double amp = amplitudeForLufsStereo (-23.0);
            const int silence = (int) (kSr * 2.0);
            const int tone = (int) (kSr * 4.0);
            juce::AudioBuffer<float> signal (2, silence + tone);
            signal.clear();
            fillStereoSine (signal, 997.0, amp, amp, silence);
            feedLoudness (meter, signal);

            expect (meter.hasIntegrated());
            expectWithinAbsoluteError (meter.getIntegratedLufs(), -23.0f, 0.5f);
        }

        beginTest ("relative gate: -50 LUFS tag does not drag integrated down");
        {
            LoudnessMeter meter;
            meter.prepare (kSr);

            // 3 s at -23 LUFS then 3 s at -50 LUFS: the quiet tag is above the
            // absolute gate but below (ungated loudness - 10 LU) -> excluded.
            const double amp1 = amplitudeForLufsStereo (-23.0);
            const double amp2 = amplitudeForLufsStereo (-50.0);
            const int s1 = (int) (kSr * 3.0);
            const int s2 = (int) (kSr * 3.0);
            juce::AudioBuffer<float> signal (2, s1 + s2);
            fillStereoSine (signal, 997.0, amp1, amp1, 0);
            fillStereoSine (signal, 997.0, amp2, amp2, s1);
            feedLoudness (meter, signal);

            expect (meter.hasIntegrated());
            expect (meter.getIntegratedLufs() > -25.0);
            expect (meter.getIntegratedLufs() < -22.5);
        }

        beginTest ("reset clears window and gate state");
        {
            LoudnessMeter meter;
            meter.prepare (kSr);
            const double amp = amplitudeForLufsStereo (-20.0);
            juce::AudioBuffer<float> signal (2, (int) (kSr * 2.0));
            fillStereoSine (signal, 997.0, amp, amp, 0);
            feedLoudness (meter, signal);
            expect (meter.hasIntegrated());

            meter.reset();
            expect (! meter.hasIntegrated());
            expectEquals (meter.getCompletedHops(), 0);
        }

        beginTest ("true peak: square 0 dBFS -> TP > 0 dBTP");
        {
            TruePeakMeter tp;
            tp.prepare (kSr);

            juce::AudioBuffer<float> square (2, (int) (kSr * 1.0));
            for (int i = 0; i < square.getNumSamples(); ++i)
            {
                const float v = (i % 96 < 48) ? 1.0f : -1.0f;  // 500 Hz square
                square.setSample (0, i, v);
                square.setSample (1, i, v);
            }
            feedTruePeak (tp, square);

            expect (tp.getTruePeakDbtp() > 0.0);
        }

        beginTest ("true peak: -18 dBFS sine reconstructs within 0.35 dB");
        {
            TruePeakMeter tp;
            tp.prepare (kSr);

            const double amp = std::pow (10.0, -18.0 / 20.0);
            juce::AudioBuffer<float> sine (2, (int) (kSr * 1.0));
            fillStereoSine (sine, 997.0, amp, amp, 0);
            feedTruePeak (tp, sine);

            expectWithinAbsoluteError (tp.getTruePeakDbtp(), -18.0f, 0.35f);
        }

        beginTest ("processor pipeline publishes -23 LUFS into SharedState");
        {
            std::unique_ptr<juce::AudioProcessor> owned (createPluginFilter());
            auto* proc = dynamic_cast<Processor*> (owned.get());
            expect (proc != nullptr);
            proc->prepareToPlay (kSr, 512);

            const double amp = amplitudeForLufsStereo (-23.0);
            const int n = (int) (kSr * 4.0);
            juce::AudioBuffer<float> signal (2, n);
            for (int i = 0; i < n; ++i)
            {
                const double t = 2.0 * kPi * 997.0 * (double) i / kSr;
                signal.setSample (0, i, (float) (amp * std::sin (t)));
                signal.setSample (1, i, (float) (amp * std::sin (t)));
            }

            // Feed through processBlock in 487-sample frames, paced BELOW the
            // analyzer drain rate (~248k samples/s): if the ring overflows the
            // dropped chunks are discontinuities and inflate the true peak.
            juce::MidiBuffer midi;
            for (int pos = 0; pos < n; pos += 487)
            {
                const int chunk = juce::jmin (487, n - pos);
                juce::AudioBuffer<float> frame (2, chunk);
                frame.copyFrom (0, 0, signal, 0, pos, chunk);
                frame.copyFrom (1, 0, signal, 1, pos, chunk);
                proc->processBlock (frame, midi);
                juce::Thread::sleep (5);   // refine: >2x headroom vs the drain rate
            }

            // Poll the published state for up to 5 s (analyzer cadence ~30 Hz).
            float read = SharedState::kSilenceLufs;
            for (int tries = 0; tries < 250; ++tries)
            {
                read = proc->getSharedState().inputLufsIntegrated.load (std::memory_order_relaxed);
                if (read > SharedState::kSilenceLufs)
                    break;
                juce::Thread::sleep (20);
            }

            expectWithinAbsoluteError (read, -23.0f, 0.35f);

            const float tpDb = proc->getSharedState().inputTruePeakDbtp.load (std::memory_order_relaxed);
            expectWithinAbsoluteError (tpDb, -23.0f, 0.4f); // true peak = amp of the -23 LUFS tone
        }

        beginTest ("FL-like stress: prepare x2, small blocks on another thread, resize torture");
        {
            std::unique_ptr<juce::AudioProcessor> owned (createPluginFilter());
            auto* proc = dynamic_cast<Processor*> (owned.get());
            expect (proc != nullptr);

            // FL re-prepares on device changes; meters must survive both calls.
            proc->prepareToPlay (44100.0, 64);
            proc->prepareToPlay (44100.0, 64);

            // Background "audio callback": 64-sample sine frames for ~1.5 s.
            const double amp = amplitudeForLufsStereo (-23.0) * 2.0;  // ~ -17 LUFS, hard to miss
            std::atomic<bool> stopFeeder { false };
            std::atomic<int> blocksProcessed { 0 };
            std::thread feeder ([proc, amp, &stopFeeder, &blocksProcessed]
            {
                juce::MidiBuffer midi;
                while (! stopFeeder.load (std::memory_order_relaxed))
                {
                    juce::AudioBuffer<float> frame (2, 64);
                    frame.clear();
                    for (int i = 0; i < 64; ++i)
                    {
                        const double t = 2.0 * kPi * 997.0 * (double) i / 44100.0;
                        frame.setSample (0, i, (float) (amp * std::sin (t)));
                        frame.setSample (1, i, (float) (amp * std::sin (t)));
                    }
                    proc->processBlock (frame, midi);
                    ++blocksProcessed;
                    juce::Thread::sleep (2);
                }
            });

            // Resize torture while the audio runs: every child must keep
            // non-zero bounds across the whole range.
            {
                std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
                expect (ed != nullptr);

                const juce::Point<int> sizes[] = { { 760, 620 }, { 620, 500 }, { 630, 505 },
                                                   { 1600, 1200 }, { 620, 1200 },
                                                   { 1600, 500 }, { 900, 700 } };
                for (const auto& s : sizes)
                {
                    ed->setSize (s.x, s.y);
                    ed->resized();
                    for (auto* c : ed->getChildren())
                        expect (c->getWidth() > 0 && c->getHeight() > 0);
                }
            }

            // Meters must publish real values from the background feed.
            float inI = SharedState::kSilenceLufs;
            float inTp = SharedState::kSilenceDbtp;
            for (int tries = 0; tries < 200 && blocksProcessed.load() < 250; ++tries)
                juce::Thread::sleep (20);

            for (int tries = 0; tries < 150; ++tries)
            {
                auto& st = proc->getSharedState();
                inI  = st.inputLufsIntegrated.load (std::memory_order_relaxed);
                inTp = st.inputTruePeakDbtp.load (std::memory_order_relaxed);
                if (inI > SharedState::kSilenceLufs && inTp > SharedState::kSilenceDbtp)
                    break;
                juce::Thread::sleep (20);
            }

            stopFeeder.store (true, std::memory_order_relaxed);
            feeder.join();

            expect (inI > SharedState::kSilenceLufs + 20.0f);   // way above silence floor
            expect (inTp > SharedState::kSilenceDbtp + 20.0f);

            // Re-prepare while meters dead: no crash, editor recreatable.
            proc->prepareToPlay (44100.0, 128);
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> frame (2, 128);
            frame.clear();
            proc->processBlock (frame, midi);
            std::unique_ptr<juce::AudioProcessorEditor> ed2 (proc->createEditor());
            expect (ed2 != nullptr);
        }
    }
};

static TestLoudness testLoudness;
