# AI003 Looping ADSR Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the AI003 Looping ADSR as a new module in the existing AI Synthesis VCV Rack plugin package (`ai001` repo), alongside AI001 Multiple.

**Architecture:** A standalone, SDK-free `LoopingAdsr` state-machine class (one instance per polyphony channel, up to 16) handles all envelope/loop logic and is unit-tested in isolation. A thin `AI003.cpp` (`rack::Module` + `rack::ModuleWidget`) reads params/inputs, does Schmitt-trigger edge detection, drives an array of `LoopingAdsr` instances, and renders the panel using coordinates already verified against the real panel artwork and the official product manual.

**Tech Stack:** C++17, VCV Rack v2 SDK, standard `make` plugin build (`RACK_DIR = ../Rack2SDK/RackSDK`), g++ 15.2.0, GNU Make 4.3 (all confirmed available on this machine).

**Design doc:** [docs/superpowers/specs/2026-07-05-ai003-looping-adsr-design.md](../specs/2026-07-05-ai003-looping-adsr-design.md)
**PRD:** [docs/superpowers/specs/2026-07-05-ai003-prd.md](../specs/2026-07-05-ai003-prd.md)

---

## File Structure

- **Create:** `src/LoopingAdsr.hpp` — DSP state machine, no `rack.hpp` dependency.
- **Create:** `tests/test_looping_adsr.cpp` — unit tests for `LoopingAdsr`.
- **Modify:** `tests/Makefile` — add a `test_looping_adsr` build target.
- **Create:** `src/AI003.cpp` — `AI003Module` + `AI003Widget`, registered as `modelAI003`.
- **Modify:** `src/plugin.hpp` — declare `extern rack::Model* modelAI003;`.
- **Modify:** `src/plugin.cpp` — register `modelAI003` in `init()`.
- **Modify:** `plugin.json` — add the AI003 module entry.
- **Modify:** `Makefile` — add `src/AI003.cpp` to `SOURCES`.
- **Create:** `res/AI003-black.svg` — hand-built panel background (same lightweight style as `res/AI001-black-v6.svg`), using the verified widget coordinates below.

---

## Task 1: LoopingAdsr — Envelope mode, AD-only cycle (no gate)

**Files:**
- Create: `src/LoopingAdsr.hpp`
- Test: `tests/test_looping_adsr.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/test_looping_adsr.cpp`:

```cpp
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

int main() {
    test_idle_stays_at_zero();
    test_manual_fires_ad_only_cycle();
    test_trigger_input_fires_ad_only_cycle();
    test_attack_ramps_toward_ceiling();
    printf("\nAll tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails (LoopingAdsr.hpp doesn't exist yet)**

Run: `cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001\tests" && g++ -std=c++17 -I../src -Wall -Wextra -O2 -o test_looping_adsr test_looping_adsr.cpp`
Expected: FAIL with `fatal error: LoopingAdsr.hpp: No such file or directory`

- [ ] **Step 3: Write the implementation**

Create `src/LoopingAdsr.hpp`:

```cpp
#pragma once
#include <cmath>
#include <algorithm>

// Envelope/loop generator core for the AI003 Looping ADSR port.
// No VCV Rack dependency — fully unit-testable. Edge detection (Schmitt
// triggers) happens in the caller (AI003Module); this class receives
// pre-computed rising-edge/level booleans each sample.
class LoopingAdsr {
public:
    enum class State { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };

    struct Params {
        float sampleTime;      // seconds per sample (1 / sample rate)
        bool loopMode;         // true = Loop, false = Envelope
        bool gatePatched;      // Gate In has a cable
        bool gateHigh;         // current Gate In level (>= 1V), only meaningful if gatePatched
        bool gateRising;       // Gate In rising edge this sample
        bool triggerPatched;   // Trigger In has a cable
        bool triggerHigh;      // current Trigger In level (used for Loop-mode gating)
        bool triggerRising;    // Trigger In rising edge this sample
        bool manualRising;     // Manual button pressed this sample
        float attackTime;      // seconds
        float decayTime;       // seconds
        float releaseTime;     // seconds
        float sustainVoltage;  // 0-10V
    };

    void reset() {
        state_ = State::IDLE;
        output_ = 0.f;
        target_ = 0.f;
        tau_ = 1.f;
    }

    State state() const { return state_; }
    float output() const { return output_; }

    float process(const Params& p) {
        if (!p.loopMode) processEnvelopeMode(p);
        advanceOutput(p);
        return output_;
    }

private:
    static constexpr float kCeiling = 10.f;
    static constexpr float kEpsilon = 0.1f;  // ~1% of full scale — "close enough" to a target

    State state_ = State::IDLE;
    float output_ = 0.f;
    float target_ = 0.f;
    float tau_ = 1.f;

    void processEnvelopeMode(const Params& p) {
        bool adFire   = (!p.gatePatched || p.gateHigh) && (p.triggerRising || p.manualRising);
        bool gateFire = p.gatePatched && p.gateRising;
        if (adFire || gateFire) enterAttack(p);

        switch (state_) {
            case State::ATTACK:
                if (reachedTarget()) enterDecay(p);
                break;
            case State::DECAY:
                if (reachedTarget()) {
                    if (p.gatePatched && p.gateHigh) enterSustain(p);
                    else state_ = State::IDLE;
                }
                break;
            case State::SUSTAIN:
                if (!(p.gatePatched && p.gateHigh)) enterRelease(p);
                break;
            case State::RELEASE:
                if (reachedTarget()) state_ = State::IDLE;
                break;
            case State::IDLE:
                break;
        }
    }

    void enterAttack(const Params& p)  { setStage(State::ATTACK, kCeiling, p.attackTime); }
    void enterDecay(const Params& p) {
        float decayTarget = (p.gatePatched && p.gateHigh) ? p.sustainVoltage : 0.f;
        setStage(State::DECAY, decayTarget, p.decayTime);
    }
    void enterSustain(const Params& p) { setStage(State::SUSTAIN, p.sustainVoltage, 1.f); }
    void enterRelease(const Params& p) { setStage(State::RELEASE, 0.f, p.releaseTime); }

    void setStage(State s, float target, float stageTime) {
        state_ = s;
        target_ = target;
        tau_ = std::max(stageTime, 0.001f) / 5.f;
    }

    bool reachedTarget() const { return std::fabs(target_ - output_) < kEpsilon; }

    void advanceOutput(const Params& p) {
        if (state_ == State::SUSTAIN) { output_ = target_; return; }
        float coeff = std::exp(-p.sampleTime / tau_);
        output_ = target_ + (output_ - target_) * coeff;
    }
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001\tests" && g++ -std=c++17 -I../src -Wall -Wextra -O2 -o test_looping_adsr test_looping_adsr.cpp && ./test_looping_adsr`
Expected: `All tests passed.`

- [ ] **Step 5: Commit**

```bash
cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001"
git add src/LoopingAdsr.hpp tests/test_looping_adsr.cpp
git commit -m "feat: add LoopingAdsr core with Envelope-mode AD-only cycle"
```

---

## Task 2: LoopingAdsr — Envelope mode, gated ADSR (Sustain/Release)

**Files:**
- Modify: `src/LoopingAdsr.hpp` (already handles this via Task 1's `enterDecay`/`enterSustain`/`enterRelease` — this task adds test coverage and fixes any gaps found)
- Test: `tests/test_looping_adsr.cpp`

- [ ] **Step 1: Add failing tests**

Append to `tests/test_looping_adsr.cpp` (before the `int main()` function):

```cpp
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
```

Add the three new calls inside `int main()`, after the existing ones:

```cpp
    test_gate_rising_starts_full_adsr();
    test_sustain_holds_while_gate_high();
    test_gate_falling_triggers_release_to_zero();
```

- [ ] **Step 2: Run tests to verify they fail or pass**

Run: `cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001\tests" && g++ -std=c++17 -I../src -Wall -Wextra -O2 -o test_looping_adsr test_looping_adsr.cpp && ./test_looping_adsr`

Expected: these should already PASS given Task 1's implementation (it was written to handle the full state table up front). If any fail, fix `src/LoopingAdsr.hpp` until they pass — do not weaken the tests.

- [ ] **Step 3: Commit**

```bash
cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001"
git add tests/test_looping_adsr.cpp
git commit -m "test: cover Envelope-mode gated ADSR (Sustain/Release) behavior"
```

---

## Task 3: LoopingAdsr — Legato retrigger

**Files:**
- Test: `tests/test_looping_adsr.cpp`

- [ ] **Step 1: Add failing test**

Append before `int main()`:

```cpp
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
    // from wherever it was, not from silence. It should stay close to
    // (or above) midOutput, never drop to near zero.
    assert(adsr.output() > midOutput * 0.5f);
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
    adsr.process(p);  // enter Release
    for (int i = 0; i < 5; i++) adsr.process(p);  // partway through Release
    float midRelease = adsr.output();
    assert(midRelease > 0.5f);  // still well above 0V

    // New gate-high arrives mid-Release — must restart Attack from here, not 0V.
    p.gateHigh = true;
    p.gateRising = true;
    adsr.process(p);
    assert(adsr.state() == LoopingAdsr::State::ATTACK);
    assert(adsr.output() >= midRelease * 0.9f);
    printf("PASS: retrigger from mid-Release is legato, does not dip to 0V\n");
}
```

Add the two new calls inside `int main()`:

```cpp
    test_legato_retrigger_starts_from_current_output();
    test_legato_retrigger_from_release();
```

- [ ] **Step 2: Run tests**

Run: `cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001\tests" && g++ -std=c++17 -I../src -Wall -Wextra -O2 -o test_looping_adsr test_looping_adsr.cpp && ./test_looping_adsr`
Expected: `All tests passed.` (Task 1's `enterAttack` already starts from whatever `output_` currently holds, since `setStage` never resets `output_` — this is what makes retrigger legato by construction.)

- [ ] **Step 3: Commit**

```bash
cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001"
git add tests/test_looping_adsr.cpp
git commit -m "test: cover legato retrigger from mid-Attack and mid-Release"
```

---

## Task 4: LoopingAdsr — Loop mode (free-running and gated)

**Files:**
- Modify: `src/LoopingAdsr.hpp`
- Test: `tests/test_looping_adsr.cpp`

- [ ] **Step 1: Write failing tests**

Append before `int main()`:

```cpp
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
```

Add the three new calls inside `int main()`:

```cpp
    test_loop_mode_free_runs_with_nothing_patched();
    test_loop_mode_gated_only_runs_while_high();
    test_loop_mode_finishes_decay_after_gate_falls();
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001\tests" && g++ -std=c++17 -I../src -Wall -Wextra -O2 -o test_looping_adsr test_looping_adsr.cpp && ./test_looping_adsr`
Expected: FAIL — `process()` currently does nothing for `loopMode == true` (Task 1 only implemented `processEnvelopeMode`).

- [ ] **Step 3: Implement Loop mode**

In `src/LoopingAdsr.hpp`, replace the `process()` method:

```cpp
    float process(const Params& p) {
        if (!p.loopMode) processEnvelopeMode(p);
        else processLoopMode(p);
        advanceOutput(p);
        return output_;
    }
```

And add a new private method `processLoopMode`, placed after `processEnvelopeMode`:

```cpp
    void processLoopMode(const Params& p) {
        bool anyPatched  = p.gatePatched || p.triggerPatched;
        bool runningHigh = (p.gatePatched && p.gateHigh) || (p.triggerPatched && p.triggerHigh);
        bool shouldRun    = !anyPatched || runningHigh;

        switch (state_) {
            case State::IDLE:
                if (shouldRun) enterAttack(p);
                break;
            case State::ATTACK:
                if (reachedTarget()) enterDecay(p);
                break;
            case State::DECAY:
                if (reachedTarget()) {
                    if (shouldRun) enterAttack(p);
                    else state_ = State::IDLE;
                }
                break;
            case State::SUSTAIN:
            case State::RELEASE:
                state_ = State::IDLE;  // unused in Loop mode
                break;
        }
    }
```

Also update `enterDecay` so Loop mode never targets Sustain voltage (Sustain is ignored in Loop mode per spec):

```cpp
    void enterDecay(const Params& p) {
        float decayTarget = (!p.loopMode && p.gatePatched && p.gateHigh) ? p.sustainVoltage : 0.f;
        setStage(State::DECAY, decayTarget, p.decayTime);
    }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001\tests" && g++ -std=c++17 -I../src -Wall -Wextra -O2 -o test_looping_adsr test_looping_adsr.cpp && ./test_looping_adsr`
Expected: `All tests passed.`

- [ ] **Step 5: Commit**

```bash
cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001"
git add src/LoopingAdsr.hpp tests/test_looping_adsr.cpp
git commit -m "feat: add Loop mode (free-running and gated) to LoopingAdsr"
```

---

## Task 5: Wire test_looping_adsr into tests/Makefile

**Files:**
- Modify: `tests/Makefile`

- [ ] **Step 1: Update the Makefile**

Replace the full contents of `tests/Makefile`:

```makefile
CXX = g++
CXXFLAGS = -std=c++17 -I../src -Wall -Wextra -O2

all: test_router test_looping_adsr

test_router: test_router.cpp ../src/MultRouter.hpp
	$(CXX) $(CXXFLAGS) -o test_router test_router.cpp

test_looping_adsr: test_looping_adsr.cpp ../src/LoopingAdsr.hpp
	$(CXX) $(CXXFLAGS) -o test_looping_adsr test_looping_adsr.cpp

.PHONY: all clean
clean:
	rm -f test_router test_looping_adsr
```

- [ ] **Step 2: Run the full test suite via make**

Run: `cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001\tests" && make clean && make && ./test_router && ./test_looping_adsr`
Expected: both binaries build with no warnings, both print `All tests passed.`

- [ ] **Step 3: Commit**

```bash
cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001"
git add tests/Makefile
git commit -m "build: wire test_looping_adsr into tests/Makefile"
```

---

## Task 6: AI003 plugin registration (empty module compiles)

**Files:**
- Create: `src/AI003.cpp`
- Modify: `src/plugin.hpp`
- Modify: `src/plugin.cpp`
- Modify: `plugin.json`
- Modify: `Makefile`

- [ ] **Step 1: Add the model declaration**

In `src/plugin.hpp`, add the new extern declaration:

```cpp
#pragma once
#include <rack.hpp>

extern rack::Plugin* pluginInstance;
extern rack::Model* modelAI001;
extern rack::Model* modelAI003;
```

- [ ] **Step 2: Register the model**

In `src/plugin.cpp`:

```cpp
#include "plugin.hpp"

rack::Plugin* pluginInstance = nullptr;

void init(rack::Plugin* p) {
    pluginInstance = p;
    p->addModel(modelAI001);
    p->addModel(modelAI003);
}
```

- [ ] **Step 3: Add the module entry to plugin.json**

In `plugin.json`, add a second entry to the `"modules"` array (after the existing `AI001_Multiple` entry):

```json
        {
            "slug": "AI001_Multiple",
            "name": "AI001 Multiple",
            "description": "2HP passive multiple — 8 jacks in two switchable groups",
            "tags": ["Multiple", "Utility"]
        },
        {
            "slug": "AI003_LoopingADSR",
            "name": "AI003 Looping ADSR",
            "description": "4-stage envelope generator with switchable free-running Loop mode",
            "tags": ["Envelope generator", "LFO"]
        }
```

- [ ] **Step 4: Create a minimal compiling AI003.cpp**

Create `src/AI003.cpp` (full module logic comes in Task 7 — this step only proves the registration/build wiring works):

```cpp
#include "plugin.hpp"
#include "LoopingAdsr.hpp"

struct AI003Module : rack::Module {
    enum ParamId  { PARAMS_LEN };
    enum InputId  { INPUTS_LEN };
    enum OutputId { OUTPUTS_LEN };
    enum LightId  { LIGHTS_LEN };

    AI003Module() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
    }

    void process(const rack::Module::ProcessArgs& /*args*/) override {}
};

struct AI003Widget : rack::ModuleWidget {
    explicit AI003Widget(AI003Module* module) {
        setModule(module);
        setPanel(rack::createPanel(
            rack::asset::plugin(pluginInstance, "res/AI003-black.svg")));
    }
};

rack::Model* modelAI003 = rack::createModel<AI003Module, AI003Widget>("AI003_LoopingADSR");
```

- [ ] **Step 5: Add a placeholder panel so the build doesn't crash on a missing asset**

Create `res/AI003-black.svg` (replaced with the real panel in Task 8 — this is only a stand-in so the module loads):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg"
     width="40.64mm" height="128.5mm"
     viewBox="0 0 40.64 128.5">
  <rect width="40.64" height="128.5" fill="#111111"/>
  <text x="20.32" y="10" text-anchor="middle"
        font-family="sans-serif" font-size="3" font-weight="bold" fill="#ffffff">AI003</text>
</svg>
```

- [ ] **Step 6: Add the new source file to the plugin Makefile**

In `Makefile`, update `SOURCES`:

```makefile
RACK_DIR ?= ../Rack2SDK/RackSDK
SLUG = AISynthesis
VERSION = 2.0.0
FLAGS += -std=c++17
SOURCES += src/plugin.cpp src/AI001.cpp src/AI003.cpp
DISTRIBUTABLES += res plugin.json
include $(RACK_DIR)/plugin.mk
```

- [ ] **Step 7: Build the whole plugin**

Run: `cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001" && make clean && make`
Expected: build succeeds with no errors, producing `plugin.dll` (or platform equivalent).

- [ ] **Step 8: Commit**

```bash
cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001"
git add src/AI003.cpp src/plugin.hpp src/plugin.cpp plugin.json Makefile res/AI003-black.svg
git commit -m "feat: register empty AI003 module, prove build wiring works"
```

---

## Task 7: AI003Module — full params/inputs/polyphony wiring

**Files:**
- Modify: `src/AI003.cpp`

- [ ] **Step 1: Replace AI003Module with the full implementation**

Replace the `AI003Module` struct in `src/AI003.cpp` (keep `AI003Widget` and the `modelAI003` line below it unchanged for now — Task 8 replaces the widget):

```cpp
#include "plugin.hpp"
#include "LoopingAdsr.hpp"
#include <array>

struct AI003Module : rack::Module {
    enum ParamId {
        ATTACK_PARAM, DECAY_PARAM, SUSTAIN_PARAM, RELEASE_PARAM,
        MODE_PARAM, MANUAL_PARAM,
        PARAMS_LEN
    };
    enum InputId  { TRIGGER_INPUT, GATE_INPUT, INPUTS_LEN };
    enum OutputId { ENV_OUTPUT, OUTPUTS_LEN };
    enum LightId  { ENV_LIGHT, LIGHTS_LEN };

    static constexpr int MAX_CHANNELS = 16;
    std::array<LoopingAdsr, MAX_CHANNELS> engines_;
    std::array<rack::dsp::SchmittTrigger, MAX_CHANNELS> triggerEdges_;
    std::array<rack::dsp::SchmittTrigger, MAX_CHANNELS> gateEdges_;
    rack::dsp::SchmittTrigger manualEdge_;

    AI003Module() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(ATTACK_PARAM, 0.f, 1.f, 0.f, "Attack");
        configParam(DECAY_PARAM, 0.f, 1.f, 0.f, "Decay");
        configParam(SUSTAIN_PARAM, 0.f, 10.f, 5.f, "Sustain", "V");
        configParam(RELEASE_PARAM, 0.f, 1.f, 0.f, "Release");
        configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Mode", {"Envelope", "Loop"});
        configButton(MANUAL_PARAM, "Manual trigger");
        configInput(TRIGGER_INPUT, "Trigger");
        configInput(GATE_INPUT, "Gate");
        configOutput(ENV_OUTPUT, "Envelope");
    }

    static float knobToTime(float knob) {
        return 0.02f * std::pow(700.f, knob);
    }

    int computeChannelCount() const {
        if (inputs[TRIGGER_INPUT].isConnected()) return inputs[TRIGGER_INPUT].getChannels();
        if (inputs[GATE_INPUT].isConnected()) return inputs[GATE_INPUT].getChannels();
        return 1;
    }

    void process(const rack::Module::ProcessArgs& args) override {
        int numChannels = computeChannelCount();
        outputs[ENV_OUTPUT].setChannels(numChannels);

        bool loopMode = params[MODE_PARAM].getValue() > 0.5f;
        float attackTime = knobToTime(params[ATTACK_PARAM].getValue());
        float decayTime = knobToTime(params[DECAY_PARAM].getValue());
        float releaseTime = knobToTime(params[RELEASE_PARAM].getValue());
        float sustainVoltage = params[SUSTAIN_PARAM].getValue();

        bool manualRising = manualEdge_.process(params[MANUAL_PARAM].getValue());
        bool triggerPatched = inputs[TRIGGER_INPUT].isConnected();
        bool gatePatched = inputs[GATE_INPUT].isConnected();

        float ledSum = 0.f;
        for (int c = 0; c < numChannels; c++) {
            float triggerVoltage = inputs[TRIGGER_INPUT].getVoltage(c);
            float gateVoltage = inputs[GATE_INPUT].getVoltage(c);

            LoopingAdsr::Params p{};
            p.sampleTime = args.sampleTime;
            p.loopMode = loopMode;
            p.gatePatched = gatePatched;
            p.gateHigh = gateVoltage >= 1.f;
            p.gateRising = gateEdges_[c].process(gateVoltage);
            p.triggerPatched = triggerPatched;
            p.triggerHigh = triggerVoltage >= 1.f;
            p.triggerRising = triggerEdges_[c].process(triggerVoltage);
            p.manualRising = manualRising;
            p.attackTime = attackTime;
            p.decayTime = decayTime;
            p.releaseTime = releaseTime;
            p.sustainVoltage = sustainVoltage;

            float out = engines_[c].process(p);
            outputs[ENV_OUTPUT].setVoltage(out, c);
            ledSum += out;
        }
        lights[ENV_LIGHT].setBrightness(numChannels > 0 ? (ledSum / numChannels) / 10.f : 0.f);
    }
};
```

**Note:** `rack::dsp::SchmittTrigger::process(voltage)` uses the SDK's default hysteresis thresholds (0.1V low / 1V high), matching how Gate/Trigger edges are conventionally detected in Rack modules.

- [ ] **Step 2: Build**

Run: `cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001" && make clean && make`
Expected: build succeeds with no errors or warnings.

- [ ] **Step 3: Commit**

```bash
cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001"
git add src/AI003.cpp
git commit -m "feat: wire AI003Module params/inputs/polyphony to LoopingAdsr"
```

---

## Task 8: AI003 panel SVG and widget placement

**Files:**
- Modify: `res/AI003-black.svg`
- Modify: `src/AI003.cpp` (replace `AI003Widget`)

**Verified widget coordinates** (mm, from the design doc — do not re-derive, these were confirmed against the actual panel artwork and the official AI003 manual):

| Widget | x (mm) | y (mm) |
|---|---|---|
| Activity LED | 9.13 | 19.9 |
| Attack knob | 29.3 | 19.85 |
| Mode switch | 9.07 | 42.88 |
| Decay knob | 29.32 | 42.9 |
| Manual Trigger button | 9.1 | 65.9 |
| Sustain knob | 29.32 | 65.88 |
| Trigger In jack | 9.1 | 88.9 |
| Release knob | 29.35 | 88.88 |
| Gate In jack | 9.1 | 111.9 |
| Envelope Out jack | 29.35 | 111.9 |

- [ ] **Step 1: Replace the placeholder panel SVG**

Replace `res/AI003-black.svg`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg"
     width="40.64mm" height="128.5mm"
     viewBox="0 0 40.64 128.5">
  <!-- Panel background -->
  <rect width="40.64" height="128.5" fill="#111111"/>
  <!-- Module name -->
  <text x="20.32" y="6" text-anchor="middle"
        font-family="sans-serif" font-size="3" font-weight="bold" fill="#ffffff">AI003</text>
  <text x="20.32" y="10.5" text-anchor="middle"
        font-family="sans-serif" font-size="2.6" fill="#ffffff">LOOPING ADSR</text>

  <!-- Left column: LED, Mode switch, Manual button, Trigger In, Gate In -->
  <circle cx="9.13" cy="19.9"  r="1.5" fill="#330000" stroke="#ff2222" stroke-width="0.3"/>
  <circle cx="9.07" cy="42.88" r="3.9" fill="#222222" stroke="#444444" stroke-width="0.3"/>
  <circle cx="9.1"  cy="65.9"  r="3.7" fill="#222222" stroke="#444444" stroke-width="0.3"/>
  <circle cx="9.1"  cy="88.9"  r="3.5" fill="#222222" stroke="#444444" stroke-width="0.3"/>
  <circle cx="9.1"  cy="111.9" r="3.5" fill="#222222" stroke="#444444" stroke-width="0.3"/>

  <!-- Right column: Attack, Decay, Sustain, Release knobs, Envelope Out -->
  <circle cx="29.3"  cy="19.85" r="3.9" fill="#222222" stroke="#444444" stroke-width="0.3"/>
  <circle cx="29.32" cy="42.9"  r="3.9" fill="#222222" stroke="#444444" stroke-width="0.3"/>
  <circle cx="29.32" cy="65.88" r="3.9" fill="#222222" stroke="#444444" stroke-width="0.3"/>
  <circle cx="29.35" cy="88.88" r="3.9" fill="#222222" stroke="#444444" stroke-width="0.3"/>
  <circle cx="29.35" cy="111.9" r="3.5" fill="#222222" stroke="#444444" stroke-width="0.3"/>

  <!-- Labels -->
  <text x="9.1"  y="26.5"  text-anchor="middle" font-family="sans-serif" font-size="2" fill="#cccccc">ENV OUT</text>
  <text x="29.3" y="26.5"  text-anchor="middle" font-family="sans-serif" font-size="2" fill="#cccccc">ATTACK</text>
  <text x="29.32" y="49.5" text-anchor="middle" font-family="sans-serif" font-size="2" fill="#cccccc">DECAY</text>
  <text x="9.1"  y="72.5"  text-anchor="middle" font-family="sans-serif" font-size="2" fill="#cccccc">MANUAL</text>
  <text x="29.32" y="72.5" text-anchor="middle" font-family="sans-serif" font-size="2" fill="#cccccc">SUSTAIN</text>
  <text x="9.1"  y="95.5"  text-anchor="middle" font-family="sans-serif" font-size="2" fill="#cccccc">RETRIG</text>
  <text x="29.35" y="95.5" text-anchor="middle" font-family="sans-serif" font-size="2" fill="#cccccc">RELEASE</text>
  <text x="9.1"  y="118.5" text-anchor="middle" font-family="sans-serif" font-size="2" fill="#cccccc">GATE</text>
  <text x="29.35" y="118.5" text-anchor="middle" font-family="sans-serif" font-size="2" fill="#cccccc">OUT</text>

  <text x="20.32" y="124" text-anchor="middle"
        font-family="sans-serif" font-size="2.2" fill="#888888">AI Synthesis</text>
</svg>
```

- [ ] **Step 2: Replace AI003Widget**

Replace the `AI003Widget` struct in `src/AI003.cpp`:

```cpp
struct AI003Widget : rack::ModuleWidget {
    explicit AI003Widget(AI003Module* module) {
        setModule(module);
        setPanel(rack::createPanel(
            rack::asset::plugin(pluginInstance, "res/AI003-black.svg")));

        addParam(rack::createParamCentered<rack::RoundLargeBlackKnob>(
            rack::mm2px(rack::math::Vec(29.3f, 19.85f)), module, AI003Module::ATTACK_PARAM));
        addParam(rack::createParamCentered<rack::RoundLargeBlackKnob>(
            rack::mm2px(rack::math::Vec(29.32f, 42.9f)), module, AI003Module::DECAY_PARAM));
        addParam(rack::createParamCentered<rack::RoundLargeBlackKnob>(
            rack::mm2px(rack::math::Vec(29.32f, 65.88f)), module, AI003Module::SUSTAIN_PARAM));
        addParam(rack::createParamCentered<rack::RoundLargeBlackKnob>(
            rack::mm2px(rack::math::Vec(29.35f, 88.88f)), module, AI003Module::RELEASE_PARAM));

        addParam(rack::createParamCentered<rack::CKSS>(
            rack::mm2px(rack::math::Vec(9.07f, 42.88f)), module, AI003Module::MODE_PARAM));
        addParam(rack::createParamCentered<rack::VCVButton>(
            rack::mm2px(rack::math::Vec(9.1f, 65.9f)), module, AI003Module::MANUAL_PARAM));

        addInput(rack::createInputCentered<rack::PJ301MPort>(
            rack::mm2px(rack::math::Vec(9.1f, 88.9f)), module, AI003Module::TRIGGER_INPUT));
        addInput(rack::createInputCentered<rack::PJ301MPort>(
            rack::mm2px(rack::math::Vec(9.1f, 111.9f)), module, AI003Module::GATE_INPUT));
        addOutput(rack::createOutputCentered<rack::PJ301MPort>(
            rack::mm2px(rack::math::Vec(29.35f, 111.9f)), module, AI003Module::ENV_OUTPUT));

        addChild(rack::createLightCentered<rack::SmallLight<rack::RedLight>>(
            rack::mm2px(rack::math::Vec(9.13f, 19.9f)), module, AI003Module::ENV_LIGHT));
    }
};
```

- [ ] **Step 3: Build**

Run: `cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001" && make clean && make`
Expected: build succeeds with no errors or warnings.

- [ ] **Step 4: Commit**

```bash
cd "C:\Users\abelo\Documents\Claude\VCV Rack\ai001"
git add res/AI003-black.svg src/AI003.cpp
git commit -m "feat: add AI003 panel graphic and widget placement"
```

---

## Task 9: Manual verification in Rack

**Files:** none (verification only)

- [ ] **Step 1: Install the built plugin and launch Rack**

Copy the built plugin folder to Rack's user plugin directory (or use Rack's own `make install`/dev-plugin mechanism, whichever this machine's Rack setup already uses), then launch VCV Rack.

- [ ] **Step 2: Verify panel wiring**

Add an AI003 module from the module browser (search "AI003" or "Looping ADSR"). Confirm all 10 widgets appear in the correct positions matching the real panel: LED, 4 knobs, Mode switch, Manual button, Trigger In, Gate In, Envelope Out.

- [ ] **Step 3: Verify Envelope mode, no gate**

Press Manual with nothing patched to Gate In. Confirm the LED flashes and Envelope Out produces a single Attack→Decay pulse to 0V (use a scope module or the LED brightness itself). Turn the Sustain knob and confirm it has no effect on this behavior.

- [ ] **Step 4: Verify Envelope mode, gate patched**

Patch a gate/LFO into Gate In. Confirm Attack→Decay→Sustain (held at the Sustain knob's voltage while gate is high)→Release (on gate low) plays out correctly.

- [ ] **Step 5: Verify Loop mode**

Flip Mode to Loop with nothing patched — confirm Envelope Out free-runs as an LFO-style Attack/Decay cycle. Patch a gate into Gate In while in Loop mode — confirm it only runs while the gate is high, and finishes its current Decay before stopping (not a hard cutoff).

- [ ] **Step 6: Verify polyphony**

Patch a polyphonic cable (e.g. from a polyphonic sequencer or MIDI-to-CV) into Trigger In with N channels — confirm Envelope Out becomes an N-channel polyphonic cable with independent envelopes per channel. Additionally patch a *different* channel-count poly cable into Gate In and confirm Trigger In's channel count wins (per the design's trigger-priority rule).

- [ ] **Step 7: Report results**

If any step fails, note the exact discrepancy (which widget, which behavior) — do not silently patch around it. This is the last task in the plan; once all steps pass, the module is complete per the design doc and PRD.
