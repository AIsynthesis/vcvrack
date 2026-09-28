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
        else processLoopMode(p);
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

    void enterAttack(const Params& p)  { setStage(State::ATTACK, kCeiling, p.attackTime); }
    void enterDecay(const Params& p) {
        float decayTarget = (!p.loopMode && p.gatePatched && p.gateHigh) ? p.sustainVoltage : 0.f;
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
