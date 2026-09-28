#pragma once
#include <cmath>
#include <cstdint>

// Ten waveform shapes for the generator.
// Mode numbering matches spec §Generator.
enum class WaveformMode {
    kSine,       // 1
    kTriangle,   // 2
    kRampUp,     // 3
    kRampDown,   // 4
    kSquare,     // 5
    kSineUp,     // 6: positive half-wave
    kSineDown,   // 7: negative half-wave
    kExpUp,      // 8: exponential rise −1→+1
    kExpDown,    // 9: exponential fall +1→−1
    kSampleHold, // 10: random value held per period
};

static constexpr int   kWaveModeCount  = 10;
static constexpr float kTwoPi          = 6.28318530718f;
static constexpr float kExpBase        = 100.0f; // curvature for exp waveforms
static constexpr float kLfoFreqMin     = 0.01f;
static constexpr float kLfoFreqMax     = 50.0f;
static constexpr float kOscFreqMin     = 0.01f;
static constexpr float kOscFreqMax     = 20000.0f;
static constexpr float kDefaultGenFreq = 440.0f;

// Maps a 0–1 normalised value to one of the ten waveform modes (stepped).
inline WaveformMode knobToWaveformMode(float knob) {
    int step = static_cast<int>(knob * kWaveModeCount);
    if(step >= kWaveModeCount) step = kWaveModeCount - 1;
    return static_cast<WaveformMode>(step);
}

struct Generator {
    float    phase    = 0.f;    // 0.0 to 1.0 per period
    float    phaseInc = 0.f;    // advance per sample
    float    shValue  = 0.f;    // held value for Sample & Hold

    void setFreq(float freq, float sampleRate) {
        phaseInc = freq / sampleRate;
    }

    // Returns next sample and advances phase.
    // rngState is advanced only when S&H mode is active and period wraps.
    float process(WaveformMode mode, uint32_t& rngState) {
        float result = computeSample(mode);

        phase += phaseInc;
        if(phase >= 1.0f) {
            phase -= 1.0f;
            // New random value ready for the next S&H period.
            rngState  = rngState * 1664525u + 1013904223u;
            shValue   = static_cast<float>(rngState >> 9)
                        / static_cast<float>(1u << 23) * 2.0f - 1.0f;
        }
        return result;
    }

private:
    float computeSample(WaveformMode mode) const {
        const float p = phase; // 0–1
        switch(mode) {
            case WaveformMode::kSine:
                return sinf(p * kTwoPi);

            case WaveformMode::kTriangle:
                // Linear rise then fall: −1 at 0, +1 at 0.5, −1 at 1
                return 1.0f - 4.0f * fabsf(p - 0.5f);

            case WaveformMode::kRampUp:
                return 2.0f * p - 1.0f;

            case WaveformMode::kRampDown:
                return 1.0f - 2.0f * p;

            case WaveformMode::kSquare:
                return p < 0.5f ? 1.0f : -1.0f;

            case WaveformMode::kSineUp:
                // Positive half of sine; zero during negative half
                return fmaxf(sinf(p * kTwoPi), 0.0f);

            case WaveformMode::kSineDown:
                // Negative half of sine; zero during positive half
                return fminf(sinf(p * kTwoPi), 0.0f);

            case WaveformMode::kExpUp: {
                // Slow start, fast finish: −1 at phase=0, +1 at phase=1
                const float norm = (powf(kExpBase, p) - 1.0f) / (kExpBase - 1.0f);
                return norm * 2.0f - 1.0f;
            }

            case WaveformMode::kExpDown: {
                // Mirror of ExpUp: +1 at phase=0, −1 at phase=1
                const float norm = (powf(kExpBase, p) - 1.0f) / (kExpBase - 1.0f);
                return 1.0f - norm * 2.0f;
            }

            case WaveformMode::kSampleHold:
                return shValue; // updated on phase wrap in process()

            default:
                return sinf(p * kTwoPi);
        }
    }
};
