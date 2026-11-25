#pragma once

// Control-change map for the Roland SH-4d, taken from the official
// "MIDI implementation chart (Tone)" in the SH-4d owner's manual (v2.00).
//
// The SH-4d has NO System Exclusive support, so everything an editor can do
// goes through CC, bank select and program change.

namespace sh4d
{
    constexpr int kNumSynthParts = 4;   // synth parts 1-4
    constexpr int kNumParts      = 5;   // index 4 = rhythm part (program select only, no CCs)

    enum class Group { Amp = 0, Filter, Lfo, Pitch, Slider };
    enum class Kind  { Knob, Switch };

    struct CcDef
    {
        int         cc;
        const char* id;          // used inside parameter IDs: "p1_" + id
        const char* name;        // full name (shown in the DAW's automation list)
        const char* shortName;   // label under the knob
        Group       group;
        Kind        kind;
        int         def;         // neutral starting value (0-127, switches 0/1)
    };

    inline constexpr CcDef kCc[] =
    {
        // ---- AMP
        {  7, "amp_level",   "Amp Level",       "Level",   Group::Amp,    Kind::Knob,   100 },
        { 10, "amp_pan",     "Amp Pan",         "Pan",     Group::Amp,    Kind::Knob,    64 },
        { 73, "amp_attack",  "Amp Attack",      "Attack",  Group::Amp,    Kind::Knob,     0 },
        { 75, "amp_decay",   "Amp Decay",       "Decay",   Group::Amp,    Kind::Knob,    64 },
        { 31, "amp_sustain", "Amp Sustain",     "Sustain", Group::Amp,    Kind::Knob,   100 },
        { 72, "amp_release", "Amp Release",     "Release", Group::Amp,    Kind::Knob,    20 },

        // ---- FILTER
        { 74, "flt_cutoff",  "Filter Cutoff",   "Cutoff",  Group::Filter, Kind::Knob,   127 },
        { 71, "flt_res",     "Filter Resonance","Reso",    Group::Filter, Kind::Knob,     0 },
        { 79, "flt_hpf",     "Filter HPF",      "HPF",     Group::Filter, Kind::Knob,     0 },
        { 90, "flt_drive",   "Filter Drive",    "Drive",   Group::Filter, Kind::Knob,     0 },
        { 78, "flt_kbd",     "Filter KBD",      "KBD",     Group::Filter, Kind::Knob,    64 },
        { 81, "flt_env",     "Filter Env",      "Env",     Group::Filter, Kind::Knob,    64 },
        { 82, "flt_attack",  "Filter Attack",   "Attack",  Group::Filter, Kind::Knob,     0 },
        { 83, "flt_decay",   "Filter Decay",    "Decay",   Group::Filter, Kind::Knob,    64 },
        { 28, "flt_sustain", "Filter Sustain",  "Sustain", Group::Filter, Kind::Knob,    64 },
        { 29, "flt_release", "Filter Release",  "Release", Group::Filter, Kind::Knob,    20 },

        // ---- LFO
        { 16, "lfo_rate",    "LFO Rate",        "Rate",    Group::Lfo,    Kind::Knob,    64 },
        { 18, "lfo_pitch",   "LFO Pitch",       "Pitch",   Group::Lfo,    Kind::Knob,     0 },
        { 19, "lfo_filter",  "LFO Filter",      "Filter",  Group::Lfo,    Kind::Knob,     0 },
        { 80, "lfo_amp",     "LFO Amp",         "Amp",     Group::Lfo,    Kind::Knob,     0 },
        { 20, "lfo_fade",    "LFO Fade",        "Fade",    Group::Lfo,    Kind::Knob,     0 },

        // ---- PITCH / PERFORMANCE
        { 21, "pitch",       "Pitch",           "Pitch",   Group::Pitch,  Kind::Knob,    64 },
        { 77, "timbre",      "Timbre",          "Timbre",  Group::Pitch,  Kind::Knob,    64 },
        {  1, "modwheel",    "Mod Wheel",       "Mod",     Group::Pitch,  Kind::Knob,     0 },
        { 64, "hold",        "Hold",            "Hold",    Group::Pitch,  Kind::Switch,   0 },
        { 65, "porta",       "Porta Switch",    "Porta",   Group::Pitch,  Kind::Switch,   0 },
        { 66, "sostenuto",   "Sostenuto",       "Sost",    Group::Pitch,  Kind::Switch,   0 },

        // ---- SLIDERS (what they do depends on the oscillator model)
        { 85, "slider1",     "Slider 1",        "Slider 1",Group::Slider, Kind::Knob,    64 },
        { 86, "slider2",     "Slider 2",        "Slider 2",Group::Slider, Kind::Knob,    64 },
        { 87, "slider3",     "Slider 3",        "Slider 3",Group::Slider, Kind::Knob,    64 },
        { 88, "slider4",     "Slider 4",        "Slider 4",Group::Slider, Kind::Knob,    64 },
    };

    constexpr int kNumCc = (int) (sizeof (kCc) / sizeof (kCc[0]));

    // Parameter IDs of the five randomiser lock switches, indexed by Group.
    inline constexpr const char* kLockIds[5] = { "lockAmp", "lockFilter", "lockLfo", "lockPitch", "lockSliders" };

    // Program change on the SH-4d is addressed as bank MSB / bank LSB / PC.
    // Tone parts: MSB 87-88, LSB 64-77.  Rhythm part: MSB 86, LSB 0 or 64.
    constexpr int kToneBankMsb   = 87;
    constexpr int kToneBankLsb   = 64;
    constexpr int kRhythmBankMsb = 86;
    constexpr int kRhythmBankLsb = 0;
}
