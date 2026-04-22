# RACK_DIR must point to the VCV Rack SDK directory.
# Override on the command line: make RACK_DIR=/path/to/Rack
RACK_DIR ?= ../Rack

SLUG = AISynthesis
VERSION = 1.0.0

FLAGS += -std=c++17
SOURCES += src/plugin.cpp src/AI004Module.cpp
DISTRIBUTABLES += res plugin.json

include $(RACK_DIR)/plugin.mk
