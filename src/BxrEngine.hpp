#pragma once
// AI250 BXR — platform-independent DSP engine.
//
// Direct port of collision-patch-init/BXR (main.cpp + controls.h). The five
// headers in src/bxr/ are byte-for-byte copies of collision-patch-init/common/,
// so the argument / function / generator / distortion / Geiger maths is the
// exact code that runs on the hardware. This file only replaces the Daisy
// hardware reads (ADC, mux, GPIO) with plain values the VCV module hands in.
//
// Voltage conventions (matching the Patch SM's ±5 V-to-±1.0 scaling):
//   audio in  : volts / 5          audio out : sample × 5 V   (firmware: ×0.5 into a ±10 V DAC)
//   CV in     : volts / 5 added to the 0–1 knob baseline (no attenuator, same as hardware)
//   V/Oct     : 1 V/oct            gates     : 0 / 5 V        (same level the firmware writes)

#include <cstddef>
#include <cstdint>
#include <cmath>
#include "bxr/argument.h"
#include "bxr/function.h"
#include "bxr/generator.h"
#include "bxr/distortion.h"
#include "bxr/geiger.h"

namespace bxr {

// ── Tuning constants (copied from BXR/controls.h) ────────────────────────────
static constexpr float kGainMin         = 0.0f;
static constexpr float kGainMax         = 2.0f;
static constexpr float kOscBxrFreqMin   = 32.70f;   // C1
static constexpr float kOscBxrFreqMax   = 2093.0f;  // C7
static constexpr float kLfoPatchFreqMin = 0.001f;
static constexpr float kLfoPatchFreqMax = 200.0f;

// ── VCV-side scaling ─────────────────────────────────────────────────────────
static constexpr float kVoltsPerUnit    = 5.0f;     // ±5 V  ↔ ±1.0 inside the DSP
static constexpr float kGateHighVolts   = 5.0f;     // hardware writes 5 V gates
// The firmware runs the Geiger gates once per 48-sample block at 48 kHz (1 ms).
// VCV can run at any sample rate, so we tick them every 1 ms of real time and
// tell processGeigerGate() the block was 48 "hardware samples" long. That keeps
// the 5 ms pulse width and the firing rate identical at 44.1, 48, 96 kHz...
static constexpr size_t   kFirmwareBlockSize = 48;
static constexpr float    kGeigerTickSeconds = 0.001f;

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline float hardClip(float x) { return clampf(x, -1.0f, 1.0f); }
inline float wetDryMix(float dry, float wet, float mix) { return dry + mix * (wet - dry); }
inline float logFreqMap(float knob, float lo, float hi) { return lo * powf(hi / lo, knob); }

// Everything the panel provides, already in firmware units (knobs 0–1, CVs in volts).
struct PanelState {
    float argKnob    = 0.0f;   // 0–1
    float funcKnob   = 0.0f;   // 0–1
    float waveKnob   = 0.0f;   // 0–1
    float in1VolKnob = 1.0f;   // 0–1
    float centerKnob = 0.7f;   // In2 Vol / Pitch, 0–1
    float distKnob   = 0.0f;   // 0–1
    float mixKnob    = 1.0f;   // 0–1

    float argCvVolts  = 0.0f;
    float funcCvVolts = 0.0f;
    float waveCvVolts = 0.0f;
    float voctVolts   = 0.0f;
    float distCvVolts = 0.0f;
    float mixCvVolts  = 0.0f;

    bool swapInputs    = false;
    bool useOscillator = true;
    bool lfoMode       = false;
};

struct ControlState {
    ArgumentMode argMode   = ArgumentMode::kAPlusB;
    FunctionMode funcMode  = FunctionMode::kPassthrough;
    WaveformMode waveMode  = WaveformMode::kSine;
    float genFreq   = 440.0f;
    float gainA     = 1.0f;
    float gainB     = 1.0f;
    float wetDry    = 1.0f;
    float distDrive = 0.0f;
    float distMix   = 0.0f;
    bool  useOscillator = true;
    bool  lfoMode       = false;
    bool  swapInputs    = false;
};

inline FunctionMode knobToFunctionMode(float v) {
    int step = static_cast<int>(v * kFuncModeCount);
    if (step >= kFuncModeCount) step = kFuncModeCount - 1;
    return static_cast<FunctionMode>(step);
}

// Mirrors buildControlState() in BXR/controls.h. The ADC noise-gate dead zones
// are dropped: VCV voltages are noise-free, and a dead zone on V/Oct would stop
// small pitch offsets from working.
inline ControlState buildControlState(const PanelState& p) {
    ControlState cs;
    cs.swapInputs    = p.swapInputs;
    cs.lfoMode       = p.lfoMode;
    cs.useOscillator = p.useOscillator;

    const float waveVal = clampf(p.waveKnob + p.waveCvVolts / kVoltsPerUnit, 0.0f, 1.0f);
    const float funcVal = clampf(p.funcKnob + p.funcCvVolts / kVoltsPerUnit, 0.0f, 1.0f);
    const float argVal  = clampf(p.argKnob  + p.argCvVolts  / kVoltsPerUnit, 0.0f, 1.0f);
    cs.waveMode = knobToWaveformMode(waveVal);
    cs.argMode  = knobToArgumentMode(argVal);
    cs.funcMode = knobToFunctionMode(funcVal);

    cs.distDrive = clampf(p.distKnob + p.distCvVolts / kVoltsPerUnit, 0.0f, 1.0f);
    cs.wetDry    = clampf(p.mixKnob  + p.mixCvVolts  / kVoltsPerUnit, 0.0f, 1.0f);
    cs.distMix   = cs.distDrive;

    cs.gainA = p.in1VolKnob;

    // Pitch is always computed: OSC Out tracks the knob + V/Oct even in IN2 mode.
    const float fMin = cs.lfoMode ? kLfoPatchFreqMin : kOscBxrFreqMin;
    const float fMax = cs.lfoMode ? kLfoPatchFreqMax : kOscBxrFreqMax;
    const float base = logFreqMap(p.centerKnob, fMin, fMax);
    cs.genFreq = clampf(base * powf(2.0f, p.voctVolts), fMin, fMax);

    if (cs.useOscillator) {
        cs.gainB = 1.0f;
    } else {
        const float gainKnob = p.centerKnob * p.centerKnob * kGainMax;
        cs.gainB = clampf(gainKnob, kGainMin, kGainMax);
    }
    return cs;
}

struct Frame {
    float outVolts   = 0.0f;
    float oscVolts   = 0.0f;
    float gate1Volts = 0.0f;
    float gate2Volts = 0.0f;
    bool  gate1 = false;
    bool  gate2 = false;
};

class Engine {
public:
    void seed(uint32_t s) {
        genRng_          = s ^ 0xCAFEBABEu;
        geigerA_.rngState = s ^ 0x12345678u;
        geigerB_.rngState = s ^ 0xDEADBEEFu;
    }

    void reset() {
        gen_       = Generator{};
        funcState_ = FunctionState{};
        bassBoost_ = BassBoostState{};
        geigerA_.counter = 0;
        geigerB_.counter = 0;
        tickTimer_ = 0.0f;
        gate1_ = gate2_ = false;
    }

    // One sample. in1Volts / in2Volts are the raw jack voltages.
    Frame process(float in1Volts, float in2Volts, const ControlState& cs, float sampleRate) {
        gen_.setFreq(cs.genFreq, sampleRate);
        const float in1 = in1Volts / kVoltsPerUnit;
        const float in2 = in2Volts / kVoltsPerUnit;

        const float genSample  = gen_.process(cs.waveMode, genRng_);
        const float hwA        = cs.swapInputs ? in2 : in1;
        const float hwB        = cs.swapInputs ? in1 : in2;
        const float sigA       = hwA * cs.gainA;
        const float sigB       = cs.useOscillator ? genSample : hwB;
        const float sigBGained = sigB * cs.gainB;

        const float arg      = applyArgument(cs.argMode, sigA, sigBGained);
        const float shaped   = applyFunction(cs.funcMode, arg, funcState_);
        const float clipped  = hardClip(shaped);
        const float boosted  = applyBassBoost(clipped, cs.distDrive, bassBoost_);
        const float dist     = applyDistortion(boosted, cs.distDrive);
        const float postDist = hardClip(wetDryMix(clipped, dist, cs.distMix));
        // Dry side is raw IN1 (unswapped, un-gained) — same as the firmware.
        const float output   = hardClip(wetDryMix(in1, postDist, cs.wetDry));

        tickGeigers(output, cs.lfoMode, sampleRate);

        Frame f;
        f.outVolts   = output    * kVoltsPerUnit;
        f.oscVolts   = genSample * kVoltsPerUnit;
        f.gate1      = gate1_;
        f.gate2      = gate2_;
        f.gate1Volts = gate1_ ? kGateHighVolts : 0.0f;
        f.gate2Volts = gate2_ ? kGateHighVolts : 0.0f;
        return f;
    }

private:
    void tickGeigers(float lastOutput, bool lfoMode, float sampleRate) {
        tickTimer_ += 1.0f / sampleRate;
        if (tickTimer_ < kGeigerTickSeconds) return;
        tickTimer_ -= kGeigerTickSeconds;

        // Same dampening as updateGeigers() in BXR/main.cpp.
        const float level = fabsf(lastOutput) * (lfoMode ? kGeigerLfoDamp : 1.0f);
        gate1_ = processGeigerGate(geigerA_, level, kFirmwareBlockSize);
        gate2_ = processGeigerGate(geigerB_, level, kFirmwareBlockSize);
    }

    Generator      gen_;
    FunctionState  funcState_;
    BassBoostState bassBoost_;
    GeigerState    geigerA_, geigerB_;
    uint32_t       genRng_    = 0xCAFEBABEu;
    float          tickTimer_ = 0.0f;
    bool           gate1_ = false, gate2_ = false;
};

} // namespace bxr
