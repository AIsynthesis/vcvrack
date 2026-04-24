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
    float computeCutoffHz() {
        float log2_fc = params[CUTOFF_PARAM].getValue();
        float cv_v    = inputs[CV_IN].getVoltage();
        float cv_amt  = params[CVAMT_PARAM].getValue();
        float fc      = std::pow(2.f, log2_fc + cv_amt * cv_v);
        return rack::clamp(fc, 20.f, 20000.f);
    }
};

// ─── Widget ────────────────────────────────────────────────────────────────────

struct AI004Widget : rack::ModuleWidget {
    explicit AI004Widget(AI004Module* module) {
        setModule(module);
        setPanel(rack::createPanel(
            rack::asset::plugin(pluginInstance, "res/panel-aluminum.svg")));

        // Screws at standard 8HP corner positions (mm coordinates)
        addChild(rack::createWidget<rack::ScrewSilver>(rack::mm2px(rack::Vec( 1.5f,   1.5f))));
        addChild(rack::createWidget<rack::ScrewSilver>(rack::mm2px(rack::Vec(29.0f,   1.5f))));
        addChild(rack::createWidget<rack::ScrewSilver>(rack::mm2px(rack::Vec( 1.5f, 117.0f))));
        addChild(rack::createWidget<rack::ScrewSilver>(rack::mm2px(rack::Vec(29.0f, 117.0f))));

        // Knobs — positions match SVG label positions exactly
        addParam(rack::createParamCentered<rack::RoundBlackKnob>(
            rack::mm2px(rack::Vec(11.f, 35.f)), module, AI004Module::CUTOFF_PARAM));
        addParam(rack::createParamCentered<rack::RoundBlackKnob>(
            rack::mm2px(rack::Vec(29.f, 35.f)), module, AI004Module::RESONANCE_PARAM));
        addParam(rack::createParamCentered<rack::RoundBlackKnob>(
            rack::mm2px(rack::Vec(11.f, 68.f)), module, AI004Module::CVAMT_PARAM));

        // LP/HP two-position toggle switch
        addParam(rack::createParamCentered<rack::CKSS>(
            rack::mm2px(rack::Vec(27.f, 69.f)), module, AI004Module::LPSWITCH_PARAM));

        // Jacks
        addInput(rack::createInputCentered<rack::PJ301MPort>(
            rack::mm2px(rack::Vec( 8.f,   102.f)), module, AI004Module::AUDIO_IN));
        addInput(rack::createInputCentered<rack::PJ301MPort>(
            rack::mm2px(rack::Vec(20.32f, 102.f)), module, AI004Module::CV_IN));
        addOutput(rack::createOutputCentered<rack::PJ301MPort>(
            rack::mm2px(rack::Vec(32.f,   102.f)), module, AI004Module::AUDIO_OUT));
    }

    void step() override {
        rack::ModuleWidget::step();
        if (!module) return;

        auto* m = dynamic_cast<AI004Module*>(module);
        if (!m) return;

        // Reload SVG panel only when skin selection changes — avoids reloading every frame.
        if (m->panel_skin != last_skin_) {
            last_skin_ = m->panel_skin;
            const char* path = (m->panel_skin == 1)
                ? "res/panel-black.svg"
                : "res/panel-aluminum.svg";
            setPanel(rack::createPanel(rack::asset::plugin(pluginInstance, path)));
        }
    }

    void appendContextMenu(rack::Menu* menu) override {
        auto* m = dynamic_cast<AI004Module*>(module);
        if (!m) return;

        menu->addChild(new rack::MenuSeparator);
        menu->addChild(rack::createMenuLabel("Panel skin"));
        menu->addChild(rack::createCheckMenuItem("Aluminum", "",
            [m] { return m->panel_skin == 0; },
            [m] { m->panel_skin = 0; }));
        menu->addChild(rack::createCheckMenuItem("Black", "",
            [m] { return m->panel_skin == 1; },
            [m] { m->panel_skin = 1; }));
    }

private:
    int last_skin_ = -1;  // tracks last applied skin to avoid reloading SVG every frame
};

// ─── Model registration ────────────────────────────────────────────────────────
rack::Model* modelAI004 = rack::createModel<AI004Module, AI004Widget>("AI004_OTA_VCF");
