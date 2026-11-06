#include "WaveformComponent.h"

namespace masteal
{

//==============================================================================
WaveformComponent::WaveformComponent (RefTransport& t)
    : transport (t)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void WaveformComponent::refreshSnapshot()
{
    const auto name = transport.getClipName();
    if (name.isEmpty())
    {
        snapshot.reset();
        snapshotName.clear();
        return;
    }

    if (name != snapshotName || snapshot == nullptr)
    {
        if (const auto* clip = transport.getClipForProcessing())
        {
            snapshot = std::make_shared<RefClip> (*clip); // draw-only copy
            snapshotName = name;
        }
    }
}

//==============================================================================
void WaveformComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff14161a));

    const auto wave = getWaveArea();
    g.setColour (juce::Colour (0xff24272e));
    g.fillRoundedRectangle (wave.toFloat(), 4.0f);

    refreshSnapshot();

    if (snapshot == nullptr)
    {
        drawEmptyState (g);
        return;
    }

    // --- waveform bars from the peaks cache --------------------------------
    const auto* clip = snapshot.get();
    const int bucket = juce::jmax (1, clip->peakBucketSamples);
    const int numPeaks = (int) clip->peaks.size();
    const auto midY = wave.getCentreY();
    const float halfH = (float) wave.getHeight() * 0.5f - 1.0f;

    g.setColour (juce::Colour (0xff4fc3f7));
    for (int px = 0; px < wave.getWidth(); ++px)
    {
        const auto s0 = xToSample (wave.getX() + px);
        const auto s1 = juce::jmax<int64_t> (xToSample (wave.getX() + px + 1), s0 + 1);
        int b0 = (int) (s0 / bucket);
        int b1 = (int) ((s1 - 1) / bucket);
        b0 = juce::jlimit (0, juce::jmax (0, numPeaks - 1), b0);
        b1 = juce::jlimit (0, juce::jmax (0, numPeaks - 1), b1);

        float m = 0.0f;
        for (int b = b0; b <= b1; ++b)
            m = juce::jmax (m, clip->peaks[(size_t) b]);

        const float h = juce::jlimit (0.5f, halfH, m * halfH);
        g.drawVerticalLine (wave.getX() + px, midY - h, midY + h);
    }

    // --- region highlight from transport atomics (always fresh) ------------
    const auto regionStart = transport.getRegionStartSamples();
    const auto regionLen = transport.getRegionLengthSamples();
    if (regionLen > 0)
    {
        const int xr0 = sampleToX (regionStart);
        const int xr1 = sampleToX (regionStart + std::max<juce::int64> (regionLen, 1));
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<int> (xr0, wave.getY(),
                                          juce::jmax (1, xr1 - xr0), wave.getHeight()));
    }

    // --- playhead -----------------------------------------------------------
    const auto pos = transport.getPlayPositionSamples();
    if (pos >= 0 && pos < (juce::int64) snapshot->buffer.getNumSamples())
    {
        g.setColour (juce::Colours::orange);
        g.drawVerticalLine (sampleToX (pos), (float) wave.getY(), (float) wave.getBottom());
    }

    // --- info line ----------------------------------------------------------
    g.setColour (juce::Colours::whitesmoke);
    g.drawText (juce::String (clip->sampleRate, 0) + " Hz  -  "
                    + juce::String ((double) snapshot->buffer.getNumSamples() / clip->sampleRate / 60.0, 2)
                    + " min  -  drag to set region, double-click resets",
                getLocalBounds().removeFromBottom (20), juce::Justification::centred);
}

void WaveformComponent::drawEmptyState (juce::Graphics& g)
{
    const auto text = transport.isLoading()
        ? juce::String ("Loading reference...")
        : juce::String ("Drop or load a reference audio file (WAV / AIFF / FLAC / MP3)");
    g.setColour (juce::Colour (0xff8a8f9c));
    g.drawText (text, getWaveArea(), juce::Justification::centred);
}

//==============================================================================
void WaveformComponent::mouseDown (const juce::MouseEvent&)
{
    dragging = true;
    dragAnchorSamples = -1;
}

void WaveformComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging || snapshot == nullptr)
        return;

    const auto n = (juce::int64) snapshot->buffer.getNumSamples();
    const auto pos = juce::jlimit<int64_t> (0, juce::jmax<int64_t> (n - 1, 0), xToSample (e.x));

    if (dragAnchorSamples < 0)
        dragAnchorSamples = pos;

    const auto start = juce::jmin (dragAnchorSamples, pos);
    const auto end = juce::jmin (n, juce::jmax (dragAnchorSamples, pos) + 1);

    transport.setRegion (start, end); // message thread: syncs atomics + clip copy
}

void WaveformComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    dragging = false;
    dragAnchorSamples = -1;
    transport.resetRegion();
}

//==============================================================================
juce::Rectangle<int> WaveformComponent::getWaveArea() const
{
    return getLocalBounds().reduced (4).removeFromTop (getLocalBounds().reduced (4).getHeight() - 20);
}

int WaveformComponent::sampleToX (juce::int64 sample) const
{
    if (snapshot == nullptr || snapshot->buffer.getNumSamples() <= 0)
        return getX();

    const auto n = (juce::int64) snapshot->buffer.getNumSamples();
    const auto wave = getWaveArea();
    return wave.getX() + (int) juce::jlimit (0.0, (double) wave.getWidth(),
        juce::jlimit<int64_t> (0, n, sample) * (double) (wave.getWidth() - 1) / (double) n);
}

juce::int64 WaveformComponent::xToSample (int x) const
{
    if (snapshot == nullptr || snapshot->buffer.getNumSamples() <= 0)
        return 0;

    const auto n = (juce::int64) snapshot->buffer.getNumSamples();
    const auto wave = getWaveArea();
    const double t = juce::jlimit (0.0, 1.0, (x - wave.getX()) / (double) juce::jmax (1, wave.getWidth() - 1));
    return (juce::int64) (t * (double) n);
}

//==============================================================================
} // namespace masteal
