#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace masteal
{

//==============================================================================
/**
 K-weighting loudness meter per ITU-R BS.1770-4 (revision 4 gating).

 Pure logic (AGENTS.md): audio samples in, measurements out — no JUCE header,
 no I/O, no GUI, no allocations after prepare(). Callable from any thread as
 long as a single thread owns the instance at a time (MASTEAL: the analysis
 thread; tests call it offline from the test thread).

 Metering model (BS.1770-4):
 - K-weighting: stage 1 = high shelf (+4 dB @ ~1682 Hz), stage 2 = steep HPF
   (~38 Hz), coefficients derived via bilinear transform at any sample rate.
 - Blocks: 100 ms hops of mean-square filtered energy ("mean squared volume");
   momentary  = last 4  hops (400 ms),
   short-term = last 30 hops (3 s),
   integrated = BS.1770-4 dual gate over 400 ms blocks with 75 % overlap:
                absolute gate -70 LUFS, then relative gate -10 LU re ungated
                loudness. Silence is naturally excluded by the absolute gate.
 - Stereo → z = meanSq(L) + meanSq(R); loudness = -0.691 + 10*log10(z).
*/
class LoudnessMeter
{
public:
    static constexpr int kNumChannels = 2;
    static constexpr int kHopsPerMomentary = 4;    // 400 ms @ 100 ms hops
    static constexpr int kHopsPerShort = 30;       // 3 s @ 100 ms hops
    static constexpr double kOffsetDb = -0.691;    // stereo with G_L = G_R = 1
    static constexpr double kAbsoluteGateLufs = -70.0;
    static constexpr double kRelativeGateLu = 10.0;

    /** Builds filter coefficients and window state. Allocation happens here,
        off the audio thread (MASTEAL: analysis thread setup in prepareToPlay). */
    void prepare (double sampleRate);
    void reset();

    /** Feeds interleaved-nothing: two pre-decoded channels (may be the same
        pointer for mono). Any chunk size; windows update on 100 ms hops. */
    void process (const float* left, const float* right, int numSamples);

    float getMomentaryLufs()  const { return (float) momentaryLufs; }
    float getShortTermLufs()  const { return (float) shortTermLufs; }
    float getIntegratedLufs() const { return (float) integratedLufs; }
    bool  hasIntegrated()     const { return integratedValid; }

    double getSampleRate() const noexcept { return sampleRate; }
    int    getHopSize()    const noexcept { return hopSize; }
    int    getCompletedHops() const noexcept { return (int) hopHistory.size(); }

private:
    //==========================================================================
    /** Second-order section (DF1, double precision: the meter must be
        repeatable to better than 0.01 LU in the conformance tests). */
    struct Biquad
    {
        double b0 { 1.0 }, b1 { 0.0 }, b2 { 0.0 }, a1 { 0.0 }, a2 { 0.0 };
        double x1 { 0.0 }, x2 { 0.0 }, y1 { 0.0 }, y2 { 0.0 };

        inline double step (double x) noexcept
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }

        inline void flushDenormals() noexcept
        {
            if (std::fabs (x1) < 1e-24) x1 = 0.0;
            if (std::fabs (x2) < 1e-24) x2 = 0.0;
            if (std::fabs (y1) < 1e-24) y1 = 0.0;
            if (std::fabs (y2) < 1e-24) y2 = 0.0;
        }
    };

    static Biquad makeHighShelf (double sampleRate);
    static Biquad makeSteepHighPass (double sampleRate);

    void finalizeHop();

    //==========================================================================
    double sampleRate { 48000.0 };
    int    hopSize { 4800 };
    int    hopFill { 0 };

    std::array<Biquad, 4> filters {};   // L: shelf+hp, R: shelf+hp  (0,1 = L; 2,3 = R)

    std::array<double, kNumChannels> hopAccumulator {};   // filtered^2 sum of the open hop
    /** z per hop = meanSq(L) + meanSq(R) for every completed hop.
        Kept for the gate: BS.1770-4 integrates over the whole signal. */
    std::vector<double> hopHistory;
    static constexpr size_t kMaxHops = 144000;   // ~4 h cap: gate memory bound

    // Sliding window sums (momentary: 4 hops, short: 30 hops), updated at hop ends.
    double momentaryWindowSum { 0.0 };
    double shortWindowSum { 0.0 };
    int momentaryHopsUsed { 0 };
    int shortHopsUsed { 0 };
    std::array<double, kHopsPerShort> shortRing {};
    int shortRingIndex { 0 };

    std::array<double, kHopsPerMomentary> momentaryRing {};
    int momentaryRingIndex { 0 };

    double momentaryLufs { kSilenceLufs };
    double shortTermLufs { kSilenceLufs };
    double integratedLufs { kSilenceLufs };
    bool   integratedValid { false };

    static constexpr double kSilenceLufs = -300.0;   // internal floor before the first hop
};

} // namespace masteal
