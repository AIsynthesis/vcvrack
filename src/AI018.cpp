#include "plugin.hpp"
#include "MatrixMixer.hpp"
#include "AI018Layout.hpp"

// AI018 Stereo Matrix Mixer — 4 stereo ins × 4 stereo outs, 16 level pots.
// Mixing maths lives in MatrixMixer.hpp; this file maps the panel onto it.
// Polyphonic: each channel is mixed independently (mono cables broadcast).

namespace {
rack::math::Vec mm(ai018layout::Pos p) { return rack::mm2px(rack::math::Vec(p.x, p.y)); }
const char* kOutNames[ai018::kOutputs] = {"A", "B", "C", "D"};
}  // namespace

struct AI018Module : rack::Module {
    // Params: KNOB_<input><output>, row-major (1A 1B 1C 1D 2A ...).
    enum ParamId  { PARAMS_LEN = ai018::kInputs * ai018::kOutputs };
    // Inputs: L1 R1 L2 R2 L3 R3 L4 R4.
    enum InputId  { INPUTS_LEN = ai018::kInputs * 2 };
    // Outputs: LA RA LB RB LC RC LD RD.
    enum OutputId { OUTPUTS_LEN = ai018::kOutputs * 2 };

    static int knob(int in, int out) { return in * ai018::kOutputs + out; }
    static int inL(int in)   { return in * 2; }
    static int inR(int in)   { return in * 2 + 1; }
    static int outL(int out) { return out * 2; }
    static int outR(int out) { return out * 2 + 1; }

    AI018Module() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN);
        for (int i = 0; i < ai018::kInputs; i++) {
            for (int o = 0; o < ai018::kOutputs; o++)
                configParam(knob(i, o), 0.f, 1.f, 0.f,
                            rack::string::f("Input %d → Out %s", i + 1, kOutNames[o]), "%", 0.f, 100.f);
            configInput(inL(i), rack::string::f("Input %d left", i + 1));
            configInput(inR(i), rack::string::f("Input %d right (normalled to left)", i + 1));
        }
        for (int o = 0; o < ai018::kOutputs; o++) {
            configOutput(outL(o), rack::string::f("Out %s left", kOutNames[o]));
            configOutput(outR(o), rack::string::f("Out %s right", kOutNames[o]));
        }
    }

    int channelCount() {
        int n = 1;
        for (int p = 0; p < INPUTS_LEN; p++) n = std::max(n, inputs[p].getChannels());
        return n;
    }

    void process(const rack::Module::ProcessArgs&) override {
        float gains[ai018::kInputs][ai018::kOutputs];
        for (int i = 0; i < ai018::kInputs; i++)
            for (int o = 0; o < ai018::kOutputs; o++)
                gains[i][o] = params[knob(i, o)].getValue();

        const int channels = channelCount();
        for (int o = 0; o < OUTPUTS_LEN; o++) outputs[o].setChannels(channels);

        for (int c = 0; c < channels; c++) {
            ai018::StereoIn in[ai018::kInputs];
            for (int i = 0; i < ai018::kInputs; i++) {
                in[i].left  = inputs[inL(i)].getPolyVoltage(c);
                in[i].right = inputs[inR(i)].getPolyVoltage(c);
                in[i].rightPatched = inputs[inR(i)].isConnected();
            }
            ai018::StereoOut out[ai018::kOutputs];
            ai018::mix(in, gains, out);
            for (int o = 0; o < ai018::kOutputs; o++) {
                outputs[outL(o)].setVoltage(out[o].left, c);
                outputs[outR(o)].setVoltage(out[o].right, c);
            }
        }
    }
};

struct AI018Widget : rack::ModuleWidget {
    explicit AI018Widget(AI018Module* module) {
        using namespace ai018layout;
        setModule(module);
        setPanel(rack::createPanel(rack::asset::plugin(pluginInstance, "res/AI018-black.svg")));

        for (Pos p : {SCREW_TL, SCREW_TR, SCREW_BL, SCREW_BR})
            addChild(rack::createWidgetCentered<rack::ScrewBlack>(mm(p)));

        const Pos knobs[4][4] = {{KNOB_1A, KNOB_1B, KNOB_1C, KNOB_1D},
                                 {KNOB_2A, KNOB_2B, KNOB_2C, KNOB_2D},
                                 {KNOB_3A, KNOB_3B, KNOB_3C, KNOB_3D},
                                 {KNOB_4A, KNOB_4B, KNOB_4C, KNOB_4D}};
        for (int i = 0; i < 4; i++)
            for (int o = 0; o < 4; o++)
                addParam(rack::createParamCentered<rack::RoundLargeBlackKnob>(
                    mm(knobs[i][o]), module, AI018Module::knob(i, o)));

        const Pos insL[4] = {IN_L1, IN_L2, IN_L3, IN_L4};
        const Pos insR[4] = {IN_R1, IN_R2, IN_R3, IN_R4};
        for (int i = 0; i < 4; i++) {
            addInput(rack::createInputCentered<rack::PJ301MPort>(mm(insL[i]), module, AI018Module::inL(i)));
            addInput(rack::createInputCentered<rack::PJ301MPort>(mm(insR[i]), module, AI018Module::inR(i)));
        }

        const Pos outsL[4] = {OUT_LA, OUT_LB, OUT_LC, OUT_LD};
        const Pos outsR[4] = {OUT_RA, OUT_RB, OUT_RC, OUT_RD};
        for (int o = 0; o < 4; o++) {
            addOutput(rack::createOutputCentered<rack::PJ301MPort>(mm(outsL[o]), module, AI018Module::outL(o)));
            addOutput(rack::createOutputCentered<rack::PJ301MPort>(mm(outsR[o]), module, AI018Module::outR(o)));
        }
    }
};

rack::Model* modelAI018 = rack::createModel<AI018Module, AI018Widget>("AI018_StereoMatrixMixer");
