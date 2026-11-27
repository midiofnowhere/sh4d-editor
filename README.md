# SH-4d Editor  (v0.4)

An original editor for the Roland SH-4d as a VST3 / Standalone (AU on macOS) plugin built with JUCE.
Everything is derived from Roland's public MIDI implementation chart in the SH-4d owner's manual (v2.00).

The SH-4d has **no System Exclusive support**, so an editor can only use control changes, bank select and
program change. That shapes everything below: the editor can *send* any documented parameter and *listen*
to the unit's panel (with Tx CC on), but it cannot ask the unit what its current sound is.

## The PANEL tab: a recreation of the hardware

The default view is laid out from a top-down photograph of the SH-4d: the OSC section with its four faders and coloured
LED buttons, the FILTER / AMP / LFO block, VOLUME and EFFECTS, the small violet LCD with the cursor ring, the two
encoders, the 16 round step buttons with their printed SHIFT functions, and the two-row button keyboard (16 white and
10 black buttons, with OCTAVE / PITCH / ARPEGGIO / HOLD / D-MOTION).

- **Everything the SH-4d exposes over MIDI works**: HPF, cutoff, resonance, KYBD, drive, the filter and amp envelopes,
  PAN, LEVEL, LFO rate / fade / pitch / filter / amp, PITCH, TIMBRE and the four OSC faders. Drag a knob (Shift =
  fine), double-click to reset, mouse wheel, right-click for MIDI learn / modulation.
- The **LCD** shows the default oscillator screen when idle, and the name, value and bar of whatever you touch.
- **Buttons that work**: the keyboard (plays the selected part; OCTAVE and PITCH buttons), HOLD (CC 64; with SHIFT it
  becomes portamento, CC 65), SOUND, and **SHIFT + step button** as printed under them: 1-5 select parts, 6/8 jump
  to SOUND, 7 to MOD, 10 INIT, 12 UNDO, 13/14 to LIBRARY, 16 MENU to SETUP, PAGE/TIE = WRITE to LIBRARY.
- **D-MOTION** lights up when the unit's tilt (aftertouch / pitch bend) arrives.
- Controls with **no MIDI CC on the SH-4d** (MODEL, filter and LFO TYPE, the [1] [2] encoders, VOLUME, the effects knobs
  and buttons, the OSC buttons, the cursor buttons, the sequencer) are drawn faithfully, with their printed labels
  dimmed, and touching one tells you on the LCD that it's unit-only.
- No manufacturer logo artwork is used, only the product name as text.

## What's in v0.2

### Talks to the unit directly
- **Hardware MIDI in / out pickers** with SH-4d auto-detect and hot-plug rescan. The plugin opens the SH-4d's
  USB MIDI ports itself, so it works the same in Ableton, FL, Reaper, Logic or Standalone with **no MIDI routing
  in the DAW**. If no hardware out is open it falls back to the DAW's MIDI output (Auto / Always / Off).
- Runs on its **own engine thread**, so Send All, modulation and controller input keep working even when the
  host isn't processing audio.
- **Echo guard**: the unit repeating back what you just sent can't fight your knob moves.
- Optional **controller input** (any USB keyboard / knob box): its notes play the selected part.

### Sound editing
- 4 synth parts x 31 parameters, host-automatable and saved with the project; rhythm part tab with bank/program select.
- **Draggable envelope graphs** for the filter and amp ADSR.
- On-screen **keyboard** for auditioning on the selected part, Panic button.
- Randomiser with amount and group locks (level / pan / mod wheel are never randomised).
- **Undo / redo** (buttons, or Ctrl/Cmd+Z).
- Resizable window.

### Modulation matrix (adds things the SH-4d can't do on its own)
- **8 macros**, **4 LFOs** (sine, triangle, saws, square, sample & hold, smooth random; free Hz or tempo-synced
  to the host from 4 bars to 1/16 triplets).
- Sources also include **aftertouch and pitch bend** - what the SH-4d's D-Motion tilt sensor transmits.
- Up to 32 routings, each: source -> part -> any knob, bipolar depth. Knobs keep their own value and show a
  teal ring where the modulated value currently is. **Right-click any knob > Modulate with** for a quick route.
- **XY pad** driving any two parameters.

### Library, scenes, tools
- **Patch library**: save the current part as a named, categorised file (plain XML in
  `Documents/SH4dEditor/Patches`), search, load (optionally sending the bank/program first), delete.
- **8 scenes** for all four parts, plus an **automatable Morph** between any two scenes.
- **A/B compare** and **copy part to part**.
- **MIDI learn** on every knob, switch, macro, LFO rate and the morph control (right-click > MIDI learn).

## Setup
1. Connect the SH-4d by USB.
2. On the unit: **MENU > SYSTEM SETTINGS > MIDI > Tx CC = ON** if you want its panel moves to show up in the editor
   (and its D-Motion tilt as modulation sources).
3. Open the plugin, go to **SETUP**, press **Auto-detect SH-4d** (or pick the ports by hand).
4. Set each part's **MIDI channel** on the SETUP page to match your unit. **The defaults (1-4, rhythm 10) are guesses.**
5. In Standalone, leave the SH-4d unticked in JUCE's own audio/MIDI options dialog - the plugin handles the device itself.

## Honest limitations
- **No patch read-back.** The editor only knows values it sent or heard. *Send All* overwrites the live sound with what
  the editor shows. Starting values are neutral guesses, not the unit's init sound.
- **CC reception depends on the unit's MATRIX settings.** Roland's chart says all CCs except 0 and 32 are recognized
  "depending on MATRIX". If a knob does nothing, check the MATRIX page. Oscillator model selection has no CC.
- Modulation is sent as 7-bit CCs (about 8000 messages/second at most), so very fast LFOs on many parameters will be stepped.
- Parameters under modulation are not "followed" from the hardware (it would fight the modulation).
- CC84 (portamento control) is deliberately left out.
- The plugin outputs silence; the SH-4d's sound comes back through its own USB audio / outputs.
- **Written and tested without the hardware**: the MIDI map and logic are checked against the manual and an automated
  test suite (engine, echo guard, modulation, learn, scenes, patches, state), but not against a real SH-4d.
  First thing to do: turn a few knobs and watch the monitor strip at the bottom.

## Build
Requires CMake 3.22+ and a C++17 compiler (Linux also needs the usual JUCE dev packages: ALSA, freetype, X11, GL).

```
cmake -B build -DCMAKE_BUILD_TYPE=Release          # fetches JUCE 8.0.8
cmake --build build --config Release
```

Use an existing JUCE checkout with `-DSH4D_JUCE_PATH=/path/to/JUCE`. Output: `build/SH4dEditor_artefacts/Release/`.
Tests: add `-DSH4D_BUILD_TESTS=ON` and run `build/SH4dTests_artefacts/Release/SH4dTests`.
The included GitHub Actions workflow builds Windows, macOS (universal) and Linux and uploads the plugins as artifacts.

## MIDI map (tone parts)

| CC | Parameter | CC | Parameter | CC | Parameter |
|---|---|---|---|---|---|
| 1 | Mod wheel | 28 | Filter sustain | 78 | Filter KBD |
| 7 | Amp level | 29 | Filter release | 79 | Filter HPF |
| 10 | Amp pan | 31 | Amp sustain | 80 | LFO amp |
| 16 | LFO rate | 64 | Hold | 81 | Filter env |
| 18 | LFO pitch | 65 | Porta switch | 82 | Filter attack |
| 19 | LFO filter | 66 | Sostenuto | 83 | Filter decay |
| 20 | LFO fade | 71 | Filter resonance | 85-88 | Slider 1-4 |
| 21 | Pitch | 72 | Amp release | 90 | Filter drive |
| 77 | Timbre | 73 | Amp attack | 74 | Filter cutoff |
| 75 | Amp decay | | | | |

Program change: bank MSB 87-88 / LSB 64-77 for tones, MSB 86 / LSB 0 or 64 for rhythm kits.

## Layout
```
Source/CcTable.h          CC table, modulation sources, shared types
Source/PluginProcessor.*  parameters, engine thread, MIDI devices, modulation, scenes, learn, state
Source/PatchLibrary.*     file-based patch library
Source/PluginEditor.*     UI shell + Sound / Mod / Library / Setup pages
Source/PanelPage.cpp      the hardware-style PANEL page and its widgets
Tests/ProcessorTests.cpp  headless tests of the engine
```
