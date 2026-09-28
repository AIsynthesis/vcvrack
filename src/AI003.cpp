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

    int computeChannelCount() {
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

struct AI003Widget : rack::ModuleWidget {
    explicit AI003Widget(AI003Module* module) {
        setModule(module);
        setPanel(rack::createPanel(
            rack::asset::plugin(pluginInstance, "res/AI003-black.svg")));

        addChild(rack::createWidgetCentered<rack::ScrewBlack>(
            rack::mm2px(rack::math::Vec(7.725f, 3.175f))));
        addChild(rack::createWidgetCentered<rack::ScrewBlack>(
            rack::mm2px(rack::math::Vec(33.075f, 3.175f))));
        addChild(rack::createWidgetCentered<rack::ScrewBlack>(
            rack::mm2px(rack::math::Vec(7.725f, 125.7f))));
        addChild(rack::createWidgetCentered<rack::ScrewBlack>(
            rack::mm2px(rack::math::Vec(33.075f, 125.7f))));

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

        addChild(rack::createLightCentered<rack::LargeLight<rack::RedLight>>(
            rack::mm2px(rack::math::Vec(9.13f, 19.9f)), module, AI003Module::ENV_LIGHT));
    }
};

rack::Model* modelAI003 = rack::createModel<AI003Module, AI003Widget>("AI003_LoopingADSR");
