#pragma once

// DEPRECATED: All SysEx message building now lives in core/Tx802Utils.h.
// This header is kept only for backward compatibility.
// New code should use core::makeRemoteSwitchSysex() etc. directly.

#include "core/Tx802Utils.h"

namespace midi {

class Tx802Messages {
public:
    static juce::MemoryBlock makeRemoteSwitchSysex(juce::uint8 deviceId1to16, juce::uint8 buttonCode, juce::uint8 value = 0x00)
    {
        return core::makeRemoteSwitchSysex(deviceId1to16, buttonCode, value);
    }

    static juce::MemoryBlock makePcedParamChangeSysex(juce::uint8 deviceId1to16, juce::uint8 paramNumber, const juce::Array<juce::uint8>& values)
    {
        return core::makePcedParamChangeSysex(deviceId1to16, paramNumber, values);
    }
};

}
