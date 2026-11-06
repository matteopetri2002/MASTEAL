#include "LoudnessMeter.h"

#include <algorithm>
#include <cstdint>

namespace masteal
{

//==============================================================================
// K-weighting: BS.1770-4 stage filters derived through the standard bilinear
// transform design so they are correct at ANY sample rate (48 k coefficients
// in the spec appendix are one instantiation of this formula).

LoudnessMeter::Biquad LoudnessMeter::makeHighShelf (double sampleRate)
{
    // Stage 1: high shelf +4 dB above ~1682 Hz (BS.1770-4 Annex 2).
    const double db = 3.999843853973347;
    const double f0 = 1681.974450955533;
    const double Q  = 0.7071752369554196;
    const double K  = std::tan (3.14159265358979 * f0 / sampleRate);

    const double Vh = std::pow (10.0, db / 20.0);
    const double Vb = std::pow (Vh, 0.4996667768471624);  // 10^(db/20 * 0.4997...)

    const double a0 = 1.0 + K / Q + K * K;
    const double b0 = (Vh + Vb * K / Q + K * K) / a0;
    const double b1 = (2.0 * (K * K - Vh)) / a0;
    const double b2 = (Vh - Vb * K / Q + K * K) / a0;
    const double a1 = (2.0 * (K * K - 1.0)) / a0;
    const double a2 = (1.0 - K / Q + K * K) / a0;

    return { b0, b1, b2, a1, a2 };
}

LoudnessMeter::Biquad LoudnessMeter::makeSteepHighPass (double sampleRate)
{
    // Stage 2: steep high pass ~38 Hz (BS.1770-4 Annex 2).
    const double f0 = 38.13547083543433;

    const double K  = std::tan (3.14159265358979 * f0 / sampleRate);
    const double Q  = 0.5003270373238853;
    const double a0 = 1.0 + K / Q + K * K;

    Biquad f;
    f.b0 = 1.0;
    f.b1 = -2.0;
    f.b2 = 1.0;
    f.a1 = (2.0 * (K * K - 1.0)) / a0;
    f.a2 = (1.0 - K / Q + K * K) / a0;
    return f;
}

//==============================================================================
void LoudnessMeter::prepare (double newSampleRate)
{
    sampleRate = std::max (8000.0, newSampleRate);
    hopSize = std::max (64, (int) std::lround (sampleRate / 10.0));  // 100 ms hops

    filters[0] = makeHighShelf (sampleRate);
    filters[1] = makeSteepHighPass (sampleRate);
    filters[2] = makeHighShelf (sampleRate);
    filters[3] = makeSteepHighPass (sampleRate);

    reset();
}

void LoudnessMeter::reset()
{
    for (auto& f : filters)
        f = { f.b0, f.b1, f.b2, f.a1, f.a2, 0.0, 0.0, 0.0, 0.0 };

    hopFill = 0;
    hopHistory.clear();
    hopAccumulator = { 0.0, 0.0 };

    momentaryWindowSum = 0.0;
    shortWindowSum = 0.0;
    momentaryHopsUsed = 0;
    shortHopsUsed = 0;
    shortRingIndex = 0;
    momentaryRingIndex = 0;
    shortRing.fill (0.0);
    momentaryRing.fill (0.0);

    momentaryLufs = kSilenceLufs;
    shortTermLufs = kSilenceLufs;
    integratedLufs = kSilenceLufs;
    integratedValid = false;
}

//==============================================================================
static double loudnessOfZ (double z) noexcept
{
    // -0.691 + 10*log10(z); numerical floor far below the -70 gate.
    if (z <= 1e-30)
        return -300.0;
    return -0.691 + 10.0 * std::log10 (z);
}

void LoudnessMeter::process (const float* left, const float* right, int numSamples)
{
    if (hopSize <= 0)
        return;

    auto* fLs = &filters[0];
    auto* fLh = &filters[1];
    auto* fRs = &filters[2];
    auto* fRh = &filters[3];

    auto& accL = hopAccumulator[0];
    auto& accR = hopAccumulator[1];

    for (int i = 0; i < numSamples; ++i)
    {
        const double xl = (double) left[i];
        const double xr = (double) right[i];

        const double yl = fLh->step (fLs->step (xl));
        const double yr = fRh->step (fRs->step (xr));

        accL += yl * yl;
        accR += yr * yr;

        if (++hopFill >= hopSize)
        {
            hopFill = 0;
            finalizeHop();
        }
    }

    fLs->flushDenormals();
    fLh->flushDenormals();
    fRs->flushDenormals();
    fRh->flushDenormals();
}

//==============================================================================
void LoudnessMeter::finalizeHop()
{
    const double zHop = (hopAccumulator[0] + hopAccumulator[1]) / (double) hopSize;
    hopAccumulator[0] = 0.0;
    hopAccumulator[1] = 0.0;

    //------------------------------------------------------------------
    // Sliding windows (average over the hops present in the window).
    if (momentaryHopsUsed < kHopsPerMomentary)
        momentaryHopsUsed++;
    momentaryWindowSum -= momentaryRing[(size_t) momentaryRingIndex];
    momentaryRing[(size_t) momentaryRingIndex] = zHop;
    momentaryWindowSum += zHop;
    momentaryRingIndex = (momentaryRingIndex + 1) % kHopsPerMomentary;

    momentaryLufs = loudnessOfZ (momentaryWindowSum / (double) momentaryHopsUsed);

    if (shortHopsUsed < kHopsPerShort)
        shortHopsUsed++;
    shortWindowSum -= shortRing[(size_t) shortRingIndex];
    shortRing[(size_t) shortRingIndex] = zHop;
    shortWindowSum += zHop;
    shortRingIndex = (shortRingIndex + 1) % kHopsPerShort;

    shortTermLufs = loudnessOfZ (shortWindowSum / (double) shortHopsUsed);

    //------------------------------------------------------------------
    // History for the gate (bounded: ~4 h of metering).
    if (hopHistory.size() >= kMaxHops)
        hopHistory.erase (hopHistory.begin());
    hopHistory.push_back (zHop);

    if (hopHistory.size() < kHopsPerMomentary)
        return;   // no full 400 ms gate block yet

    //------------------------------------------------------------------
    // BS.1770-4 integrated gate over 400 ms blocks, 75 % overlap (100 ms hop).
    const auto n = hopHistory.size();
    const size_t blockHops = kHopsPerMomentary;

    // Pass 1: blocks above the absolute gate.
    double ungatedSum = 0.0;
    int ungatedCount = 0;
    for (size_t start = 0; start + blockHops <= n; ++start)
    {
        double z = 0.0;
        for (size_t h = start; h < start + blockHops; ++h)
            z += hopHistory[h];
        z /= (double) blockHops;

        if (loudnessOfZ (z) > kAbsoluteGateLufs)
        {
            ungatedSum += z;
            ungatedCount++;
        }
    }

    if (ungatedCount == 0)
        return;   // whole signal so far is under the absolute gate

    const double ungatedLoudness = loudnessOfZ (ungatedSum / (double) ungatedCount);

    // Pass 2: blocks above (ungated loudness - 10 LU).
    double keptSum = 0.0;
    int keptCount = 0;
    for (size_t start = 0; start + blockHops <= n; ++start)
    {
        double z = 0.0;
        for (size_t h = start; h < start + blockHops; ++h)
            z += hopHistory[h];
        z /= (double) blockHops;

        if (loudnessOfZ (z) > kAbsoluteGateLufs
            && loudnessOfZ (z) > ungatedLoudness - kRelativeGateLu)
        {
            keptSum += z;
            keptCount++;
        }
    }

    if (keptCount == 0)
        return;

    integratedLufs = loudnessOfZ (keptSum / (double) keptCount);
    integratedValid = true;
}

} // namespace masteal
