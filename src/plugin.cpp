#include "plugin.hpp"

rack::Plugin* pluginInstance = nullptr;

void init(rack::Plugin* p) {
    pluginInstance = p;
    p->addModel(modelAI001);
    p->addModel(modelAI003);
    p->addModel(modelAI250);
    p->addModel(modelAI018);
}
