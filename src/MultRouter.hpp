#pragma once
#include <array>

// Pure routing logic for the AI001 passive multiple.
// No VCV Rack dependency — fully unit-testable.
class MultRouter {
public:
    static constexpr int NUM_PORTS    = 8;
    static constexpr int BREAK_IDX    = 4;   // port 5, 0-indexed
    static constexpr int MAX_CHANNELS = 16;

    // Voltage state for one physical jack position.
    // channels == 0 means the jack is not connected.
    struct PortState {
        float voltages[MAX_CHANNELS] = {};
        int channels = 0;
    };

    // What the module should write to one output port.
    struct GroupResult {
        float voltages[MAX_CHANNELS] = {};
        int channels = 1;  // minimum 1 even when silent
    };

    // Computes routing for all 8 output ports given the 8 input states.
    // inputs[BREAK_IDX].channels > 0  →  split mode
    // inputs[BREAK_IDX].channels == 0 →  merged mode
    void route(const PortState inputs[NUM_PORTS], GroupResult outputs[NUM_PORTS]) const {
        if (inputs[BREAK_IDX].channels > 0) {
            routeGroup(inputs, outputs, 0, 4);   // group 1: indices 0..3
            routeGroup(inputs, outputs, 4, 8);   // group 2: indices 4..7
        } else {
            routeMergedGroup(inputs, outputs);
            zeroOutput(outputs[BREAK_IDX]);
        }
    }

private:
    void routeGroup(const PortState* inputs, GroupResult* outputs,
                    int begin, int end) const {
        int src = findSource(inputs, begin, end);
        if (src < 0) {
            for (int i = begin; i < end; i++) zeroOutput(outputs[i]);
            return;
        }
        for (int i = begin; i < end; i++) copySource(inputs[src], outputs[i]);
    }

    void routeMergedGroup(const PortState* inputs, GroupResult* outputs) const {
        int src = -1;
        for (int i = 0; i < NUM_PORTS && src < 0; i++) {
            if (i == BREAK_IDX) continue;
            if (inputs[i].channels > 0) src = i;
        }
        for (int i = 0; i < NUM_PORTS; i++) {
            if (i == BREAK_IDX) continue;
            if (src < 0) zeroOutput(outputs[i]);
            else copySource(inputs[src], outputs[i]);
        }
    }

    int findSource(const PortState* inputs, int begin, int end) const {
        for (int i = begin; i < end; i++)
            if (inputs[i].channels > 0) return i;
        return -1;
    }

    void copySource(const PortState& src, GroupResult& dst) const {
        dst.channels = src.channels;
        for (int c = 0; c < src.channels; c++) dst.voltages[c] = src.voltages[c];
        for (int c = src.channels; c < MAX_CHANNELS; c++) dst.voltages[c] = 0.f;
    }

    void zeroOutput(GroupResult& dst) const {
        dst.channels = 1;
        for (int c = 0; c < MAX_CHANNELS; c++) dst.voltages[c] = 0.f;
    }
};
