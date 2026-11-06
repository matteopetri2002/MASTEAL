#pragma once

#include <array>
#include <cmath>

namespace masteal
{

//==============================================================================
/**
 True peak meter per ITU-R BS.1770-4: 4x oversampling through a polyphase
 windowed-sinc FIR, then the max of the interpolated samples is the running
 true peak (peak hold, no auto-reset).

 Polyphase model: for input sample n and phase p (0..3),
     y[4n+p] = sum_{j=0..11} kernel[4j+p] * xLow[n-j]
 (xLow past a chunk boundary comes from a 12-sample ring; before the first
 sample the "history" is zeros, i.e. proper streaming state).

 Pure logic (AGENTS.md): no JUCE, no allocations after prepare(), one owner
 thread at a time (MASTEAL: the analysis thread; tests call it offline).
*/
class TruePeakMeter
{
public:
    static constexpr int kOversample = 4;
    static constexpr int kTapsPerPhase = 12;                          // low-rate span
    static constexpr int kKernelTaps = kOversample * kTapsPerPhase;   // 48 taps, high rate
    static constexpr double kSilenceDbtp = -300.0;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        buildKernel();
        reset();
    }

    void reset() noexcept
    {
        historyL.fill (0.0);
        historyR.fill (0.0);
        writeIndex = 0;
        runMax = 0.0;
        dbtp = kSilenceDbtp;
    }

    /** Feeds a chunk of audio; the peak hold covers everything fed since reset(). */
    void process (const float* left, const float* right, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const double xl0 = (double) left[n];
            const double xr0 = (double) right[n];

            for (int p = 0; p < kOversample; ++p)
            {
                double yL = 0.0;
                double yR = 0.0;

                for (int j = 0; j < kTapsPerPhase; ++j)
                {
                    const double kv = kernel[(size_t) (4 * j + p)];
                    if (j == 0)
                    {
                        yL += kv * xl0;
                        yR += kv * xr0;
                    }
                    else
                    {
                        const size_t slot = (writeIndex - (size_t) j) & kHistoryMask;
                        yL += kv * historyL[slot];
                        yR += kv * historyR[slot];
                    }
                }

                const double aL = std::fabs (yL);
                const double aR = std::fabs (yR);
                if (aL > runMax) runMax = aL;
                if (aR > runMax) runMax = aR;
            }

            // Push the consumed sample into the ring.
            const size_t slot = writeIndex & kHistoryMask;
            historyL[slot] = xl0;
            historyR[slot] = xr0;
            ++writeIndex;
        }

        dbtp = (runMax > 0.0) ? 20.0 * std::log10 (runMax) : kSilenceDbtp;
    }

    float getTruePeakDbtp() const noexcept { return (float) dbtp; }
    double getTruePeakLinear() const noexcept { return runMax; }
    double getSampleRate() const noexcept { return sampleRate_; }

private:
    void buildKernel() noexcept
    {
        // Windowed-sinc interpolation kernel at the HIGH rate; cutoff = the LOW
        // rate Nyquist, Blackman window. Normalized so overall DC gain == 1
        // (in-phase samples are reconstructed, not scaled).
        constexpr double pi = 3.14159265358979323846;
        const double cut = 0.5 / (double) kOversample;               // cycles per high sample
        const double centre = 0.5 * (double) (kKernelTaps - 1);

        double sum = 0.0;
        for (int k = 0; k < kKernelTaps; ++k)
        {
            const double t = (double) k - centre;
            const double sinc = (std::fabs (t) < 1e-9)
                ? 2.0 * cut
                : std::sin (2.0 * pi * cut * t) / (pi * t);

            const double x = (double) k / (double) (kKernelTaps - 1);
            const double blackman = 0.42 - 0.50 * std::cos (2.0 * pi * x)
                                          + 0.08 * std::cos (4.0 * pi * x);

            kernel[(size_t) k] = sinc * blackman;
            sum += kernel[(size_t) k];
        }

        // Normalization: for interpolation each phase's DC gain must be ~1,
        // so the total sum over all 48 taps equals the oversampling factor.
        const double norm = sum > 1e-12 ? (double) kOversample / sum : 1.0;
        for (auto& v : kernel)
            v *= norm;
    }

    static constexpr size_t kHistoryLen = 16;             // >= kTapsPerPhase, power of 2
    static constexpr size_t kHistoryMask = kHistoryLen - 1;

    double sampleRate_ { 48000.0 };
    std::array<double, kKernelTaps> kernel {};
    std::array<double, kHistoryLen> historyL {};
    std::array<double, kHistoryLen> historyR {};
    size_t writeIndex { 0 };
    double runMax { 0.0 };
    double dbtp { kSilenceDbtp };
};

} // namespace masteal
