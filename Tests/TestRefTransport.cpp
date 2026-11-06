#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/Processor.h"

namespace
{
//==============================================================================
/** Writes a synthetic stereo WAV file for loading tests. */
juce::File writeTestWav (const juce::File& file, juce::AudioBuffer<float>& content,
                         int channels)
{
    std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
    jassert (out != nullptr);

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (out.release(), 44100.0, (unsigned int) channels, 16, {}, 0));
    jassert (writer != nullptr);

    writer->writeFromAudioSampleBuffer (content, 0, content.getNumSamples());
    writer->flush();
    writer.reset();
    return file;
}

/** Generates a sine into ch0 and a scaled sine into ch1. */
void fillSine (juce::AudioBuffer<float>& buffer, float freq, float rightScale)
{
    const double sr = 44100.0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float scale = ch == 0 ? 1.0f : rightScale;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (ch, i,
                scale * std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / sr));
    }
}

juce::AudioParameterBool* boolParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
{
    return dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (id));
}
}

//==============================================================================
/** RefTransport contract tests (issue #3):
      - WAV loading (stereo + mono copied to stereo), peaks, region defaults;
      - error path on non-existing file;
      - region editing and playback state;
      - processor A/B: reference replaces the input beyond the 10 ms ramp;
      - processor bypass: bit-exact passthrough.
*/
class TestRefTransport : public juce::UnitTest
{
public:
    TestRefTransport() : juce::UnitTest ("MASTEAL RefTransport") {}

    void runTest() override
    {
        using namespace masteal;

        beginTest ("stereo wav loads, peaks + region built");
        {
            juce::TemporaryFile temp (".wav");
            juce::AudioBuffer<float> content (2, 44100);
            fillSine (content, 220.0f, 0.9f);
            const auto file = writeTestWav (temp.getFile(), content, 2);

            RefTransport trans;
            trans.loadFileSync (file);
            expect (trans.hasClip());
            expect (! trans.isLoading());
            expect (! trans.isPlaying());
            expectEquals (trans.getClipName(), file.getFileName());

            auto* clip = trans.getClipForProcessing();
            expect (clip != nullptr);
            expectEquals (clip->buffer.getNumChannels(), 2);
            expectEquals (clip->buffer.getNumSamples(), 44100);
            expectWithinAbsoluteError (clip->sampleRate, 44100.0, 0.5);
            expectEquals (clip->regionStart, (juce::int64) 0);
            expectEquals (clip->regionEnd, (juce::int64) 44100);
            // peaks: 44100 / (0.25 s = 11025) = 4 buckets
            expectEquals ((int) clip->peaks.size(), 4);
            expect (clip->peaks[0] > 0.9f && clip->peaks[0] <= 1.0f);
        }

        beginTest ("mono wav loads duplicated to stereo");
        {
            juce::TemporaryFile temp (".wav");
            juce::AudioBuffer<float> content (1, 22050);
            fillSine (content, 440.0f, 1.0f);
            const auto file = writeTestWav (temp.getFile(), content, 1);

            RefTransport trans;
            trans.loadFileSync (file);

            auto* clip = trans.getClipForProcessing();
            expect (clip != nullptr);
            expectEquals (clip->buffer.getNumChannels(), 2);
            expectEquals (clip->buffer.getNumSamples(), 22050);
            expectWithinAbsoluteError (clip->buffer.getSample (0, 100),
                                       clip->buffer.getSample (1, 100), 1e-4f);
        }

        beginTest ("missing file reports an error");
        {
            RefTransport trans;
            trans.loadFileSync (juce::File ("/tmp/masteal-no-such-file.wav"));
            expect (! trans.hasClip());
            expect (trans.getLoadErrorText().isNotEmpty());
        }

        beginTest ("region editing");
        {
            juce::TemporaryFile temp (".wav");
            juce::AudioBuffer<float> content (2, 44100);
            fillSine (content, 220.0f, 0.9f);
            const auto file = writeTestWav (temp.getFile(), content, 2);

            RefTransport trans;
            trans.loadFileSync (file);

            trans.setRegion (1000, 21000);
            expectEquals (trans.getRegionStartSamples(), (juce::int64) 1000);
            expectEquals (trans.getRegionLengthSamples(), (juce::int64) 20000);

            trans.setLooping (false);
            trans.startPlayback();
            expect (trans.isPlaying());
            expectEquals (trans.getPlayPositionSamples(), (juce::int64) 1000);
            trans.stopPlayback();
            expect (! trans.isPlaying());

            trans.resetRegion();
            expectEquals (trans.getRegionStartSamples(), (juce::int64) 0);
            expectEquals (trans.getRegionLengthSamples(), (juce::int64) 44100);
        }

        beginTest ("processor A/B: reference replaces input after the 10 ms ramp");
        {
            juce::TemporaryFile temp (".wav");
            juce::AudioBuffer<float> content (2, 44100);
            fillSine (content, 220.0f, 0.9f);
            const auto file = writeTestWav (temp.getFile(), content, 2);

            std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
            auto* mast = dynamic_cast<masteal::Processor*> (proc.get());
            expect (mast != nullptr);

            proc->prepareToPlay (44100.0, 512);

            auto& apvts = mast->getValueTreeState();
            boolParam (apvts, "bypass")->setValueNotifyingHost (0.0f);
            boolParam (apvts, "abMonitor")->setValueNotifyingHost (1.0f);

            auto& trans = mast->getRefTransport();
            trans.loadFileSync (file);
            trans.setLooping (true);
            trans.startPlayback();
            expect (trans.hasClip());
            expect (trans.isPlaying());

            // Silent input: whatever reaches the output beyond the ramp is the ref.
            juce::AudioBuffer<float> buffer (2, 1024);
            juce::MidiBuffer midi;
            buffer.clear();
            proc->processBlock (buffer, midi);

            const int ramp = (int) (0.010f * 44100.0f); // 10 ms ramp length in samples
            // Before the ramp the input (silent) still dominates mixed with the fade.
            expect (std::abs (buffer.getSample (0, 1)) <
                    std::abs (trans.getClipForProcessing()->buffer.getSample (0, 1)));
            // After the ramp the output must be exactly the reference.
            expect (ramp + 1 < 1024);
            expectWithinAbsoluteError (buffer.getSample (0, ramp),
                                       trans.getClipForProcessing()->buffer.getSample (0, ramp),
                                       1e-5f);
            expectWithinAbsoluteError (buffer.getSample (1, 800),
                                       trans.getClipForProcessing()->buffer.getSample (1, 800),
                                       1e-5f);

            // Playhead advanced and stays inside the clip.
            const auto pos = trans.getPlayPositionSamples();
            expect (pos >= 1024 && pos <= 44100);
        }

        beginTest ("processor bypass: output is bit-exact input");
        {
            juce::TemporaryFile temp (".wav");
            juce::AudioBuffer<float> content (2, 44100);
            fillSine (content, 220.0f, 0.9f);
            const auto file = writeTestWav (temp.getFile(), content, 2);

            std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
            auto* mast = dynamic_cast<masteal::Processor*> (proc.get());
            expect (mast != nullptr);
            proc->prepareToPlay (44100.0, 512);

            auto& apvts = mast->getValueTreeState();
            boolParam (apvts, "bypass")->setValueNotifyingHost (1.0f);
            boolParam (apvts, "abMonitor")->setValueNotifyingHost (1.0f);

            auto& trans = mast->getRefTransport();
            trans.loadFileSync (file);
            trans.startPlayback();

            juce::AudioBuffer<float> input (2, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    input.setSample (ch, i, 0.25f * (float) (i % 7));

            juce::AudioBuffer<float> expected (2, 512);
            expected.makeCopyOf (input);

            juce::MidiBuffer midi;
            proc->processBlock (input, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    expectEquals (input.getSample (ch, i), expected.getSample (ch, i));
        }
    }
};

static TestRefTransport testRefTransport;
