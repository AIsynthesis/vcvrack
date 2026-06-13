#include "plugin.hpp"

// ─── Module ────────────────────────────────────────────────────────────────────

struct AI001Module : rack::Module {
    // Jack 1 (top) and jack 5 (break) are inputs; jacks 2-4 and 6-8 are outputs.
    enum InputId  { IN_1, IN_5, INPUTS_LEN };
    enum OutputId { OUT_2, OUT_3, OUT_4, OUT_6, OUT_7, OUT_8, OUTPUTS_LEN };

    int panelSkin = 0;  // 0 = black (default), 1 = silver

    AI001Module() {
        config(0, INPUTS_LEN, OUTPUTS_LEN);
        configInput(IN_1, "Port 1 (source)");
        configInput(IN_5, "Port 5 (source / break)");
        configOutput(OUT_2, "Port 2");
        configOutput(OUT_3, "Port 3");
        configOutput(OUT_4, "Port 4");
        configOutput(OUT_6, "Port 6");
        configOutput(OUT_7, "Port 7");
        configOutput(OUT_8, "Port 8");
    }

    void process(const rack::Module::ProcessArgs& /*args*/) override {
        bool split = inputs[IN_5].isConnected();

        // Group 1: IN_1 always feeds OUT_2, OUT_3, OUT_4
        writeGroup(inputs[IN_1], {OUT_2, OUT_3, OUT_4});

        // Group 2: IN_5 feeds OUT_6-8 when split; IN_1 does when merged
        rack::engine::Input& src2 = split ? inputs[IN_5] : inputs[IN_1];
        writeGroup(src2, {OUT_6, OUT_7, OUT_8});
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "panelSkin", json_integer(panelSkin));
        return root;
    }

    void dataFromJson(json_t* root) override {
        json_t* skin = json_object_get(root, "panelSkin");
        if (skin) panelSkin = static_cast<int>(json_integer_value(skin));
    }

private:
    void writeGroup(rack::engine::Input& src, std::initializer_list<int> ids) {
        int ch = src.getChannels();
        for (int id : ids) {
            outputs[id].setChannels(ch > 0 ? ch : 1);
            for (int c = 0; c < ch; c++)
                outputs[id].setVoltage(src.getVoltage(c), c);
            if (ch == 0)
                outputs[id].setVoltage(0.f);
        }
    }
};

// ─── Widget ────────────────────────────────────────────────────────────────────

struct AI001Widget : rack::ModuleWidget {
    // Positions derived from gerber board outline (profile.gbr.svg).
    // Coordinates: SVG y = (128500 - gerber_y) / 1000 mm, x = gerber_x / 1000 mm.
    static constexpr float kX  = 5.105f;
    static constexpr float kSX = 2.0f;
    static constexpr float kY[8] = { 15.997f, 29.826f, 43.655f, 57.484f,
                                      71.313f, 85.142f, 98.971f, 112.800f };

    explicit AI001Widget(AI001Module* module) {
        setModule(module);
        setPanel(rack::createPanel(
            rack::asset::plugin(pluginInstance, "res/AI001-black-v6.svg")));

        // Mounting screws
        addChild(rack::createWidget<rack::ScrewBlack>(
            rack::mm2px(rack::math::Vec(kSX - 1.68f, 3.019f  - 1.68f))));
        addChild(rack::createWidget<rack::ScrewBlack>(
            rack::mm2px(rack::math::Vec(kSX - 1.68f, 125.621f - 1.68f))));

        // Jack 1: input
        addInput(rack::createInputCentered<rack::PJ301MPort>(
            rack::mm2px(rack::math::Vec(kX, kY[0])), module, AI001Module::IN_1));
        // Jacks 2-4: outputs
        addOutput(rack::createOutputCentered<rack::PJ301MPort>(
            rack::mm2px(rack::math::Vec(kX, kY[1])), module, AI001Module::OUT_2));
        addOutput(rack::createOutputCentered<rack::PJ301MPort>(
            rack::mm2px(rack::math::Vec(kX, kY[2])), module, AI001Module::OUT_3));
        addOutput(rack::createOutputCentered<rack::PJ301MPort>(
            rack::mm2px(rack::math::Vec(kX, kY[3])), module, AI001Module::OUT_4));
        // Jack 5: input (break jack)
        addInput(rack::createInputCentered<rack::PJ301MPort>(
            rack::mm2px(rack::math::Vec(kX, kY[4])), module, AI001Module::IN_5));
        // Jacks 6-8: outputs
        addOutput(rack::createOutputCentered<rack::PJ301MPort>(
            rack::mm2px(rack::math::Vec(kX, kY[5])), module, AI001Module::OUT_6));
        addOutput(rack::createOutputCentered<rack::PJ301MPort>(
            rack::mm2px(rack::math::Vec(kX, kY[6])), module, AI001Module::OUT_7));
        addOutput(rack::createOutputCentered<rack::PJ301MPort>(
            rack::mm2px(rack::math::Vec(kX, kY[7])), module, AI001Module::OUT_8));
    }

    void step() override {
        rack::ModuleWidget::step();
        if (!module) return;

        auto* m = dynamic_cast<AI001Module*>(module);
        if (!m) return;

        if (m->panelSkin != lastSkin_) {
            lastSkin_ = m->panelSkin;
            const char* path = (m->panelSkin == 1)
                ? "res/AI001-silver-v6.svg"
                : "res/AI001-black-v6.svg";
            setPanel(rack::createPanel(rack::asset::plugin(pluginInstance, path)));
        }
    }

    void appendContextMenu(rack::Menu* menu) override {
        auto* m = dynamic_cast<AI001Module*>(module);
        if (!m) return;

        menu->addChild(new rack::MenuSeparator);
        menu->addChild(rack::createMenuLabel("Panel skin"));
        menu->addChild(rack::createCheckMenuItem("Black", "",
            [m] { return m->panelSkin == 0; },
            [m] { m->panelSkin = 0; }));
        menu->addChild(rack::createCheckMenuItem("Silver", "",
            [m] { return m->panelSkin == 1; },
            [m] { m->panelSkin = 1; }));
    }

private:
    int lastSkin_ = -1;
};

// ─── Model registration ────────────────────────────────────────────────────────
rack::Model* modelAI001 = rack::createModel<AI001Module, AI001Widget>("AI001_Multiple");
