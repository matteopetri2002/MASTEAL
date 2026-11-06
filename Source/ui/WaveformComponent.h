#pragma once

#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../reference/RefTransport.h"

namespace masteal
{

//==============================================================================
/** Waveform of the loaded reference with:
      - region highlight (analysis region; drag on the waveform sets it,
        double-click resets it to the whole clip);
      - a moving playhead line while playback runs.

    Pure adapter (ARCHITECTURE.md): reads a RefTransport snapshot, writes
    region/transport commands only. No audio-thread access.
*/
class WaveformComponent : public juce::Component
{
public:
    explicit WaveformComponent (RefTransport& t);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    juce::Rectangle<int> getWaveArea() const;
    int sampleToX (juce::int64 sample) const;
    juce::int64 xToSample (int x) const;
    void drawEmptyState (juce::Graphics& g);
    void refreshSnapshot();     // rebuild the draw cache if the clip changed

    RefTransport& transport;
    std::shared_ptr<RefClip> snapshot;    // message-thread draw cache
    juce::String snapshotName;
    bool dragging { false };
    juce::int64 dragAnchorSamples { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformComponent)
};

} // namespace masteal
