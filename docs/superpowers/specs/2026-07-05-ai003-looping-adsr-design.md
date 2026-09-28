# Design: AI003 Looping ADSR — VCV Rack Module

## Goal
Port the AI003 Looping ADSR Eurorack module into the existing AI Synthesis VCV Rack
plugin package (same package as AI001 Multiple). A 4-stage envelope generator with
a switchable free-running/gated Loop mode.

Full feature list and behavior spec: see [2026-07-05-ai003-prd.md](2026-07-05-ai003-prd.md).
(Note: `prd.md` at the repo root is AI001's PRD, not AI003's — this repo now hosts
more than one module, so each module's PRD lives under `docs/superpowers/specs/`.)

## Architecture

- **`src/LoopingAdsr.hpp`** — standalone DSP class, no `rack.hpp` dependency. One
  instance per polyphony channel (up to 16). Unit-testable in isolation via
  `tests/test_looping_adsr.cpp` + `tests/Makefile`, following the same pattern as
  `tests/test_router.cpp`.
- **`src/AI003.cpp`** — `rack::Model` / `rack::Module` / `rack::ModuleWidget`.
  Holds `std::array<LoopingAdsr, 16>`, reads params/inputs each block, writes
  polyphonic Envelope Out and the single Activity LED.
- Registered in `plugin.cpp` / `plugin.hpp` / `plugin.json` alongside
  `AI001_Multiple`, same `addModel()` pattern.
- **`res/AI003-black.svg`** — a small, hand-built panel SVG (background + widget
  holes) derived from the real Fusion 360 export, not the raw export itself.

## State Machine (`LoopingAdsr`)

States: `IDLE → ATTACK → DECAY → SUSTAIN → RELEASE → IDLE`. A `loopMode` flag
changes the transition table; both flag and gate/trigger inputs are read fresh
every `process()` call, so behavior changes take effect within the same sample.

| Mode | Gate/Trigger patched? | Behavior |
|---|---|---|
| Envelope | No | Manual button / Trigger In rising edge → `ATTACK`. After `DECAY`, go straight to `IDLE` — Sustain and Release are skipped entirely regardless of knob position. |
| Envelope | Gate In patched | Gate rising edge → `ATTACK`. After `DECAY` → `SUSTAIN`, held at the Sustain CV while gate is high. Gate falling edge → `RELEASE` → `IDLE`. |
| Loop | No | Free-running: on reaching `IDLE` (end of `DECAY`), immediately re-enter `ATTACK`. Ignores gate/trigger state entirely. |
| Loop | Gate or Trigger patched | Loop only starts a new `ATTACK` while gate/trigger is high. On falling edge, does **not** force a transition — lets the in-flight `DECAY` finish, then goes to `IDLE` instead of re-triggering. |

- **Legato retrigger:** any entry into `ATTACK`, from any prior state, starts the
  exponential ramp from `currentOutput_` — never resets to 0V.
- **Mode switch mid-cycle:** takes effect immediately (no "finish current cycle
  first" grace period). E.g. a `RELEASE` in progress can become a free-running
  `ATTACK` on the very next sample if Loop becomes active with nothing patched.
- **Edge detection:** `rack::dsp::SchmittTrigger` for Manual button and Trigger In
  (rising edge = fire). Gate In uses its own Schmitt trigger to detect rising/
  falling edges that drive transitions, plus a held-level read (`>= 1.f` = high)
  for the Sustain-hold condition.

## Polyphony

- `numChannels = triggerInput.isConnected() ? triggerInput.getChannels()
  : (gateInput.isConnected() ? gateInput.getChannels() : 1)`.
- **Trigger In's channel count wins whenever it is patched**, even if Gate In
  carries a different channel count. Channels beyond an unpatched or
  narrower input's count read as low/disconnected for that input only.
- Envelope Out channel count is set via `outputs[ENV_OUTPUT].setChannels(numChannels)`
  each block.
- One `LoopingAdsr` instance drives each channel independently — no shared state
  across channels.

## Parameter Curves

- Attack / Decay / Release: `time = 0.02f * std::pow(700.f, param)` seconds,
  giving 20ms at `param = 0` → 14s at `param = 1` (log taper).
- Sustain: linear 0–10V.
- Envelope curve shape: exponential only (matching hardware), no user-selectable
  shapes — out of scope per PRD.

## LED

`light = currentOutput_ / 10.f` per channel; for polyphonic patches, average
across active channels for the single physical LED (matches stock VCV convention
for poly lights on a mono indicator).

## Panel Wiring

- Source asset: `C:\Users\abelo\Documents\Fusion 360\Fusion Projects\ai003\ai003 pcb panels\AI003_GERBER\AI003 BLKVCV.svg`
  — a raw, ungrouped 91,671-line Inkscape/Gerber export (28,888 `<path>` elements,
  3,589 nested transform matrices, no semantic layer IDs, no `<circle>` elements
  at all). Confirmed panel size: 40.64mm × 128.5mm (8HP).
- **This raw file is not vector CAD data in any usable sense** — it's a
  potrace/pstoedit-style raster trace: every visible mark is built from
  thousands of clip-path-masked horizontal strips. Regex or manual path-bbox
  math cannot recover hole positions, because the real geometry is defined by
  each shape's clip intersection, not by its raw path bounds (confirmed: naive
  per-path bbox math produced nonsense values up to 484mm on a 128.5mm-tall
  panel).
- **Actual extraction method used** (validated live, not just planned): loaded
  the raw SVG in a real browser (via a throwaway local static file server +
  the preview tooling), rendered it to an off-screen `<canvas>` at 20px/mm,
  and ran connected-component (flood-fill) analysis on fully-transparent
  pixel regions — the panel's mounting/component holes are literal alpha=0
  cutouts in the artwork, not colored circles. This correctly captures every
  nested clip-path and transform because the browser's own SVG rasterizer
  does that compositing, rather than re-implementing it. Found 9 real
  widget-sized holes (+4 corner mounting screws +1 full-canvas background
  blob, both excluded) plus one LED bezel hole distinguished by its smaller
  diameter (~5.4mm vs ~7–7.8mm for knobs/jacks/switch/button).
- Each hole was matched to its widget by rendering cropped, numbered
  screenshots of the panel and reading the actual printed silkscreen labels
  next to each hole, then cross-checked against the official product manual
  (aisynthesis.com AI003 manual, which includes a numbered panel diagram).
  Two initial misreadings were corrected this way: the LED is labeled
  "ENV OUT" because it *displays* Envelope Out level, not because it's a
  jack; and "MANUAL" + "RETRIG" are two independent labels for two separate
  vertically-stacked controls (Manual Trigger button, then Trigger In jack
  below it) — not a single two-line caption as first read.
- **Spec values confirmed against the official manual, PRD kept as
  authoritative where the two differ**: the manual states Decay range
  10ms–20s, Release range 100ms–7s, and Envelope Out range 0–7.5V, which
  differ from the PRD's unified 20ms–14s taper (all three knobs) and 0–10V
  output. Per explicit user decision, this port intentionally deviates from
  the hardware here — use the PRD's values, not the manual's.
- **Verified widget coordinates** (mm, origin top-left, Y down — matches
  Rack's `mm2px()` convention directly, no additional flipping needed):

  | Widget | x (mm) | y (mm) | Hole ⌀ (mm) |
  |---|---|---|---|
  | Activity LED | 9.13 | 19.9 | 5.4 |
  | Attack knob | 29.3 | 19.85 | 7.8 |
  | Mode switch (Loop/Envelope) | 9.07 | 42.88 | 7.78 |
  | Decay knob | 29.32 | 42.9 | 7.78 |
  | Manual Trigger button | 9.1 | 65.9 | 7.35 |
  | Sustain knob | 29.32 | 65.88 | 7.8 |
  | Trigger In jack | 9.1 | 88.9 | 7.0 |
  | Release knob | 29.35 | 88.88 | 7.83 |
  | Gate In jack | 9.1 | 111.9 | 7.05 |
  | Envelope Out jack | 29.35 | 111.9 | 7.05 |

- A small hand-built `res/AI003-black.svg` (background + panel outline only,
  same lightweight style as the existing ~1.3KB `AI001-black-v6.svg`) is used
  as the actual Rack panel graphic; widget positions come from the table
  above via `mm2px()` calls in `AI003.cpp`'s widget constructor, not from
  parsing any SVG at runtime.

## Testing

- **Unit tests** (`tests/test_looping_adsr.cpp`, plain `LoopingAdsr` instance, no
  SDK dependency):
  - Each transition table above (Envelope w/ and w/o gate; Loop w/ and w/o
    gate/trigger).
  - Legato retrigger starting from non-zero current output.
  - Exponential curve timing: Attack/Decay/Release hit expected ~63%/~99%
    progress at the time constants implied by known knob params.
  - Sustain holds at the configured CV while gate remains high.
- **Manual verification in Rack:**
  - Manual button fires an AD-only cycle with nothing patched.
  - Patching Gate In switches to full ADSR behavior.
  - Loop mode free-runs unpatched; gates correctly when patched.
  - Polyphonic cable on Trigger In spawns matching channel count on Envelope Out,
    including the trigger-priority rule when Gate In also carries a poly cable
    with a different count.
  - LED visually tracks output level.
- **Panel sanity check:** load in Rack, visually confirm all 10 widgets line up
  with the real black panel graphic before calling the module done.

## Open Items
None — panel coordinates were resolved during design (see Panel Wiring table
above), including user confirmation on the two ambiguous labels.
