#include "plugin.hpp"

rack::Plugin* pluginInstance = nullptr;

void init(rack::Plugin* p) {
    pluginInstance = p;
    p->addModel(modelAI004);
}
