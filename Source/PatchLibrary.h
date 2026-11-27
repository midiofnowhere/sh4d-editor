#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include "CcTable.h"

// File-based patch library. A patch is the 31 CC values of one part (stored by CC
// number so files stay valid if the table ever changes) plus the bank/program the
// patch was built on, a category and a name. Files are plain XML (.sh4dpatch) in
// Documents/SH4dEditor/Patches, so they can be backed up and shared.

struct PatchInfo
{
    juce::File   file;
    juce::String name;
    juce::String category;
    int bankMsb = sh4d::kToneBankMsb;
    int bankLsb = sh4d::kToneBankLsb;
    int program = 0;
};

class PatchLibrary
{
public:
    using Values = std::array<int, (size_t) sh4d::kNumCc>;

    static juce::File getFolder();                         // created on demand
    static void setFolderOverride (const juce::File&);     // for tests

    static juce::Array<PatchInfo> scan();                  // sorted by category then name

    static bool save (const juce::String& name, const juce::String& category, const Values& values,
                      int bankMsb, int bankLsb, int program, juce::File* writtenTo = nullptr);

    // 'values' is filled in for every CC the file contains; others are left untouched.
    static bool load (const juce::File&, Values& values, PatchInfo& info);

    static bool remove (const juce::File&);
};
