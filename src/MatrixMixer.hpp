#pragma once
// AI018 Stereo Matrix Mixer — platform-independent mixing core (no Rack includes).
//
// Hardware behaviour (AI018 build guide):
//   * 4 stereo inputs. Each R jack is normalled to its L jack: with nothing in R,
//     the L signal is duplicated onto the R channel.
//   * 16 pots: row = input pair 1–4, column = output A–D. Each output is the
//     stereo sum of inputs 1–4 weighted by that column's four pots.
//   * DC-coupled, so it mixes audio and CV alike.

#include <algorithm>

namespace ai018 {

static constexpr int kInputs  = 4;
static constexpr int kOutputs = 4;
// Op-amp outputs on ±12 V rails can't swing past roughly ±11 V.
static constexpr float kRailVolts = 11.0f;

struct StereoIn {
    float left  = 0.0f;
    float right = 0.0f;
    bool  rightPatched = false;
};

struct StereoOut {
    float left  = 0.0f;
    float right = 0.0f;
};

// gains[input][output], each 0–1 (pot fully clockwise = unity).
inline void mix(const StereoIn (&in)[kInputs], const float (&gains)[kInputs][kOutputs],
                StereoOut (&out)[kOutputs]) {
    for (int o = 0; o < kOutputs; o++) {
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < kInputs; i++) {
            const float inR = in[i].rightPatched ? in[i].right : in[i].left;  // R normalled to L
            l += gains[i][o] * in[i].left;
            r += gains[i][o] * inR;
        }
        out[o].left  = std::clamp(l, -kRailVolts, kRailVolts);
        out[o].right = std::clamp(r, -kRailVolts, kRailVolts);
    }
}

}  // namespace ai018
