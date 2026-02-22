#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "midi/MidiSender.h"

namespace midi {

class MidiThru : private juce::MidiInputCallback {
public:
    MidiThru() = default;
    ~MidiThru() override { stop(); }

    juce::StringArray listInputs() const
    {
        juce::StringArray names;
        for (const auto& d : juce::MidiInput::getAvailableDevices()) names.add(d.name);
        return names;
    }

    bool start(const juce::String& inputName, MidiSender* senderToUse)
    {
        stop();
        sender = senderToUse;
        // Open input by name
        for (const auto& d : juce::MidiInput::getAvailableDevices())
        {
            if (d.name == inputName)
            {
                input = juce::MidiInput::openDevice(d.identifier, this);
                if (input)
                {
                    input->start();
                    running = true;
                    return true;
                }
                break;
            }
        }
        return false;
    }

    void stop()
    {
        running = false;
        if (input)
        {
            input->stop();
            input.reset();
        }
        sender = nullptr;
    }

    bool isRunning() const { return running; }

private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override
    {
        // TODO (Bug 4): filter to notes and CC only (skip SysEx and other status bytes)
        if (sender)
            sender->sendMessageNow(message);
    }

    std::unique_ptr<juce::MidiInput> input;
    MidiSender* sender { nullptr };
    bool running { false };
};

}
