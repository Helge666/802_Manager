/**
 * send_single_preset – Send a single DX7 voice preset to the TX802 edit buffer.
 *
 * Port of Python cli/tx802/send_single_preset.py.
 * Supports loading from a .syx file (--presetfile) or from the DB (--presetid --db).
 *
 * Usage:
 *   send_single_preset --presetfile voice.syx --port "USB MIDI"
 *   send_single_preset --presetid 42 --db presets.sqlite3
 */

#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "core/Dx7Utils.h"
#include "core/Tx802Utils.h"
#include "storage/PresetsDb.h"

static std::unique_ptr<juce::MidiOutput> openPort(const juce::String& name)
{
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name == name) return juce::MidiOutput::openDevice(d.identifier);
    return nullptr;
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String presetFile, dbPath, portName;
    int presetId = -1, deviceId = 1;

    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);
        if (arg == "--presetfile" && i + 1 < argc) presetFile = juce::String(argv[++i]);
        else if (arg == "--presetid" && i + 1 < argc) presetId = juce::String(argv[++i]).getIntValue();
        else if (arg == "--db" && i + 1 < argc) dbPath = juce::String(argv[++i]);
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
            std::cout << "Usage: send_single_preset [options]\n"
                      << "  --presetfile <.syx>  Load from file (mutually exclusive with --presetid)\n"
                      << "  --presetid <id>      Load from database\n"
                      << "  --db <path>          Database path (required with --presetid)\n"
                      << "  --port <name>        MIDI output port\n"
                      << "  --device-id <1-16>   Device ID (default: 1)\n"
                      << "  --list-ports         List MIDI ports\n";
            return 0;
        }
        else { std::cerr << "Unknown option: " << arg.toStdString() << "\n"; return 1; }
    }

    if (presetFile.isEmpty() && presetId < 0) { std::cerr << "Error: --presetfile or --presetid required\n"; return 1; }
    if (presetId >= 0 && dbPath.isEmpty()) { std::cerr << "Error: --db required with --presetid\n"; return 1; }

    // Load SysEx data
    juce::MemoryBlock sysexData;

    if (presetFile.isNotEmpty())
    {
        juce::File f(presetFile);
        if (!f.existsAsFile()) { std::cerr << "File not found: " << presetFile.toStdString() << "\n"; return 1; }
        f.loadFileAsData(sysexData);
        std::cout << "Read " << sysexData.getSize() << " bytes from '" << presetFile.toStdString() << "'\n";
    }
    else
    {
        juce::File dbFile(dbPath);
        if (!dbFile.existsAsFile()) { std::cerr << "Database not found: " << dbPath.toStdString() << "\n"; return 1; }
        storage::PresetsDb db(dbFile);
        if (!db.open()) { std::cerr << "Failed to open database\n"; return 1; }

        juce::String err;
        if (!db.getSysexById(presetId, sysexData, err))
        {
            std::cerr << "Preset ID " << presetId << " not found: " << err.toStdString() << "\n";
            return 1;
        }
        std::cout << "Loaded preset ID " << presetId << " from database (" << sysexData.getSize() << " bytes)\n";
    }

    // Validate
    juce::String msg;
    juce::MemoryBlock v155;
    if (!core::Dx7Utils::verifySingleVoiceSysex(sysexData, msg, v155))
    {
        std::cerr << "Invalid voice SysEx: " << msg.toStdString() << "\n";
        return 1;
    }

    juce::String presetName = core::Dx7Utils::extractPresetNameFromSysex(sysexData);
    std::cout << "Preset: '" << presetName.toStdString() << "'\n";

    // Update device ID in SysEx if needed
    auto* bytes = static_cast<juce::uint8*>(sysexData.getData());
    juce::uint8 expectedDevByte = core::toDeviceIdByte((juce::uint8)deviceId);
    if (bytes[2] != expectedDevByte)
    {
        bytes[2] = expectedDevByte;
        std::cout << "Updated device ID byte in SysEx to " << deviceId << "\n";
    }

    // Open port
    if (portName.isEmpty())
    {
        auto devices = juce::MidiOutput::getAvailableDevices();
        if (devices.isEmpty()) { std::cerr << "No MIDI ports found\n"; return 1; }
        portName = devices[0].name;
        std::cout << "Auto-selected port: " << portName.toStdString() << "\n";
    }

    auto output = openPort(portName);
    if (!output) { std::cerr << "Could not open port: " << portName.toStdString() << "\n"; return 1; }

    // Send
    output->sendMessageNow(juce::MidiMessage(sysexData.getData(), (int)sysexData.getSize()));
    juce::Thread::sleep(100);

    // VOICE_SELECT to clear "Data received"
    int code = core::getButtonCode("VOICE_SELECT");
    if (code >= 0)
    {
        auto syx = core::makeRemoteSwitchSysex((juce::uint8)deviceId, (juce::uint8)code);
        output->sendMessageNow(juce::MidiMessage(syx.getData(), (int)syx.getSize()));
    }

    std::cout << "Sent preset '" << presetName.toStdString() << "' to edit buffer.\n";
    return 0;
}
