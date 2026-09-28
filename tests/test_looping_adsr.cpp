#include <cassert>
#include <cstdio>
#include <cmath>
#include "LoopingAdsr.hpp"

static LoopingAdsr::Params makeParams(float sampleTime = 0.01f) {
    LoopingAdsr::Params p{};
    p.sampleTime = sampleTime;
    p.loopMode = false;
    p.gatePatched = false;
    p.gateHigh = false;
    p.gateRising = false;
    p.triggerPatched = false;
    p.triggerHigh = false;
    p.triggerRising = false;
    p.manualRising = false;
    p.attackTime = 0.1f;
    p.decayTime = 0.1f;
    p.releaseTime = 0.1f;
    p.sustainVoltage = 5.f;
    return p;
}

static void test_idle_stays_at_zero() {
    LoopingAdsr adsr;
    LoopingAdsr::Params p = makeParams();
    for (int i = 0; i < 10; i++) adsr.process(p);
    assert(adsr.state() == LoopingAdsr::State::IDLE);
    assert(adsr.output() == 0.f);
    printf("PASS: idle stays at zero with nothing patched or fired\n");
}

static void test_manual_fires_ad_only_cycle() {
    LoopingAdsr adsr;
    LoopingAdsr::Params p = makeParams();

    p.manualRising = true;
    adsr.process(p);
    assert(adsr.state() == LoopingAdsr::State::ATTACK);
    p.manualRising = false;

    // Run long enough to fully complete Attack (0.1s) then Decay (0.1s).
    for (int i = 0; i < 40; i++) adsr.process(p);  // 40 * 0.01s = 0.4s
    assert(adsr.state() == LoopingAdsr::State::IDLE);
    assert(adsr.output() < 0.2f);  // back near 0V, Sustain/Release skipped entirely
    printf("PASS: manual button fires an AD-only cycle, ends at IDLE near 0V\n");
}

static void test_trigger_input_fires_ad_only_cycle() {
    LoopingAdsr adsr;
    LoopingAdsr::Params p = makeParams();
    p.triggerPatched = true;

    p.triggerRising = true;
    adsr.process(p);
    assert(adsr.state() == LoopingAdsr::State::ATTACK);
    printf("PASS: Trigger In rising edge fires an AD cycle same as Manual\n");
}

static void test_attack_ramps_toward_ceiling() {
    LoopingAdsr adsr;
    LoopingAdsr::Params p = makeParams();
    p.manualRising = true;
    adsr.process(p);
    p.manualRising = false;

    float prev = adsr.output();
    for (int i = 0; i < 5; i++) {
        adsr.process(p);
        assert(adsr.output() > prev);  // monotonically rising during Attack
        prev = adsr.output();
    }
    printf("PASS: output rises monotonically during Attack\n");
}

static void test_gate_rising_starts_full_adsr() {
    LoopingAdsr adsr;
    LoopingAdsr::Params p = makeParams();
    p.gatePatched = true;
    p.gateHigh = true;
    p.gateRising = true;

    adsr.process(p);
    assert(adsr.state() == LoopingAdsr::State::ATTACK);
    printf("PASS: Gate In rising edge starts full ADSR cycle\n");
}

static void test_sustain_holds_while_gate_high() {
    LoopingAdsr adsr;
    LoopingAdsr::Params p = makeParams();
    p.gatePatched = true;
    p.gateHigh = true;
    p.gateRising = true;
    adsr.process(p);
    p.gateRising = false;

    // Run through Attack (0.1s) + Decay (0.1s) = 20 steps of 0.01s.
    for (int i = 0; i < 25; i++) adsr.process(p);
    assert(adsr.state() == LoopingAdsr::State::SUSTAIN);
    assert(std::fabs(adsr.output() - p.sustainVoltage) < 0.2f);

    // Hold for a while longer — must stay at Sustain, not drift or time out.
    for (int i = 0; i < 100; i++) adsr.process(p);
    assert(adsr.state() == LoopingAdsr::State::SUSTAIN);
    assert(adsr.output() == p.sustainVoltage);
    printf("PASS: Sustain holds at configured CV while gate stays high\n");
}

static void test_gate_falling_triggers_release_to_zero() {
    LoopingAdsr adsr;
    LoopingAdsr::Params p = makeParams();
    p.gatePatched = true;
    p.gateHigh = true;
    p.gateRising = true;
    adsr.process(p);
    p.gateRising = false;
    for (int i = 0; i < 25; i++) adsr.process(p);
    assert(adsr.state() == LoopingAdsr::State::SUSTAIN);

    p.gateHigh = false;  // gate falls
    adsr.process(p);
    assert(adsr.state() == LoopingAdsr::State::RELEASE);

    for (int i = 0; i < 15; i++) adsr.process(p);  // 0.15s > releaseTime (0.1s)
    assert(adsr.state() == LoopingAdsr::State::IDLE);
    assert(adsr.output() < 0.2f);
    printf("PASS: Gate falling edge triggers Release, ends at IDLE near 0V\n");
}

static void test_legato_retrigger_starts_from_current_output() {
    LoopingAdsr adsr;
    LoopingAdsr::Params p = makeParams();
    p.manualRising = true;
    adsr.process(p);
    p.manualRising = false;

    // Let Attack run partway (not to completion).
    for (int i = 0; i < 3; i++) adsr.process(p);
    float midOutput = adsr.output();
    assert(midOutput > 0.f);
    assert(adsr.state() == LoopingAdsr::State::ATTACK);

    // Retrigger mid-attack.
    p.manualRising = true;
    adsr.process(p);
    p.manualRising = false;

    assert(adsr.state() == LoopingAdsr::State::ATTACK);
    // Output must not have jumped back to 0 — legato retrigger continues
    // from wherever it was, not from silence. Since Attack always ramps
    // *toward* the ceiling (10V) and midOutput is already below the ceiling,
    // a genuinely legato continuation can only stay at or rise above
    // midOutput on the next sample. A broken reset-to-0V implementation
    // would necessarily dip below midOutput on this next sample instead
    // (e.g. with these params it would fall to ~3.9V vs. midOutput ~7.8V),
    // so this is a tight, non-trivial check.
    assert(adsr.output() >= midOutput);
    printf("PASS: retrigger mid-cycle is legato, does not reset to 0V\n");
}

static void test_legato_retrigger_from_release() {
    LoopingAdsr adsr;
    LoopingAdsr::Params p = makeParams();
    p.gatePatched = true;
    p.gateHigh = true;
    p.gateRising = true;
    adsr.process(p);
    p.gateRising = false;
    for (int i = 0; i < 25; i++) adsr.process(p);  // reach Sustain
    assert(adsr.state() == LoopingAdsr::State::SUSTAIN);

    p.gateHigh = false;
    adsr.process(p);  // enter Release (tau = releaseTime/5 = 0.02s, so this
                       // first sample already decays sustain 5V -> ~3.03V)
    adsr.process(p);  // one more sample, partway through Release -> ~1.84V
    float midRelease = adsr.output();
    assert(adsr.state() == LoopingAdsr::State::RELEASE);
    assert(midRelease > 1.f);  // still well above 0V

    // New gate-high arrives mid-Release — must restart Attack from here, not 0V.
    p.gateHigh = true;
    p.gateRising = true;
    adsr.process(p);
    assert(adsr.state() == LoopingAdsr::State::ATTACK);

    // Attack pulls hard toward the 10V ceiling (tau = attackTime/5 = 0.02s),
    // so a single sample from either a legato start (midRelease ~1.84V) or a
    // broken reset-to-0V start both land well above midRelease itself —
    // comparing against midRelease directly would not catch a regression.
    // Instead compare against what a from-0V restart would produce on this
    // exact sample: since both curves share the same target/tau and legato
    // starts from a strictly higher point (midRelease > 0V), legato must
    // produce a strictly higher output at every following sample.
    constexpr float kCeiling = 10.f;  // matches LoopingAdsr's internal ceiling
    float coeff = std::exp(-p.sampleTime / (p.attackTime / 5.f));
    float brokenRestartOutput = kCeiling * (1.f - coeff);
    assert(adsr.output() > brokenRestartOutput);
    printf("PASS: retrigger from mid-Release is legato, does not dip to 0V\n");
}

static void test_loop_mode_free_runs_with_nothing_patched() {
    LoopingAdsr adsr;
    LoopingAdsr::Params p = makeParams();
    p.loopMode = true;

    adsr.process(p);
    assert(adsr.state() == LoopingAdsr::State::ATTACK);

    // Run through several full Attack+Decay cycles (0.2s each) — must keep
    // re-entering ATTACK on its own, never settle at IDLE.
    bool sawAttackAgain = false;
    for (int i = 0; i < 200; i++) {
        adsr.process(p);
        if (i > 40 && adsr.state() == LoopingAdsr::State::ATTACK) sawAttackAgain = true;
    }
    assert(sawAttackAgain);
    assert(adsr.state() != LoopingAdsr::State::IDLE);
    printf("PASS: Loop mode free-runs Attack/Decay with nothing patched\n");
}

static void test_loop_mode_gated_only_runs_while_high() {
    LoopingAdsr adsr;
    LoopingAdsr::Params p = makeParams();
    p.loopMode = true;
    p.gatePatched = true;
    p.gateHigh = false;

    // Gate low, nothing patched running — must stay IDLE.
    for (int i = 0; i < 10; i++) adsr.process(p);
    assert(adsr.state() == LoopingAdsr::State::IDLE);
    assert(adsr.output() == 0.f);

    p.gateHigh = true;
    adsr.process(p);
    assert(adsr.state() == LoopingAdsr::State::ATTACK);
    printf("PASS: Loop mode with Gate patched only starts once gate goes high\n");
}

static void test_loop_mode_finishes_decay_after_gate_falls() {
    LoopingAdsr adsr;
    LoopingAdsr::Params p = makeParams();
    p.loopMode = true;
    p.gatePatched = true;
    p.gateHigh = true;

    adsr.process(p);  // enter Attack
    for (int i = 0; i < 10; i++) adsr.process(p);  // finish Attack (0.1s), enter Decay
    assert(adsr.state() == LoopingAdsr::State::DECAY);

    p.gateHigh = false;  // gate falls mid-Decay
    adsr.process(p);
    // Must NOT jump to IDLE or freeze immediately — still finishing Decay.
    assert(adsr.state() == LoopingAdsr::State::DECAY);

    for (int i = 0; i < 15; i++) adsr.process(p);  // let Decay finish (0.1s)
    assert(adsr.state() == LoopingAdsr::State::IDLE);
    assert(adsr.output() < 0.2f);
    printf("PASS: Loop mode finishes in-flight Decay after gate falls, then stops at 0V\n");
}

int main() {
    test_idle_stays_at_zero();
    test_manual_fires_ad_only_cycle();
    test_trigger_input_fires_ad_only_cycle();
    test_attack_ramps_toward_ceiling();
    test_gate_rising_starts_full_adsr();
    test_sustain_holds_while_gate_high();
    test_gate_falling_triggers_release_to_zero();
    test_legato_retrigger_starts_from_current_output();
    test_legato_retrigger_from_release();
    test_loop_mode_free_runs_with_nothing_patched();
    test_loop_mode_gated_only_runs_while_high();
    test_loop_mode_finishes_decay_after_gate_falls();
    printf("\nAll tests passed.\n");
    return 0;
}
