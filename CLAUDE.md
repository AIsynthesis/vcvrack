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
