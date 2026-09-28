#pragma once
#include <cmath>

// Nine ways to combine the two pre-gain signals before waveshaping.
// Mode numbering matches prd.md §Argument Stage.
enum class ArgumentMode {
    kA,           // 1: pass A, ignore B
    kB,           // 2: pass B, ignore A
    kAPlusB,      // 3: A + B  (default)
    kAMinusB,     // 4: A - B
    kBMinusA,     // 5: B - A
    kATimesB,     // 6: A·B / 10
    kEuclidean,   // 7: √(A² + B²)
    kADivAbsB,    // 8: A / |B|
    kA10DivAbsB,  // 9: 10A / |B|
};

static constexpr int   kArgModeCount    = 9;
static constexpr float kArgProductScale = 0.1f;    // prevents saturation in multiply mode
static constexpr float kArgDivGuard     = 0.0001f; // minimum |B| to block divide-by-zero
static constexpr float kArgDivBoost     = 10.0f;   // numerator scale for mode 9

// Maps a 0–1 knob value to one of the nine equally-spaced argument modes.
inline ArgumentMode knobToArgumentMode(float knob) {
    int step = static_cast<int>(knob * kArgModeCount);
    if (step >= kArgModeCount) step = kArgModeCount - 1;
    return static_cast<ArgumentMode>(step);
}

inline float applyArgument(ArgumentMode mode, float a, float b) {
    switch (mode) {
        case ArgumentMode::kA:         return a;
        case ArgumentMode::kB:         return b;
        case ArgumentMode::kAPlusB:    return a + b;
        case ArgumentMode::kAMinusB:   return a - b;
        case ArgumentMode::kBMinusA:   return b - a;
        case ArgumentMode::kATimesB:   return a * b * kArgProductScale;
        case ArgumentMode::kEuclidean: return sqrtf(a * a + b * b);
        case ArgumentMode::kADivAbsB: {
            float denom = fabsf(b);
            if (denom < kArgDivGuard) denom = kArgDivGuard;
            return a / denom;
        }
        case ArgumentMode::kA10DivAbsB: {
            float denom = fabsf(b);
            if (denom < kArgDivGuard) denom = kArgDivGuard;
            return kArgDivBoost * a / denom;
        }
        default: return a + b;
    }
}
