#pragma once
#include <cmath>

// Tube triode character at low drive → chaotic wavefold at high drive.
//
// Drive 0.0–0.5: asymmetric tanh stage. A pre-gain of (input + k·input·|input|)
//   introduces even harmonics (triode character) alongside odd ones. At low gain
//   the response is nearly linear; increasing gain pushes toward warm saturation.
// Drive 0.5–1.0: tube stage continues; sine wavefold blends in progressively.
//   The tube output is amplified and passed through sin(foldIn · π/2). At drive=1.0
//   the signal folds multiple times, producing complex chaotic harmonics.
//
// Output is NOT self-normalised at high fold settings — the stage downstream in
// main.cpp hard-clips to [−1, 1].

static constexpr float kDistTubeMaxGain = 40.0f;        // gain at drive=1.0
static constexpr float kDistAsymmetry   = 0.3f;         // even-harmonic bias (triode)
static constexpr float kDistFoldDepth   = 4.5f;         // fold over-drive multiplier at max (5.0 lands on sin zero at saturation)
static constexpr float kDistHalfPi     = 1.5707963268f; // π/2

// ── Bass boost pre-stage ──────────────────────────────────────────────────────
//
// One-pole low-shelf applied before the tube+fold stage. Boosted low frequencies
// get pushed harder into saturation, adding body and warmth at higher drive —
// mirrors the low-end emphasis of a tube amp tone stack.
//
// kBassBoostA: one-pole LP coefficient. Shelf frequency ≈ fc = (1−a)·fs/(2π).
//   a = 0.9607 → fc ≈ 300 Hz at 48 kHz.  Raise a to lower the shelf frequency.
// kBassBoostMax: peak shelf gain at drive=1.0 (additive: 0.7 → +≈5 dB at DC).

static constexpr float kBassBoostA   = 0.9607f;  // LP pole (~300 Hz at 48 kHz)
static constexpr float kBassBoostMax = 0.7f;     // max additive shelf gain at drive=1.0

struct BassBoostState {
    float lp = 0.0f;
};

// Call this on the signal BEFORE applyDistortion.
// Boosted output is intentionally allowed to exceed ±1 — applyDistortion clips it.
inline float applyBassBoost(float input, float drive, BassBoostState& state) {
    state.lp = kBassBoostA * state.lp + (1.0f - kBassBoostA) * input;
    return input + drive * kBassBoostMax * state.lp;
}

// driveKnob: 0–1 (0 = barely linear, 1 = full chaotic fold)
inline float applyDistortion(float input, float driveKnob) {
    // Gain scales linearly from 1× to kDistTubeMaxGain across full drive range.
    const float gain = 1.0f + driveKnob * (kDistTubeMaxGain - 1.0f);

    // Tube pre-stage: asymmetric shape adds even harmonics.
    const float asymIn = input + kDistAsymmetry * input * fabsf(input);
    // Normalise so peak output ≈ 1.0 regardless of drive level.
    const float norm   = tanhf(gain * (1.0f + kDistAsymmetry));
    const float tube   = tanhf(asymIn * gain) / norm;

    // Wavefold amount: 0 below drive=0.5, ramps to 1.0 at drive=1.0.
    const float foldAmt = driveKnob < 0.5f ? 0.0f : (driveKnob - 0.5f) * 2.0f;

    // Sine wavefold: amplify tube output then wrap through sin(·π/2).
    const float foldIn  = tube * (1.0f + foldAmt * kDistFoldDepth);
    const float folded  = sinf(foldIn * kDistHalfPi);

    // Blend: at foldAmt=0 output is pure tube; at foldAmt=1 it's fully folded.
    return tube + foldAmt * (folded - tube);
}
