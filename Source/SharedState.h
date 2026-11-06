#pragma once

#include <atomic>

namespace masteal
{

//==============================================================================
/** Lock-free state shared between the audio thread and the UI (~30 Hz).

    Rules (see AGENTS.md and ARCHITECTURE.md "Stato"):
    - plain std::atomic members only: no locks, no allocations, no JUCE types;
    - the audio thread publishes measurements, the UI reads them on a timer;
    - the UI never calls DSP directly: commands flow through parameters (APVTS)
      or through the atomic counters below.

    Measurement fields are placeholders until issues #4-#6 add the real meters.
*/
struct SharedState
{
    SharedState() = default;
    SharedState (const SharedState&) = delete;
    SharedState& operator= (const SharedState&) = delete;

    static constexpr float kSilenceLufs = -70.0f;
    static constexpr float kSilenceDbtp = -70.0f;

    // --- measurements: input channel (audio -> UI) ----------------------------
    std::atomic<float> inputLufsMomentary   { kSilenceLufs };
    std::atomic<float> inputLufsShortTerm   { kSilenceLufs };
    std::atomic<float> inputLufsIntegrated  { kSilenceLufs };
    std::atomic<float> inputTruePeakDbtp    { kSilenceDbtp };

    // --- measurements: reference fingerprint (audio/import thread -> UI) ------
    std::atomic<float> refLufsIntegrated    { kSilenceLufs };
    std::atomic<float> refTruePeakDbtp      { kSilenceDbtp };
    std::atomic<bool>  referenceLoaded      { false };

    // --- transport / monitoring state (audio -> UI) ---------------------------
    std::atomic<bool>  abMonitorActive      { false };

    // --- commands (UI -> audio), monotonic counters ---------------------------
    std::atomic<int>   reloadFingerprintRequest { 0 };
};

} // namespace masteal
