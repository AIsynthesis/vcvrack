#pragma once
#include <cmath>
#include <cstdint>

// Gate pulse width in samples (~5 ms at 48 kHz).
static constexpr uint32_t kGatePulseSamples = 240u;

// Scale factor for trigger probability.
// Raised to compensate for the cubic curve that suppresses mid/low levels.
// Reduced 60% (0.15 -> 0.06) to cut overall gate firing rate.
static constexpr float kGeigerScale = 0.06f;

// Exponent for probability curve. >1 makes gates fire far less often at
// moderate levels and burst rapidly only when the signal is especially hot.
// exp=3: signal 0.1→prob 0.000015 (near-silent); 0.5→prob 0.0019; 1.0→prob 0.15.
static constexpr float kGeigerExp = 3.0f;

// LFO-mode dampening: multiply signalLevel by this before the Geiger calculation
// so gates are essentially silent when the generator is running as an LFO.
// Combined with the cubic curve: (0.02)^3 = 8e-6, making gates vanishingly rare.
static constexpr float kGeigerLfoDamp = 0.02f;

// Per-gate state: one instance for each of the two output channels.
struct GeigerState {
    uint32_t rngState = 0u;   // LCG state — seed differently for each instance
    uint32_t counter  = 0u;   // samples remaining in current pulse (0 = idle)
};

// Advances one Geiger gate by one audio block.
// Returns true if the gate output should be HIGH after this block.
//
// signalLevel:  absolute value of the processed audio output (0–1)
// blockSize:    number of samples in the current block (e.g. 48)
inline bool processGeigerGate(GeigerState& state,
                               float signalLevel,
                               size_t blockSize) {
    // Tick down remaining pulse time.
    if(state.counter > 0) {
        state.counter = (state.counter > static_cast<uint32_t>(blockSize))
                        ? state.counter - static_cast<uint32_t>(blockSize)
                        : 0u;
    }

    // Only consider a new trigger when the gate is idle.
    if(state.counter == 0) {
        // Cubic curve: sparse at low levels, bursts only when signal is hot.
        const float probability = powf(signalLevel, kGeigerExp) * kGeigerScale;
        // LCG advance (Knuth, TAOCP vol. 2)
        state.rngState = state.rngState * 1664525u + 1013904223u;
        const float rand01 = static_cast<float>(state.rngState >> 9)
                             / static_cast<float>(1u << 23);
        if(rand01 < probability) {
            state.counter = kGatePulseSamples;
        }
    }

    return state.counter > 0;
}
