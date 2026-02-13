#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "core/Tx802Utils.h"   // SysEx message building now lives in core

namespace midi {

class MidiSender {
public:
    MidiSender() = default;

    static juce::StringArray listOutputs()
    {
        juce::StringArray names;
        auto devices = juce::MidiOutput::getAvailableDevices();
        for (const auto& d : devices) names.add(d.name);
        return names;
    }

    bool openByName(const juce::String& name)
    {
        close();
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
        {
            if (d.name == name)
            {
                output = juce::MidiOutput::openDevice(d.identifier);
                return output != nullptr;
            }
        }
        return false;
    }

    void close()
    {
        if (output)
        {
            output.reset();
        }
    }

    bool isOpen() const { return output != nullptr; }

    bool sendSysex(const juce::MemoryBlock& syx)
    {
        if (! output) return false;
        juce::MidiMessage m((const void*) syx.getData(), (int) syx.getSize());
        output->sendMessageNow(m);
        return true;
    }

    // Paced send allowing runtime-configurable chunking/delay
    bool sendSysexPaced(const juce::MemoryBlock& syx, int chunkBytes, int interChunkMs)
    {
        if (! output) return false;
        const int total = (int) syx.getSize();
        const juce::uint8* data = static_cast<const juce::uint8*>(syx.getData());
        const int maxChunk = juce::jlimit(16, 1024, chunkBytes);
        const int delayMs = juce::jlimit(0, 200, interChunkMs);
        for (int offset = 0; offset < total; offset += maxChunk)
        {
            const int len = juce::jmin(maxChunk, total - offset);
            juce::MidiMessage m((const void*)(data + offset), len);
            output->sendMessageNow(m);
            if (delayMs > 0) juce::Thread::sleep(delayMs);
        }
        return true;
    }

    bool sendNoteOn(int channel0to15, int note0to127, int velocity0to127)
    {
        if (! output) return false;
        output->sendMessageNow(juce::MidiMessage::noteOn(channel0to15, note0to127, (juce::uint8) juce::jlimit(0, 127, velocity0to127)));
        return true;
    }

    bool sendNoteOff(int channel0to15, int note0to127)
    {
        if (! output) return false;
        output->sendMessageNow(juce::MidiMessage::noteOff(channel0to15, note0to127));
        return true;
    }

    void sendAllNotesOffAllChannels()
    {
        if (! output) return;
        for (int ch = 0; ch < 16; ++ch)
            output->sendMessageNow(juce::MidiMessage::allNotesOff(ch));
    }

    // Convenience wrappers – now use core:: directly
    bool sendRemoteSwitch(juce::uint8 deviceId1to16, juce::uint8 buttonCode, juce::uint8 value = 0x00)
    {
        return sendSysex(core::makeRemoteSwitchSysex(deviceId1to16, buttonCode, value));
    }

    bool sendPcedParamChange(juce::uint8 deviceId1to16, juce::uint8 paramNumber, const juce::Array<juce::uint8>& values)
    {
        return sendSysex(core::makePcedParamChangeSysex(deviceId1to16, paramNumber, values));
    }

    bool sendMessageNow(const juce::MidiMessage& m)
    {
        if (! output) return false;
        output->sendMessageNow(m);
        return true;
    }

private:
    std::unique_ptr<juce::MidiOutput> output;
};

}
