#include "plugin.hpp"
#include "BxrEngine.hpp"
#include "AI250Layout.hpp"

// AI250 BXR — VCV Rack version of the Collision BXR firmware.
// All DSP lives in BxrEngine.hpp; this file only maps panel controls to it.

namespace {

// Stepped mode knobs sit at the centre of each firmware step, so CV added on top
// moves through the steps exactly like the hardware's knob + CV sum does.
float stepToNormalized(float step, int count) {
    return (step + 0.5f) / static_cast<float>(count);
}

rack::math::Vec mm(ai250layout::Pos p) {
    return rack::mm2px(rack::math::Vec(p.x, p.y));
}

}  // namespace

struct AI250Module : rack::Module {
    enum ParamId {
        ARG_PARAM, FUNC_PARAM, WAVE_PARAM,
        IN1VOL_PARAM, CENTER_PARAM, DIST_PARAM, MIX_PARAM,
        SWAP_PARAM, IN2OSC_PARAM, SPEED_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        ARG_CV_INPUT, FUNC_CV_INPUT, WAVE_CV_INPUT, VOCT_INPUT,
        DIST_CV_INPUT, MIX_CV_INPUT, IN1_INPUT, IN2_INPUT,
        INPUTS_LEN
    };
    enum OutputId { GATE1_OUTPUT, GATE2_OUTPUT, OSC_OUTPUT, MAIN_OUTPUT, OUTPUTS_LEN };
    enum LightId  { GATE1_LIGHT, GATE2_LIGHT, LIGHTS_LEN };

    bxr::Engine engine_;

    AI250Module() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configSwitch(ARG_PARAM, 0.f, kArgModeCount - 1, 2.f, "Argument",
            {"A", "B", "A + B", "A − B", "B − A", "A × B", "√(A² + B²)", "A / |B|", "10A / |B|"});
        configSwitch(FUNC_PARAM, 0.f, kFuncModeCount - 1, 2.f, "Function",
            {"ln|x|", "√|x|", "x (pass)", "x²", "−dx/dt ×100", "−dx/dt"});
        configSwitch(WAVE_PARAM, 0.f, kWaveModeCount - 1, 0.f, "Osc shape",
            {"Sine", "Triangle", "Ramp up", "Ramp down", "Square",
             "Sine (positive half)", "Sine (negative half)", "Exp up", "Exp down", "Sample & hold"});
        getParamQuantity(ARG_PARAM)->snapEnabled  = true;
        getParamQuantity(FUNC_PARAM)->snapEnabled = true;
        getParamQuantity(WAVE_PARAM)->snapEnabled = true;

        configParam(IN1VOL_PARAM, 0.f, 1.f, 1.f, "IN1 level", "%", 0.f, 100.f);
        configParam(CENTER_PARAM, 0.f, 1.f, 0.7f, "IN2 level / Osc pitch", "%", 0.f, 100.f);
        configParam(DIST_PARAM,   0.f, 1.f, 0.f, "Distortion", "%", 0.f, 100.f);
        configParam(MIX_PARAM,    0.f, 1.f, 1.f, "Dry/wet blend", "%", 0.f, 100.f);

        configSwitch(SWAP_PARAM,   0.f, 1.f, 0.f, "Swap inputs 1/2", {"Normal", "Swapped"});
        // Hardware toggles: IN 2 up / OSC down, AUDIO RATE up / LFO down (CKSS value 1 = lever up).
        configSwitch(IN2OSC_PARAM, 0.f, 1.f, 0.f, "Source B", {"Oscillator", "IN2 audio"});
        configSwitch(SPEED_PARAM,  0.f, 1.f, 1.f, "Oscillator rate", {"LFO", "Audio (C1–C7)"});

        configInput(ARG_CV_INPUT,  "Argument CV");
        configInput(FUNC_CV_INPUT, "Function CV");
        configInput(WAVE_CV_INPUT, "Osc shape CV");
        configInput(VOCT_INPUT,    "Osc pitch (V/Oct)");
        configInput(DIST_CV_INPUT, "Distortion CV");
        configInput(MIX_CV_INPUT,  "Dry/wet CV");
        configInput(IN1_INPUT,     "Audio IN1 (signal A)");
        configInput(IN2_INPUT,     "Audio IN2 (signal B)");

        configOutput(GATE1_OUTPUT, "Geiger gate 1");
        configOutput(GATE2_OUTPUT, "Geiger gate 2");
        configOutput(OSC_OUTPUT,   "Oscillator");
        configOutput(MAIN_OUTPUT,  "Out");

        configBypass(IN1_INPUT, MAIN_OUTPUT);

        engine_.seed(static_cast<uint32_t>(rack::random::u32()));
        engine_.reset();
    }

    void onReset() override {
        engine_.reset();
    }

    bxr::PanelState readPanel() {
        bxr::PanelState p;
        p.argKnob    = stepToNormalized(params[ARG_PARAM].getValue(),  kArgModeCount);
        p.funcKnob   = stepToNormalized(params[FUNC_PARAM].getValue(), kFuncModeCount);
        p.waveKnob   = stepToNormalized(params[WAVE_PARAM].getValue(), kWaveModeCount);
        p.in1VolKnob = params[IN1VOL_PARAM].getValue();
        p.centerKnob = params[CENTER_PARAM].getValue();
        p.distKnob   = params[DIST_PARAM].getValue();
        p.mixKnob    = params[MIX_PARAM].getValue();

        p.argCvVolts  = inputs[ARG_CV_INPUT].getVoltage();
        p.funcCvVolts = inputs[FUNC_CV_INPUT].getVoltage();
        p.waveCvVolts = inputs[WAVE_CV_INPUT].getVoltage();
        p.voctVolts   = inputs[VOCT_INPUT].getVoltage();
        p.distCvVolts = inputs[DIST_CV_INPUT].getVoltage();
        p.mixCvVolts  = inputs[MIX_CV_INPUT].getVoltage();

        p.swapInputs    = params[SWAP_PARAM].getValue()   > 0.5f;
        p.useOscillator = params[IN2OSC_PARAM].getValue() < 0.5f;
        p.lfoMode       = params[SPEED_PARAM].getValue()  < 0.5f;
        return p;
    }

    void process(const rack::Module::ProcessArgs& args) override {
        const bxr::ControlState cs = bxr::buildControlState(readPanel());
        const bxr::Frame f = engine_.process(
            inputs[IN1_INPUT].getVoltage(), inputs[IN2_INPUT].getVoltage(), cs, args.sampleRate);

        outputs[MAIN_OUTPUT].setVoltage(f.outVolts);
        outputs[OSC_OUTPUT].setVoltage(f.oscVolts);
        outputs[GATE1_OUTPUT].setVoltage(f.gate1Volts);
        outputs[GATE2_OUTPUT].setVoltage(f.gate2Volts);

        lights[GATE1_LIGHT].setBrightnessSmooth(f.gate1 ? 1.f : 0.f, args.sampleTime);
        lights[GATE2_LIGHT].setBrightnessSmooth(f.gate2 ? 1.f : 0.f, args.sampleTime);
    }
};

// Stepped mode knob whose detents line up with the step labels printed round the
// hardware knob: each step sits at the centre of its zone of a 300° sweep.
struct SteppedKnob : rack::RoundLargeBlackKnob {
    void setSteps(int steps) {
        const float half = 0.83f * static_cast<float>(M_PI);
        minAngle = -half + half / steps;
        maxAngle =  half - half / steps;
    }
};

template <class TKnob>
TKnob* addSteppedKnob(rack::ModuleWidget* w, rack::math::Vec pos, AI250Module* module, int paramId, int steps) {
    TKnob* knob = rack::createParamCentered<TKnob>(pos, module, paramId);
    knob->setSteps(steps);
    w->addParam(knob);
    return knob;
}

struct AI250Widget : rack::ModuleWidget {
    explicit AI250Widget(AI250Module* module) {
        using namespace ai250layout;
        setModule(module);
        setPanel(rack::createPanel(rack::asset::plugin(pluginInstance, "res/AI250-black.svg")));

        addChild(rack::createWidgetCentered<rack::ScrewBlack>(mm(SCREW_TL)));
        addChild(rack::createWidgetCentered<rack::ScrewBlack>(mm(SCREW_TR)));
        addChild(rack::createWidgetCentered<rack::ScrewBlack>(mm(SCREW_BL)));
        addChild(rack::createWidgetCentered<rack::ScrewBlack>(mm(SCREW_BR)));

        addSteppedKnob<SteppedKnob>(this, mm(KNOB_ARG),  module, AI250Module::ARG_PARAM,  kArgModeCount);
        addSteppedKnob<SteppedKnob>(this, mm(KNOB_FUNC), module, AI250Module::FUNC_PARAM, kFuncModeCount);
        addSteppedKnob<SteppedKnob>(this, mm(KNOB_WAVE), module, AI250Module::WAVE_PARAM, kWaveModeCount);

        addParam(rack::createParamCentered<rack::RoundLargeBlackKnob>(mm(KNOB_IN1VOL), module, AI250Module::IN1VOL_PARAM));
        addParam(rack::createParamCentered<rack::RoundLargeBlackKnob>(mm(KNOB_DIST),   module, AI250Module::DIST_PARAM));
        addParam(rack::createParamCentered<rack::RoundLargeBlackKnob>(mm(KNOB_CENTER), module, AI250Module::CENTER_PARAM));
        addParam(rack::createParamCentered<rack::RoundLargeBlackKnob>(mm(KNOB_MIX),    module, AI250Module::MIX_PARAM));

        addParam(rack::createParamCentered<rack::CKSS>(mm(SWITCH_IN2OSC), module, AI250Module::IN2OSC_PARAM));
        addParam(rack::createParamCentered<rack::CKSS>(mm(SWITCH_SPEED),  module, AI250Module::SPEED_PARAM));
        addParam(rack::createParamCentered<rack::CKSS>(mm(SWITCH_SWAP),   module, AI250Module::SWAP_PARAM));

        addInput(rack::createInputCentered<rack::PJ301MPort>(mm(IN_VOCT),    module, AI250Module::VOCT_INPUT));
        addInput(rack::createInputCentered<rack::PJ301MPort>(mm(IN_WAVE_CV), module, AI250Module::WAVE_CV_INPUT));
        addInput(rack::createInputCentered<rack::PJ301MPort>(mm(IN_MIX_CV),  module, AI250Module::MIX_CV_INPUT));
        addInput(rack::createInputCentered<rack::PJ301MPort>(mm(IN_ARG_CV),  module, AI250Module::ARG_CV_INPUT));
        addInput(rack::createInputCentered<rack::PJ301MPort>(mm(IN_FUNC_CV), module, AI250Module::FUNC_CV_INPUT));
        addInput(rack::createInputCentered<rack::PJ301MPort>(mm(IN_DIST_CV), module, AI250Module::DIST_CV_INPUT));
        addInput(rack::createInputCentered<rack::PJ301MPort>(mm(IN_IN1),     module, AI250Module::IN1_INPUT));
        addInput(rack::createInputCentered<rack::PJ301MPort>(mm(IN_IN2),     module, AI250Module::IN2_INPUT));

        addOutput(rack::createOutputCentered<rack::PJ301MPort>(mm(OUT_GATE1), module, AI250Module::GATE1_OUTPUT));
        addOutput(rack::createOutputCentered<rack::PJ301MPort>(mm(OUT_GATE2), module, AI250Module::GATE2_OUTPUT));
        addOutput(rack::createOutputCentered<rack::PJ301MPort>(mm(OUT_OSC),   module, AI250Module::OSC_OUTPUT));
        addOutput(rack::createOutputCentered<rack::PJ301MPort>(mm(OUT_OUT),   module, AI250Module::MAIN_OUTPUT));

        addChild(rack::createLightCentered<rack::MediumLight<rack::RedLight>>(mm(LIGHT_GATE1), module, AI250Module::GATE1_LIGHT));
        addChild(rack::createLightCentered<rack::MediumLight<rack::RedLight>>(mm(LIGHT_GATE2), module, AI250Module::GATE2_LIGHT));
    }
};

rack::Model* modelAI250 = rack::createModel<AI250Module, AI250Widget>("AI250_BXR");
