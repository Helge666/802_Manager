/**
 * send_preset_bank – Send a DX7 voice bank (.syx VMEM) to a Yamaha TX802.
 *
 * Port of Python cli/tx802/send_preset_bank.py.
 * Uses core (Dx7Utils for validation) + juce_audio_devices for MIDI.
 *
 * Usage:
 *   send_preset_bank --bankfile my_bank.syx --port "USB MIDI"
 *   send_preset_bank --bankfile my_bank.syx --stopafter 8
 */

#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "core/Dx7Utils.h"
#include "core/Tx802Utils.h"

static std::unique_ptr<juce::MidiOutput> openPort(const juce::String& name)
{
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name == name)
            return juce::MidiOutput::openDevice(d.identifier);
    return nullptr;
}

static bool sendPaced(juce::MidiOutput& out, const juce::MemoryBlock& syx,
                       int chunkBytes = 256, int delayMs = 20)
{
    const int total = (int) syx.getSize();
    const auto* data = static_cast<const juce::uint8*>(syx.getData());
    for (int off = 0; off < total; off += chunkBytes)
    {
        int len = juce::jmin(chunkBytes, total - off);
        out.sendMessageNow(juce::MidiMessage(data + off, len));
        if (delayMs > 0) juce::Thread::sleep(delayMs);
    }
    return true;
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String bankFile, portName;
    int deviceId = 1, stopAfter = 0;

    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);
        if (arg == "--bankfile" && i + 1 < argc) bankFile = juce::String(argv[++i]);
        else if (arg == "--port" && i + 1 < argc) portName = juce::String(argv[++i]);
        else if ((arg == "--device-id") && i + 1 < argc) deviceId = juce::jlimit(1, 16, juce::String(argv[++i]).getIntValue());
        else if (arg == "--stopafter" && i + 1 < argc) stopAfter = juce::jlimit(1, 31, juce::String(argv[++i]).getIntValue());
        else if (arg == "--list-ports")
        {
            for (const auto& d : juce::MidiOutput::getAvailableDevices())
                std::cout << "  " << d.name.toStdString() << "\n";
            return 0;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: send_preset_bank --bankfile <file.syx> [options]\n"
                      << "  --port <name>       MIDI output port\n"
                      << "  --device-id <1-16>  Device ID (default: 1)\n"
                      << "  --stopafter <1-31>  Send only first N voices (partial transfer)\n"
                      << "  --list-ports        List MIDI ports\n";
            return 0;
        }
        else { std::cerr << "Unknown option: " << arg.toStdString() << "\n"; return 1; }
    }

    if (bankFile.isEmpty()) { std::cerr << "Error: --bankfile required\n"; return 1; }

    // Read and validate bank
    juce::File f(bankFile);
    if (!f.existsAsFile()) { std::cerr << "Error: File not found: " << bankFile.toStdString() << "\n"; return 1; }

    juce::MemoryBlock data;
    f.loadFileAsData(data);
    std::cout << "Read " << data.getSize() << " bytes from '" << bankFile.toStdString() << "'\n";

    juce::String msg;
    if (!core::Dx7Utils::isValidDx7Bank(data, msg))
    {
        std::cerr << "Invalid bank: " << msg.toStdString() << "\n";
        return 1;
    }
    std::cout << "Bank validation: " << msg.toStdString() << "\n";

    // Handle --stopafter: truncate bank data
    if (stopAfter > 0 && stopAfter < 32)
    {
        std::cout << "Partial transfer: sending first " << stopAfter << " voices\n";
        // Rebuild a shorter bank (not a valid bank, but TX802 accepts partial writes)
        int payloadBytes = stopAfter * 128;
        juce::MemoryBlock partial(6 + payloadBytes + 2);
        auto* out = static_cast<juce::uint8*>(partial.getData());
        std::memcpy(out, data.getData(), 6 + payloadBytes);
        // Recalculate checksum over partial data
        juce::MemoryBlock chkData(static_cast<const juce::uint8*>(data.getData()) + 6, payloadBytes);
        out[6 + payloadBytes] = core::Dx7Utils::calculateChecksum(chkData);
        out[6 + payloadBytes + 1] = 0xF7;
        data = partial;
    }

    // Auto-select port
    if (portName.isEmpty())
    {
        auto devices = juce::MidiOutput::getAvailableDevices();
        if (devices.isEmpty()) { std::cerr << "No MIDI ports found\n"; return 1; }
        portName = devices[0].name;
        std::cout << "Auto-selected port: " << portName.toStdString() << "\n";
    }

    auto output = openPort(portName);
    if (!output) { std::cerr << "Could not open port: " << portName.toStdString() << "\n"; return 1; }
    std::cout << "Opened port: " << portName.toStdString() << "\n";

    // Send PRTCT_OFF
    std::cout << "Sending PRTCT_OFF...\n";
    auto macroSeq = core::expandMacro("PRTCT_OFF");
    for (const auto& bp : macroSeq)
    {
        for (int r = 0; r < bp.repeat; ++r)
        {
            int code = core::getButtonCode(bp.name);
            if (code >= 0)
            {
                auto syx = core::makeRemoteSwitchSysex((juce::uint8)deviceId, (juce::uint8)code);
                output->sendMessageNow(juce::MidiMessage(syx.getData(), (int)syx.getSize()));
                juce::Thread::sleep(100);
            }
        }
    }
    juce::Thread::sleep(120);

    // Send bank with pacing
    std::cout << "Sending bank (" << data.getSize() << " bytes, paced)...\n";
    sendPaced(*output, data);

    juce::Thread::sleep(150);

    // Send VOICE_SELECT to clear "Data received"
    {
        int code = core::getButtonCode("VOICE_SELECT");
        auto syx = core::makeRemoteSwitchSysex((juce::uint8)deviceId, (juce::uint8)code);
        output->sendMessageNow(juce::MidiMessage(syx.getData(), (int)syx.getSize()));
    }

    std::cout << "Bank sent successfully.\n";
    return 0;
}
