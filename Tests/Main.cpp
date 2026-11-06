#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/** Sanity test: verifies the test harness works. Real analysis/DSP tests
    arrive with issues #4-#6 (see docs/SPEC.md acceptance criteria).
*/
class SanityTest : public juce::UnitTest
{
public:
    SanityTest() : juce::UnitTest ("MASTEAL sanity") {}

    void runTest() override
    {
        beginTest ("harness runs");
        expectEquals (1 + 1, 2);

        beginTest ("audio buffer basics");
        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        expectEquals (buffer.getNumChannels(), 2);
        expectEquals (buffer.getNumSamples(), 512);
        expectEquals (buffer.getReadPointer (0)[0], 0.0f);
    }
};

static SanityTest sanityTest;

//==============================================================================
int main (int argc, char** argv)
{
    juce::ignoreUnused (argc, argv);
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::UnitTestRunner runner;
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (auto* result = runner.getResult (i))
            failures += result->failures;

    return failures > 0 ? 1 : 0;
}
