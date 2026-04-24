#include "plugin.hpp"
#include "OtaVcf.hpp"

// ─── Module struct ─────────────────────────────────────────────────────────────

struct AI004Module : rack::Module {
    enum ParamId {
        CUTOFF_PARAM,
        RESONANCE_PARAM,
        CVAMT_PARAM,
        LPSWITCH_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        AUDIO_IN,
        CV_IN,
        INPUTS_LEN
    };
    enum OutputId {
        AUDIO_OUT,
        OUTPUTS_LEN
    };

    OtaVcf vcf;
    int panel_skin = 0;  // 0 = aluminum, 1 = black

    AI004Module() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN);

        // Cutoff stored as log2(Hz) so the knob maps linearly to octaves (log taper).
        // Display base 2 causes Rack to render the tooltip in Hz automatically.
        configParam(CUTOFF_PARAM, std::log2(20.f), std::log2(20000.f),
                    std::log2(1000.f), "Cutoff", " Hz", 2.f);
        configParam(RESONANCE_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
        configParam(CVAMT_PARAM, -1.f, 1.f, 0.f, "CV Amount");
        configSwitch(LPSWITCH_PARAM, 0.f, 1.f, 0.f, "Filter mode", {"LP", "HP"});

        configInput(AUDIO_IN, "Audio");
        configInput(CV_IN, "CV");
        configOutput(AUDIO_OUT, "Audio");
    }

    void process(const rack::Module::ProcessArgs& args) override {
        float cutoff_hz = computeCutoffHz();
        float resonance = params[RESONANCE_PARAM].getValue();
        OtaVcf::Mode mode = (params[LPSWITCH_PARAM].getValue() >= 0.5f)
                          ? OtaVcf::Mode::HP : OtaVcf::Mode::LP;

        float in  = inputs[AUDIO_IN].getVoltage();
        float out = vcf.process(in, cutoff_hz, resonance, args.sampleRate, mode);
        outputs[AUDIO_OUT].setVoltage(out);
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "panel_skin", json_integer(panel_skin));
        return root;
    }

    void dataFromJson(json_t* root) override {
        json_t* skin = json_object_get(root, "panel_skin");
        if (skin) panel_skin = static_cast<int>(json_integer_value(skin));
    }

private:
    // CV is 1V/oct: adding CV voltage (scaled by CV Amount) to log2(fc) shifts cutoff by octaves.
    float computeCutoffHz() const {
        float log2_fc = params[CUTOFF_PARAM].getValue();
        float cv_v    = inputs[CV_IN].getVoltage();
        float cv_amt  = params[CVAMT_PARAM].getValue();
        float fc      = std::pow(2.f, log2_fc + cv_amt * cv_v);
        return rack::clamp(fc, 20.f, 20000.f);
    }
};

// modelAI004 is referenced in plugin.cpp — defined here after AI004Widget is added in Task 6.
// Placeholder so this file compiles independently:
rack::Model* modelAI004 = nullptr;
