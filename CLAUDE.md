# AI004 OTA VCF — VCV Rack Plugin

## What This Is
A VCV Rack v2 C++ plugin recreating the AI004 OTA Voltage Controlled Filter (MS-20-style). One module, mono signal path, two panel skins. Developer has no prior audio software experience — prioritize clear, well-commented code over cleverness.

## Tech Stack
- Language: C++17
- SDK: VCV Rack v2 SDK (https://github.com/VCVRack/Rack)
- Build system: CMake via VCV Rack's standard plugin Makefile
- Target platforms: Windows, macOS, Linux
- No external DSP libraries — implement OTA filter from scratch using the SDK primitives

## Architecture
- One plugin, one module: `AI004_OTA_VCF`
- DSP logic lives in a dedicated `OtaVcf` C++ class (separate from the Rack module struct)
- Panel SVG files: `res/panel-aluminum.svg` and `res/panel-black.svg`
- Skin selection stored in module JSON via `dataToJson` / `dataFromJson`

## Code Style
- Snake_case for variables and functions; PascalCase for classes and structs
- Comment every non-obvious DSP decision with the reasoning (e.g. why a particular tanh approximation)
- Keep `process()` method under 30 lines — extract DSP steps into named functions
- No raw `new`/`delete` — use RAII and stack allocation where possible

## Coding Principles
- **Single responsibility.** Every function does one thing. If you need an "and" to describe it, split it.
- **Small functions.** If a function exceeds ~30 lines, extract sub-operations into named functions.
- **Descriptive names over comments.** `applyDiodeSaturation()` needs no comment. `calc()` needs a refactor.
- **Comment why, not what.** Explain non-obvious DSP reasoning, not what the line of code does.
- **Fail early.** Clamp all parameter inputs at the boundary of `process()` before they enter the filter.

## What NOT To Do
- Do not add polyphonic processing
- Do not add oversampling
- Do not normalize input gain — hardware character depends on input level
- Do not add a preset system
- Do not use third-party DSP libraries (keep it self-contained)


---

# Modules in this plugin (v2.1.0)
- AI001_Multiple (published in the VCV Library as v2.0.1)
- AI003_LoopingADSR — `src/AI003.cpp`, `src/LoopingAdsr.hpp`, tests in `tests/test_looping_adsr.cpp`
- AI250_BXR — see below
- AI018_StereoMatrixMixer — see below
- AI004 source files remain in `src/` but are NOT built (removed from SOURCES).

# AI250 BXR — VCV Rack port of the Collision BXR firmware

## Source of truth
- Hardware firmware: `C:\Users\abelo\Documents\Claude\collision-patch-init\BXR\` (main.cpp, controls.h)
- `src/bxr/*.h` are verbatim copies of `collision-patch-init/common/` — if the firmware DSP changes, re-copy them; do not fork the maths here.

## Files
- `src/BxrEngine.hpp` — platform-independent engine (no Rack includes). Mirrors `buildControlState()` and `AudioCallback()`; unit-tested by `tests/test_bxr_engine.cpp`.
- `src/AI250.cpp` — Rack module + widget only; reads params/jacks into `bxr::PanelState`.
- `src/AI250Layout.hpp` + `res/AI250-black.svg` — both GENERATED from the real panel Gerbers in `res-src/AI250-gerbers/` by `tools/gerber_to_panel.py AI250` (`pip install gerbonara`). Never hand-place widgets: every knob/jack/switch/LED is snapped to its Edge_Cuts hole. Reuse this script for other modules' panels.
- Module slug: `AI250_BXR`, 14HP, black panel (black mask, white silk, gold exposed copper).
- Switch polarity matches the hardware silkscreen: IN 2 up / OSC down, AUDIO RATE up / LFO down.

## Porting conventions
- ±5 V ↔ ±1.0 inside the DSP (Patch SM scaling). Outputs ×5 V. Gates 0/5 V like hardware.
- CV adds `volts / 5` to the knob's 0–1 baseline, no attenuators (same as hardware).
- ADC dead zones are intentionally dropped (VCV has no ADC noise; they would break small V/Oct offsets).
- Geiger gates tick every 1 ms of real time with the firmware's 48-sample block, so pulse width and rate are sample-rate independent.

# AI018 Stereo Matrix Mixer
- `src/MatrixMixer.hpp` (pure mixing core, tested by `tests/test_matrix_mixer.cpp`) + `src/AI018.cpp` (Rack module).
- Behaviour from the AI018 build guide: 4 stereo ins, R normalled to L; 16 pots (row = input, column = output A–D); DC-coupled audio/CV.
- Assumptions (not in the guide — confirm against hardware): pots are linear 0 → unity gain; outputs clip at ±11 V (±12 V op-amp rails).
- Polyphonic: every channel mixed independently. Panel/layout generated with `python3 tools/gerber_to_panel.py AI018` (EAGLE Gerbers in `res-src/AI018-gerbers/`). 18HP.


## Plugin slug — IMPORTANT
- This repo is the VCV Library source: `plugin.json` slug stays `AISynthesis`, version must be above the last Library release.
- For LOCAL testing, build with slug `AISynthesisDev` (name "AI Synthesis (Dev)"). If a local build uses `AISynthesis`, Rack
  replaces it with the Library version and hides any module not on the user's Library subscription — new modules never appear.
- Install a dev build: copy `dist/AISynthesisDev-<ver>-win-x64.vcvplugin` into `%LOCALAPPDATA%\Rack2\plugins-win-x64\` and restart Rack.

## Panels
- All new panels are generated from the real panel Gerbers: `python3 tools/gerber_to_panel.py <MODULE>` (`pip install gerbonara`),
  Gerbers in `res-src/<MODULE>-gerbers/`. It writes `res/<MODULE>-black.svg` and `src/<MODULE>Layout.hpp` and snaps every widget to its hole.
