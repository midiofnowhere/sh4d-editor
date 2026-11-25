# SH-4d Editor

An original MIDI editor for the Roland SH-4d, as a VST3 / Standalone (AU on macOS) plugin built with JUCE.
Knobs in the plugin send control changes to the SH-4d; all parameters are host-automatable and are saved in your DAW project.

Everything here is derived from Roland's public MIDI implementation chart in the SH-4d owner's manual (v2.00).

## What it does

- **4 synth parts**, each with its own full parameter set, MIDI channel and automation lane (`P1 Filter Cutoff`, `P3 LFO Rate`, ...).
- **31 parameters per part**: filter (cutoff, resonance, HPF, drive, KBD, env, ADSR), amp (level, pan, ADSR), LFO (rate, pitch, filter, amp, fade), pitch / timbre / mod, hold / porta / sostenuto switches, sliders 1-4.
- **Follows the hardware**: turn a knob on the SH-4d (with *System Settings > MIDI > Tx CC = ON*) and the editor knob moves.
- **Program select**: bank MSB / LSB + program change per part, with prev / next.
- **Rhythm part** tab for kit selection.
- **XY pad** that can drive any two parameters of the current part.
- **Randomiser** with an amount (small variation around the current sound up to full random) and per-group locks. Level, pan and mod wheel are never randomised.
- **Send All** pushes all four parts to the unit; **Panic** sends All Notes Off / All Sound Off.
- **MIDI monitor** strip at the bottom shows the last message sent and received - handy for checking your channel setup.

## Build

Requires CMake 3.22+ and a C++17 compiler. On Linux you also need the usual JUCE dev packages (ALSA, freetype, X11, GL).

```
cmake -B build -DCMAKE_BUILD_TYPE=Release          # fetches JUCE 8.0.8
cmake --build build --config Release
```

Use an existing JUCE checkout with `-DSH4D_JUCE_PATH=/path/to/JUCE`.
The built VST3 and Standalone end up under `build/SH4dEditor_artefacts/`.

Optional headless tests of the MIDI logic: `-DSH4D_BUILD_TESTS=ON`, then run `build/SH4dTests_artefacts/.../SH4dTests`.

## Setting it up

1. Connect the SH-4d by USB-C.
2. On the SH-4d: **MENU > SYSTEM SETTINGS > MIDI > Tx CC = ON** if you want panel moves to appear in the editor.
3. Standalone: pick the SH-4d as MIDI input and output in the options dialog.
   In a DAW: put the plugin on an instrument track, route the plugin's MIDI output to the SH-4d, and (for follow-the-hardware) route the SH-4d's MIDI input to the plugin.
4. Set each part's **CH** to the channel that part listens on in your SH-4d.
   **The defaults (1, 2, 3, 4, rhythm 10) are guesses.** Check your unit and change them.

## Honest limitations

- **No patch read-back.** The SH-4d does not support System Exclusive, so an editor cannot ask the unit for its current sound. The editor only knows values it has sent or heard (via Tx CC). Use *Send All* deliberately: it overwrites the live sound with whatever the editor shows.
- **Defaults are neutral guesses**, not the unit's actual initial tone.
- **CC reception depends on the unit's MATRIX settings** (the chart says all CCs except 0 and 32 are "recognized depending on settings of MATRIX"). If a knob does nothing, check the MATRIX page. Oscillator model selection has no CC, so it can't be changed from here; Slider 1-4 and Timbre do different things per oscillator model.
- Roland's chart also lists CC84 (Portamento Control); it's deliberately left out because its meaning is note-based rather than a simple value.
- The plugin outputs silence. The SH-4d's sound comes back through its own USB audio / outputs.
- This was written without access to the hardware: the MIDI map and logic are checked against the manual and the headless tests, but the first thing to do on your unit is turn a few knobs and watch the monitor strip.

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
Source/CcTable.h          the CC table (single source of truth)
Source/PluginProcessor.*  parameters, MIDI in/out, state
Source/PluginEditor.*     UI
Tests/ProcessorTests.cpp  headless MIDI-logic tests
```
