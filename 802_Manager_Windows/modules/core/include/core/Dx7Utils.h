#pragma once

#include <juce_core/juce_core.h>
#include <cstring>

namespace core {

/**
 * Utilities for converting between Yamaha DX7 single-voice (155 bytes VCED)
 * and packed bank (128 bytes) formats, validating SysEx, and extracting names.
 */
class Dx7Utils {
public:
    // --- Basic helpers ---
    static juce::uint8 calculateChecksum(const juce::MemoryBlock& data7bit);

    // --- Single voice SysEx (163 bytes) validation ---
    // On success returns true, fills message with "Valid ..." and voiceData155 with 155-byte core data.
    static bool verifySingleVoiceSysex(const juce::MemoryBlock& sysex163,
                                       juce::String& message,
                                       juce::MemoryBlock& voiceData155);

    // --- 32-voice bank validation (4104 bytes) ---
    static bool isValidDx7Bank(const juce::MemoryBlock& bankData,
                               juce::String& message);

    // --- Name helpers ---
    static juce::String extractPresetNameFromSysex(const juce::MemoryBlock& sysex163);
    static juce::String extractPresetNameFromUnpacked(const juce::MemoryBlock& voiceData155);

    // --- Conversions ---
    static juce::MemoryBlock packSingleToBankVoice(const juce::MemoryBlock& voiceData155);
    static juce::MemoryBlock unpackBankVoiceToSingle(const juce::MemoryBlock& packed128);
    static juce::MemoryBlock createSinglePresetSysex(const juce::MemoryBlock& packed128);
};

}
