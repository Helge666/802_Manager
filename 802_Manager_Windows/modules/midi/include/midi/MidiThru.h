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

    void setAtToBreath(bool e) { atToBreath = e; }

private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override
    {
        if (! sender) return;

        // Forward Notes, CC, and Pitch Bend (Bug 4: drop SysEx and other status bytes).
        // Aftertouch (Channel + Poly) is converted to Breath Controller (CC2)
        // when atToBreath is enabled, otherwise dropped.
        if (message.isNoteOnOrOff() || message.isController() || message.isPitchWheel())
        {
            sender->sendMessageNow(message);
        }
        else if (atToBreath)
        {
            if (message.isChannelPressure())
            {
                sender->sendMessageNow(
                    juce::MidiMessage::controllerEvent(
                        message.getChannel(), 2, message.getChannelPressureValue()));
            }
            else if (message.isAftertouch())
            {
                sender->sendMessageNow(
                    juce::MidiMessage::controllerEvent(
                        message.getChannel(), 2, message.getAfterTouchValue()));
            }
        }
        // Everything else (SysEx, Program Change, etc.) is dropped.
    }

    std::unique_ptr<juce::MidiInput> input;
    MidiSender* sender { nullptr };
    bool running { false };
    bool atToBreath { false };
};

}
