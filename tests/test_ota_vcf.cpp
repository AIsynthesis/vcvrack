#include <cassert>
#include <cmath>
#include <cstdio>
#include "../src/OtaVcf.hpp"

static constexpr float kSampleRate  = 48000.f;
static constexpr int   kNumSamples  = 4800;   // 100ms of audio

static float rms(const float* buf, int n) {
    float sum = 0.f;
    for (int i = 0; i < n; ++i) sum += buf[i] * buf[i];
    return std::sqrt(sum / n);
}

static float sine_sample(float freq_hz, int i) {
    return std::sin(2.f * 3.14159265f * freq_hz * static_cast<float>(i) / kSampleRate);
}

// LP mode: 100Hz signal through 1kHz-cutoff filter should pass with minimal attenuation.
static void test_lp_passes_low_freq() {
    OtaVcf vcf;
    float out[kNumSamples];
    for (int i = 0; i < kNumSamples; ++i)
        out[i] = vcf.process(sine_sample(100.f, i), 1000.f, 0.f, kSampleRate, OtaVcf::Mode::LP);
    float amplitude = rms(out + kNumSamples / 2, kNumSamples / 2);  // skip transient
    assert(amplitude > 0.3f);
    printf("PASS: LP passes low frequency (rms=%.3f)\n", amplitude);
}

// LP mode: 8kHz signal through 500Hz-cutoff filter should be strongly attenuated vs. 100Hz.
static void test_lp_attenuates_high_freq() {
    OtaVcf vcf_hi, vcf_lo;
    float out_hi[kNumSamples], out_lo[kNumSamples];
    for (int i = 0; i < kNumSamples; ++i) {
        out_hi[i] = vcf_hi.process(sine_sample(8000.f, i), 500.f, 0.f, kSampleRate, OtaVcf::Mode::LP);
        out_lo[i] = vcf_lo.process(sine_sample(100.f,  i), 500.f, 0.f, kSampleRate, OtaVcf::Mode::LP);
    }
    float hi = rms(out_hi + kNumSamples / 2, kNumSamples / 2);
    float lo = rms(out_lo + kNumSamples / 2, kNumSamples / 2);
    assert(hi < lo * 0.4f);
    printf("PASS: LP attenuates high frequency (hi_rms=%.3f lo_rms=%.3f)\n", hi, lo);
}

// HP mode: 100Hz signal through 1kHz-cutoff filter should be attenuated.
static void test_hp_attenuates_low_freq() {
    OtaVcf vcf;
    float out[kNumSamples];
    for (int i = 0; i < kNumSamples; ++i)
        out[i] = vcf.process(sine_sample(100.f, i), 1000.f, 0.f, kSampleRate, OtaVcf::Mode::HP);
    float amplitude = rms(out + kNumSamples / 2, kNumSamples / 2);
    assert(amplitude < 0.3f);
    printf("PASS: HP attenuates low frequency (rms=%.3f)\n", amplitude);
}

// HP mode: 8kHz signal through 1kHz-cutoff filter should pass.
static void test_hp_passes_high_freq() {
    OtaVcf vcf;
    float out[kNumSamples];
    for (int i = 0; i < kNumSamples; ++i)
        out[i] = vcf.process(sine_sample(8000.f, i), 1000.f, 0.f, kSampleRate, OtaVcf::Mode::HP);
    float amplitude = rms(out + kNumSamples / 2, kNumSamples / 2);
    assert(amplitude > 0.3f);
    printf("PASS: HP passes high frequency (rms=%.3f)\n", amplitude);
}

// Self-oscillation: max resonance + silence input -> non-zero output after ring-up.
static void test_self_oscillation() {
    OtaVcf vcf;
    const int kRingUp = 48000;  // 1 second to ring up
    for (int i = 0; i < kRingUp; ++i)
        vcf.process(0.f, 1000.f, 1.0f, kSampleRate, OtaVcf::Mode::LP);
    float out[kNumSamples];
    for (int i = 0; i < kNumSamples; ++i)
        out[i] = vcf.process(0.f, 1000.f, 1.0f, kSampleRate, OtaVcf::Mode::LP);
    float amplitude = rms(out, kNumSamples);
    assert(amplitude > 0.01f);
    printf("PASS: Self-oscillation at max resonance (rms=%.4f)\n", amplitude);
}

// process() must never produce NaN or Inf across a range of valid inputs.
static void test_no_nan_inf() {
    OtaVcf vcf;
    for (int i = 0; i < 10000; ++i) {
        float in  = (i % 3 == 0) ? 5.f : (i % 3 == 1) ? -5.f : 0.f;
        float res = static_cast<float>(i % 100) / 99.f;
        float y   = vcf.process(in, 1000.f, res, kSampleRate, OtaVcf::Mode::LP);
        assert(!std::isnan(y) && !std::isinf(y));
    }
    printf("PASS: No NaN/Inf produced\n");
}

int main() {
    test_lp_passes_low_freq();
    test_lp_attenuates_high_freq();
    test_hp_attenuates_low_freq();
    test_hp_passes_high_freq();
    test_self_oscillation();
    test_no_nan_inf();
    printf("\nAll tests passed.\n");
    return 0;
}
