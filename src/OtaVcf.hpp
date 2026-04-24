#pragma once
#include <cmath>
#include <algorithm>

// Two-stage OTA filter modeling the AI004 / Korg MS-20 circuit.
//
// Topology: state-variable filter (SVF) with two integrators.
//   hp = input - diodeSaturation(2R * bp) - lp    (highpass node)
//   bp += g * softSaturate(hp)                     (OTA transconductance stage)
//   lp += g * bp                                   (lowpass integrator)
//
// LP output: lp (12dB/oct).
// HP output: in - hp_lp_ (6dB/oct, one-pole, per spec).
//
// SVF topology with diode saturation in the damping term models MS-20 resonance.
// At resonance=1.0, effective damping goes slightly negative (kExtraDrive > 0),
// making the equilibrium unstable so the filter rings up from thermal noise into
// a stable limit cycle bounded by tanh saturation.
//
// No SDK dependencies -- safe to include in standalone unit tests.
class OtaVcf {
public:
    enum class Mode { LP, HP };

    OtaVcf() { reset(); }

    void reset() {
        bp_    = 0.f;
        lp_    = 1e-6f;  // tiny seed breaks zero equilibrium for self-oscillation ring-up
        hp_lp_ = 0.f;
    }

    // Process one audio sample.
    //   in          -- input sample (any level; +-5V typical in VCV Rack)
    //   cutoff_hz   -- cutoff frequency in Hz, clamped to [20, 20000]
    //   resonance   -- 0..1; self-oscillation begins around 0.8
    //   sample_rate -- host sample rate in Hz
    //   mode        -- LP (12dB/oct output) or HP (6dB/oct output, one-pole)
    float process(float in, float cutoff_hz, float resonance,
                  float sample_rate, Mode mode) {
        float g = computeG(cutoff_hz, sample_rate);

        // One-pole HP section: always runs for state continuity on mode switches.
        // HP output = in - hp_lp_ gives 6dB/oct slope per spec.
        float gn = g / (1.f + g);
        hp_lp_ += gn * (in - hp_lp_);

        // R is damping: 1.0 = critically damped, 0.0 = onset of self-oscillation.
        // kExtraDrive pushes R slightly past zero at full resonance so the filter
        // becomes unstable and rings up from the 1e-6 noise seed.
        float R = (1.0f - resonance) - kExtraDrive;

        // Diode saturation in the damping path creates MS-20 asymmetric character.
        // At high resonance R < 0, this term inverts, providing positive bp feedback
        // that drives self-oscillation. Saturation keeps the limit cycle bounded.
        float damp = diodeSaturation(2.0f * R * bp_);
        float hp   = in - damp - lp_;

        // First integrator: OTA transconductance with soft saturation models overload.
        bp_ += g * softSaturate(hp);

        // Second integrator: LP stage with soft ceiling to prevent state runaway
        // during self-oscillation. Ceiling at 15V is well above normal VCV Rack
        // signal levels (+-5V), so passband signals pass with negligible saturation.
        float lp_next = lp_ + g * bp_;
        lp_ = 15.f * std::tanh(lp_next * (1.f / 15.f));

        return (mode == Mode::LP) ? lp_ : (in - hp_lp_);
    }

private:
    // kExtraDrive slightly past zero makes the equilibrium unstable at resonance=1.
    static constexpr float kExtraDrive = 0.05f;
    static constexpr float kDiodePos   = 1.4f;  // harder clip on positive half
    static constexpr float kDiodeNeg   = 0.8f;  // softer clip on negative half

    float bp_    = 0.f;    // bandpass integrator state
    float lp_    = 1e-6f;  // lowpass integrator state (seeded for self-oscillation ring-up)
    float hp_lp_ = 0.f;    // one-pole LP for HP output path (gives 6dB/oct HP)

    // Bilinear-warped frequency coefficient.
    // Clamping fc before tan() prevents instability near Nyquist.
    static float computeG(float cutoff_hz, float sample_rate) {
        float fc = std::clamp(cutoff_hz, 20.f, 20000.f) / sample_rate;
        fc = std::clamp(fc, 0.001f, 0.499f);
        return std::tan(3.14159265f * fc);
    }

    // tanh-based saturation models OTA transconductance.
    // Pre-scaling by 0.5 preserves near-unity gain for typical VCV Rack levels.
    static float softSaturate(float x) { return std::tanh(x * 0.5f) * 2.0f; }

    // Asymmetric diode pair in the damping/feedback path.
    // Harder clip on positive half creates even harmonics at high resonance.
    static float diodeSaturation(float x) {
        if (x >= 0.f) return std::tanh(x * kDiodePos);
        else          return std::tanh(x * kDiodeNeg);
    }
};
