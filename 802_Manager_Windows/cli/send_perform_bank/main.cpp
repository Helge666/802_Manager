/**
 * send_perform_bank – Send a Performance Bank (PMEM) .syx file to a Yamaha TX802.
 *
 * Port of Python cli/tx802/send_perform_bank.py.
 *
 * Usage:
 *   send_perform_bank --bankfile perform.syx --port "USB MIDI"
 */

#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "core/Tx802Utils.h"

static std::unique_ptr<juce::MidiOutput> openPort(const juce::String& name)
{
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name == name) return juce::MidiOutput::openDevice(d.identifier);
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

static void sendButton(juce::MidiOutput& out, const juce::String& name, int deviceId)
{
    int code = core::getButtonCode(name);
    if (code >= 0)
    {
        auto syx = core::makeRemoteSwitchSysex((juce::uint8)deviceId, (juce::uint8)code);
        out.sendMessageNow(juce::MidiMessage(syx.getData(), (int)syx.getSize()));
        juce::Thread::sleep(100);
    }
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String bankFile, portName;
    int deviceId = 1;

    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);
        if (arg == "--bankfile" && i + 1 < argc) bankFile = juce::String(argv[++i]);
        else if (arg == "--port" && i + 1 < argc) portName = juce::String(argv[++i]);
        else if (arg == "--device-id" && i + 1 < argc) deviceId = juce::jlimit(1, 16, juce::String(argv[++i]).getIntValue());
        else if (arg == "--list-ports")
        {
            for (const auto& d : juce::MidiOutput::getAvailableDevices())
                std::cout << "  " << d.name.toStdString() << "\n";
            return 0;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: send_perform_bank --bankfile <file.syx> [options]\n"
                      << "  --port <name>       MIDI output port\n"
                      << "  --device-id <1-16>  Device ID (default: 1)\n"
                      << "  --list-ports        List MIDI ports\n";
            return 0;
        }
        else { std::cerr << "Unknown option: " << arg.toStdString() << "\n"; return 1; }
    }

    if (bankFile.isEmpty()) { std::cerr << "Error: --bankfile required\n"; return 1; }

    juce::File f(bankFile);
    if (!f.existsAsFile()) { std::cerr << "File not found: " << bankFile.toStdString() << "\n"; return 1; }

    juce::MemoryBlock data;
    f.loadFileAsData(data);
    std::cout << "Read " << data.getSize() << " bytes from '" << bankFile.toStdString() << "'\n";

    // Basic PMEM validation
    const auto* bytes = static_cast<const juce::uint8*>(data.getData());
    if (data.getSize() < 10 || bytes[0] != 0xF0 || bytes[1] != 0x43 || bytes[data.getSize()-1] != 0xF7)
    {
        std::cerr << "Invalid SysEx file (bad F0/F7 framing or too short)\n";
        return 1;
    }
    // Check for PMEM header bytes after device ID byte
    if (!(bytes[3] == 0x7E && bytes[4] == 0x01 && bytes[5] == 0x28))
    {
        std::cerr << "Warning: File does not appear to be a PMEM bank (unexpected header bytes)\n";
        // Continue anyway – user may know what they're doing
    }
    std::cout << "SysEx structure looks OK\n";

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

    // PRTCT_OFF
    std::cout << "Sending PRTCT_OFF...\n";
    auto macroSeq = core::expandMacro("PRTCT_OFF");
    for (const auto& bp : macroSeq)
        for (int r = 0; r < bp.repeat; ++r)
            sendButton(*output, bp.name, deviceId);
    juce::Thread::sleep(120);

    // Send PMEM bank
    std::cout << "Sending performance bank (" << data.getSize() << " bytes, paced)...\n";
    sendPaced(*output, data);

    juce::Thread::sleep(150);
    sendButton(*output, "PERFORM_SELECT", deviceId);

    std::cout << "Performance bank sent successfully.\n";
    return 0;
}
