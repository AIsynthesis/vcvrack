// Unit tests for the AI250 BXR engine (no VCV SDK needed).
// Build & run:  make test_bxr_engine && ./test_bxr_engine
#include "BxrEngine.hpp"
#include <cstdio>
#include <cmath>
#include <random>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static float stepCentre(int step, int count) { return (step + 0.5f) / count; }

static void testModeQuantisation() {
    for (int i = 0; i < kArgModeCount; i++)
        CHECK(static_cast<int>(knobToArgumentMode(stepCentre(i, kArgModeCount))) == i, "arg step %d", i);
    for (int i = 0; i < kFuncModeCount; i++)
        CHECK(static_cast<int>(bxr::knobToFunctionMode(stepCentre(i, kFuncModeCount))) == i, "func step %d", i);
    for (int i = 0; i < kWaveModeCount; i++)
        CHECK(static_cast<int>(knobToWaveformMode(stepCentre(i, kWaveModeCount))) == i, "wave step %d", i);

    // +5 V of CV sweeps the whole range, like the hardware (knob + CV/5, clamped).
    bxr::PanelState p;
    p.argKnob = stepCentre(0, kArgModeCount);
    p.argCvVolts = 5.0f;
    CHECK(bxr::buildControlState(p).argMode == ArgumentMode::kA10DivAbsB, "arg +5V -> last mode");
    p.argCvVolts = -5.0f;
    CHECK(bxr::buildControlState(p).argMode == ArgumentMode::kA, "arg -5V -> first mode");
}

static bxr::PanelState defaultPanel() {
    bxr::PanelState p;
    p.argKnob  = stepCentre(2, kArgModeCount);   // A + B
    p.funcKnob = stepCentre(2, kFuncModeCount);  // pass
    p.waveKnob = stepCentre(0, kWaveModeCount);  // sine
    return p;
}

static void testOscPassesThroughByDefault() {
    bxr::Engine e; e.seed(1); e.reset();
    const auto cs = bxr::buildControlState(defaultPanel());
    float maxDiff = 0, peak = 0;
    for (int n = 0; n < 48000; n++) {
        auto f = e.process(0.f, 0.f, cs, 48000.f);
        maxDiff = std::fmax(maxDiff, std::fabs(f.outVolts - f.oscVolts));
        peak = std::fmax(peak, std::fabs(f.oscVolts));
    }
    CHECK(maxDiff < 1e-4f, "default patch: OUT should equal OSC, diff %f", maxDiff);
    CHECK(peak > 4.9f && peak <= 5.0001f, "OSC peak should be 5 V, got %f", peak);
}

static void testMixZeroIsDryIn1() {
    bxr::Engine e; e.seed(1); e.reset();
    auto p = defaultPanel();
    p.mixKnob = 0.f;
    p.swapInputs = true;  // dry path is raw IN1 even when swapped
    const auto cs = bxr::buildControlState(p);
    for (int n = 0; n < 1000; n++) {
        float in1 = 3.f * std::sin(n * 0.01f);
        auto f = e.process(in1, -2.f, cs, 48000.f);
        CHECK(std::fabs(f.outVolts - in1) < 1e-4f, "mix=0 should be dry IN1 (%f vs %f)", f.outVolts, in1);
        if (failures) return;
    }
}

static void testAllModesBoundedAndFinite() {
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> volts(-10.f, 10.f);
    for (int a = 0; a < kArgModeCount; a++)
    for (int fn = 0; fn < kFuncModeCount; fn++)
    for (int w = 0; w < kWaveModeCount; w++)
    for (float drive : {0.f, 0.5f, 1.f}) {
        bxr::Engine e; e.seed(a * 100 + fn * 10 + w); e.reset();
        bxr::PanelState p;
        p.argKnob = stepCentre(a, kArgModeCount);
        p.funcKnob = stepCentre(fn, kFuncModeCount);
        p.waveKnob = stepCentre(w, kWaveModeCount);
        p.distKnob = drive;
        p.useOscillator = (w % 2) == 0;
        const auto cs = bxr::buildControlState(p);
        for (int n = 0; n < 500; n++) {
            auto f = e.process(volts(rng), volts(rng), cs, 48000.f);
            bool ok = std::isfinite(f.outVolts) && std::fabs(f.outVolts) <= 5.0001f &&
                      std::isfinite(f.oscVolts) && std::fabs(f.oscVolts) <= 5.0001f;
            if (!ok) { CHECK(false, "arg %d func %d wave %d drive %.1f -> out %f osc %f", a, fn, w, drive, f.outVolts, f.oscVolts); return; }
        }
    }
}

static float measureFreq(const bxr::PanelState& p, float sr) {
    bxr::Engine e; e.seed(3); e.reset();
    const auto cs = bxr::buildControlState(p);
    int crossings = 0; float prev = 0;
    const int n = static_cast<int>(sr * 2);
    for (int i = 0; i < n; i++) {
        float v = e.process(0, 0, cs, sr).oscVolts;
        if (prev < 0 && v >= 0) crossings++;
        prev = v;
    }
    return crossings / 2.0f;
}

static void testVoct() {
    auto p = defaultPanel();
    p.centerKnob = 0.5f;  // 32.7 * 64^0.5 = 261.6 Hz (C4)
    float f0 = measureFreq(p, 48000.f);
    p.voctVolts = 1.f;
    float f1 = measureFreq(p, 48000.f);
    CHECK(std::fabs(f0 - 261.6f) < 1.5f, "centre knob 0.5 should be ~C4, got %f", f0);
    CHECK(std::fabs(f1 / f0 - 2.f) < 0.02f, "+1 V should double pitch: %f -> %f", f0, f1);
    // Same pitch at 96 kHz.
    p.voctVolts = 0.f;
    float f96 = measureFreq(p, 96000.f);
    CHECK(std::fabs(f96 - f0) < 1.5f, "pitch should not depend on sample rate: %f vs %f", f0, f96);
}

struct GateStats { int pulses; float meanWidthMs; };

static GateStats geigerStats(float sr, bool lfo) {
    bxr::Engine e; e.seed(7); e.reset();
    bxr::PanelState p;
    p.argKnob = stepCentre(0, kArgModeCount);   // A only
    p.funcKnob = stepCentre(2, kFuncModeCount); // pass
    p.lfoMode = lfo;
    const auto cs = bxr::buildControlState(p);
    const int n = static_cast<int>(sr * 60);  // one minute
    int pulses = 0, highSamples = 0; bool prev = false;
    for (int i = 0; i < n; i++) {
        auto f = e.process(5.f, 0.f, cs, sr);  // full-scale DC -> max Geiger activity
        if (f.gate1 && !prev) pulses++;
        if (f.gate1) highSamples++;
        prev = f.gate1;
    }
    return {pulses, pulses ? 1000.f * highSamples / sr / pulses : 0.f};
}

static void testGeiger() {
    GateStats s44 = geigerStats(44100.f, false);
    GateStats s48 = geigerStats(48000.f, false);
    GateStats s96 = geigerStats(96000.f, false);
    std::printf("  Geiger @ full level, 60 s: 44.1k %d pulses (%.2f ms), 48k %d (%.2f ms), 96k %d (%.2f ms)\n",
                s44.pulses, s44.meanWidthMs, s48.pulses, s48.meanWidthMs, s96.pulses, s96.meanWidthMs);
    CHECK(s48.pulses > 500, "hot signal should fire plenty of gates, got %d", s48.pulses);
    for (auto s : {s44, s48, s96})
        CHECK(std::fabs(s.meanWidthMs - 5.f) < 1.1f, "gate width should be ~5 ms, got %f", s.meanWidthMs);
    CHECK(std::abs(s96.pulses - s48.pulses) < s48.pulses / 5, "firing rate should not depend on sample rate");

    GateStats lfo = geigerStats(48000.f, true);
    CHECK(lfo.pulses < 3, "LFO mode should damp gates almost to silence, got %d", lfo.pulses);
}

int main() {
    testModeQuantisation();
    testOscPassesThroughByDefault();
    testMixZeroIsDryIn1();
    testAllModesBoundedAndFinite();
    testVoct();
    testGeiger();
    if (failures == 0) std::printf("All BXR engine tests passed.\n");
    return failures == 0 ? 0 : 1;
}
