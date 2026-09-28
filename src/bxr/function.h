#pragma once
#include <cmath>

// Six waveshaping functions applied after the argument stage.
// Mode numbering matches prd.md §Function Stage.
enum class FunctionMode {
    kLogAbs,            // 1: ln|x|     — compresses loud signals toward zero  (CCW)
    kSqrtAbs,           // 2: √|x|      — gentle compression, always non-negative
    kPassthrough,       // 3: x         — no function applied
    kSquare,            // 4: x²        — adds even harmonics, always non-negative
    kDerivativeScaled,  // 5: −dx/dt × 100
    kDerivative,        // 6: −dx/dt    — aggressive high-frequency emphasis    (CW)
};

static constexpr int   kFuncModeCount   = 6;
static constexpr float kFuncLogGuard    = 1e-10f;  // prevents ln(0) = −∞
static constexpr float kFuncDeriveScale = 100.0f;  // prd specifies ×100 for mode 5

// Persistent state required by the derivative modes.
// Must be zeroed at startup (prd.md §Behaviour).
struct FunctionState {
    float prev = 0.f;
};

// Advances to the next function mode, wrapping around.
inline FunctionMode cycleFunctionMode(FunctionMode current) {
    return static_cast<FunctionMode>(
        (static_cast<int>(current) + 1) % kFuncModeCount);
}

// Applies the selected function and advances the derivative delay line.
inline float applyFunction(FunctionMode mode, float x, FunctionState& state) {
    float result;
    switch (mode) {
        case FunctionMode::kPassthrough:
            result = x;
            break;
        case FunctionMode::kLogAbs:
            result = logf(fabsf(x) + kFuncLogGuard);
            break;
        case FunctionMode::kSqrtAbs:
            result = sqrtf(fabsf(x));
            break;
        case FunctionMode::kSquare:
            result = x * x;
            break;
        case FunctionMode::kDerivativeScaled:
            // Discrete first-order derivative: −(x[n] − x[n−1]).
            // Scaled ×100 so the output reaches useful amplitudes at audio rates.
            result = -(x - state.prev) * kFuncDeriveScale;
            break;
        case FunctionMode::kDerivative:
            result = -(x - state.prev);
            break;
        default:
            result = x;
    }
    state.prev = x;
    return result;
}
