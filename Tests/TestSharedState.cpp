#include <juce_core/juce_core.h>
#include <thread>
#include <chrono>
#include "../Source/SharedState.h"

//==============================================================================
/** SharedState contract tests: defaults, atomic round-trips, non-blocking
    producer/consumer semantics (issue #2).
*/
class SharedStateTest : public juce::UnitTest
{
public:
    SharedStateTest() : juce::UnitTest ("MASTEAL SharedState") {}

    void runTest() override
    {
        using masteal::SharedState;

        beginTest ("defaults: silence and no reference");
        {
            SharedState s;
            expectEquals (s.inputLufsMomentary.load(),  SharedState::kSilenceLufs);
            expectEquals (s.inputLufsShortTerm.load(),  SharedState::kSilenceLufs);
            expectEquals (s.inputLufsIntegrated.load(), SharedState::kSilenceLufs);
            expectEquals (s.inputTruePeakDbtp.load(),   SharedState::kSilenceDbtp);
            expectEquals (s.refLufsIntegrated.load(),   SharedState::kSilenceLufs);
            expectEquals (s.refTruePeakDbtp.load(),     SharedState::kSilenceDbtp);
            expect (! s.referenceLoaded.load());
            expect (! s.abMonitorActive.load());
            expectEquals (s.reloadFingerprintRequest.load(), 0);
        }

        beginTest ("atomic round-trip");
        {
            SharedState s;
            s.inputLufsShortTerm.store (-12.3f);
            s.inputTruePeakDbtp.store (-0.7f);
            s.refLufsIntegrated.store (-9.0f);
            s.referenceLoaded.store (true);
            s.abMonitorActive.store (true);

            expectEquals (s.inputLufsShortTerm.load(), -12.3f);
            expectEquals (s.inputTruePeakDbtp.load(), -0.7f);
            expectEquals (s.refLufsIntegrated.load(), -9.0f);
            expect (s.referenceLoaded.load());
            expect (s.abMonitorActive.load());
        }

        beginTest ("UI command counter is monotonic");
        {
            SharedState s;
            s.reloadFingerprintRequest.fetch_add (1);
            s.reloadFingerprintRequest.fetch_add (1);
            expectEquals (s.reloadFingerprintRequest.load(), 2);
        }

        beginTest ("concurrent producer/consumer does not tear");
        {
            SharedState s;
            s.inputLufsShortTerm.store (-60.0f); // start of the published range
            std::atomic<bool> stop { false };

            // Simulates: audio thread publishes at ~30 Hz while UI reads.
            std::thread producer ([&] {
                for (float v = -60.0f; v < 0.0f && ! stop.load(); v += 0.5f)
                {
                    s.inputLufsShortTerm.store (v, std::memory_order_relaxed);
                    std::this_thread::sleep_for (std::chrono::milliseconds (1));
                }
                stop.store (true);
            });

            while (! stop.load())
            {
                const float v = s.inputLufsShortTerm.load (std::memory_order_relaxed);
                expect (v >= -60.0f && v <= 0.0f); // always a valid, whole value
            }
            producer.join();
        }
    }
};

static SharedStateTest sharedStateTest;
