# PRD: AI004 OTA VCF — VCV Rack Module

## Goal
A VCV Rack v2 plugin that faithfully recreates the AI004 OTA Voltage Controlled Filter — an MS-20-style analog filter — as a virtual Eurorack module. The user should be able to patch it into any VCV Rack patch and get the same distinctive character, overdrive, and self-oscillation behavior of the physical hardware.

## Features

### Signal Path
- Audio input jack: accepts any VCV Rack signal (behavior changes with input level, matching hardware — no normalization)
- Audio output jack: single output, carries LP or HP signal depending on mode switch
- Designed around 10Vpp input signal level as reference (like the hardware); quieter oscillators will sound different — this is intentional

### Controls
- **Cutoff knob:** frequency range 20Hz–20kHz, default 1kHz, logarithmic taper
- **Resonance knob:** 0–100%, default 0%; above ~80% the filter self-oscillates; full range allows extreme overdrive
- **LP/HP toggle switch:** selects Lowpass (12dB/octave slope) or Highpass (6dB/octave slope), default LP
- **CV Amount knob (attenuverter):** -1.0 to +1.0, default 0.0; scales and inverts CV input before it modulates cutoff
- **Internal resonance gain trim:** fixed value baked into DSP, not exposed on panel; matches hardware trim pot behavior

### CV / Modulation
- **CV input jack:** 1V/octave compatible; modulates cutoff frequency; amount and polarity set by CV Amount knob

### Panel
- 8HP wide, matching hardware Eurorack footprint
- Two panel variants selectable by right-clicking the module in VCV Rack:
  - **Aluminum** — light silver/grey background, red knobs, white labels (default)
  - **Black** — black background, black knobs, white labels
- Knob layout mirrors physical AI004 panel: Cutoff top-left, Resonance top-right, CV Amount bottom-left, LP/HP switch bottom-center
- Jacks at bottom: Audio In (left), CV In (center), Audio Out (right)
- Module name "AI004" and "OTA VCF" subtitle displayed on panel

### DSP Behavior
- OTA-based filter topology matching the Korg MS-20 / AI004 circuit structure
- Resonance feedback path includes diode gain stage (key to MS-20 character)
- Volume of passed signal drops as resonance increases — this is authentic, not a bug
- Self-oscillation begins naturally as resonance approaches maximum
- Soft overdrive/saturation on input and feedback path — no hard clipping imposed

## Behaviour
- Module processes audio at VCV Rack's sample rate (typically 44.1kHz or 48kHz)
- No polyphonic cable support — mono in, mono out only
- Right-click context menu exposes panel skin selector (Aluminum / Black)
- Selected panel skin persists when the patch is saved and reloaded
- Module slug: `AI004_OTA_VCF`; plugin slug: `AISynthesis`

## Out of Scope (for now)
- Preset system / saved knob states
- Polyphonic cable support
- Oversampling
- Tempo sync of any kind
- Custom UI animations or VU meters
- Sidechain or second audio input
