// Unit tests for the AI018 matrix mixing core. Build: make test_matrix_mixer
#include "MatrixMixer.hpp"
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

int main() {
    using namespace ai018;
    float g[kInputs][kOutputs] = {};
    StereoIn in[kInputs];
    StereoOut out[kOutputs];

    // All pots at zero -> silence.
    in[0] = {3.f, -2.f, true};
    mix(in, g, out);
    for (auto& o : out) CHECK(near(o.left, 0) && near(o.right, 0), "zero gains should be silent");

    // Right normalled to left when R unpatched.
    g[0][1] = 1.f;
    in[0] = {4.f, 0.f, false};
    mix(in, g, out);
    CHECK(near(out[1].left, 4.f) && near(out[1].right, 4.f), "R should copy L when unpatched (%f %f)", out[1].left, out[1].right);

    // Patched right is independent.
    in[0] = {4.f, -1.f, true};
    mix(in, g, out);
    CHECK(near(out[1].right, -1.f), "patched R should pass its own signal");

    // Column sums with per-pot weights; other columns untouched.
    g[0][1] = 0.5f; g[2][1] = 0.25f;
    in[2] = {2.f, 2.f, true};
    mix(in, g, out);
    CHECK(near(out[1].left, 0.5f * 4.f + 0.25f * 2.f), "weighted sum wrong: %f", out[1].left);
    CHECK(near(out[0].left, 0.f) && near(out[3].left, 0.f), "unrelated outputs must stay silent");

    // Output limited to the op-amp swing.
    for (int i = 0; i < kInputs; i++) { g[i][2] = 1.f; in[i] = {10.f, -10.f, true}; }
    mix(in, g, out);
    CHECK(near(out[2].left, kRailVolts) && near(out[2].right, -kRailVolts), "should clip at rails");

    if (!failures) std::printf("All AI018 matrix mixer tests passed.\n");
    return failures ? 1 : 0;
}
