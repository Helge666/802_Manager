/**
 * press_button – CLI tool to send button presses to a Yamaha TX802 via MIDI SysEx.
 *
 * Port of the Python cli/tx802/press_button.py.
 * Uses only the core library (Tx802Utils) + juce_core + juce_audio_devices.
 *
 * Usage:
 *   press_button --buttons "VOICE_SELECT,TG1,PLUS=5,TEXT=Hello"
 *   press_button --buttons "PRTCT_OFF,RESET" --device-id 2 --delay 150
 *   press_button --list-ports
 */

#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "core/Tx802Utils.h"

// ── MIDI output helpers (minimal, no dependency on midi module) ───────

static std::unique_ptr<juce::MidiOutput> openMidiOutput(const juce::String& portName)
{
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
    {
        if (d.name == portName)
            return juce::MidiOutput::openDevice(d.identifier);
    }
    return nullptr;
}

static bool sendRemoteSwitch(juce::MidiOutput& out, juce::uint8 deviceId, juce::uint8 code)
{
    auto sysex = core::makeRemoteSwitchSysex(deviceId, code, 0x00);
    juce::MidiMessage msg(sysex.getData(), (int) sysex.getSize());
    out.sendMessageNow(msg);
    return true;
}

static bool pressButton(juce::MidiOutput& out, const juce::String& name,
                         juce::uint8 deviceId, int delayMs)
{
    int code = core::getButtonCode(name);
    if (code < 0)
    {
        std::cerr << "  ERROR: Unknown button '" << name.toStdString() << "'\n";
        return false;
    }
    sendRemoteSwitch(out, deviceId, (juce::uint8) code);
    juce::Thread::sleep(delayMs);
    return true;
}

// ── Parse "BUTTON=N" or "TEXT=Hello" ─────────────────────────────────

struct ParsedCommand {
    juce::String name;
    juce::String textParam;   // only for TEXT=...
    int repeatCount { 1 };
};

static ParsedCommand parseButtonSpec(const juce::String& spec)
{
    ParsedCommand cmd;
    if (spec.contains("="))
    {
        cmd.name = spec.upToFirstOccurrenceOf("=", false, false).trim().toUpperCase();
        juce::String rhs = spec.fromFirstOccurrenceOf("=", false, false).trim();

        if (cmd.name.startsWith("CODE"))
        {
            cmd.name = "CODE=" + rhs;
            cmd.repeatCount = 1;
        }
        else if (cmd.name == "TEXT")
        {
            cmd.textParam = rhs;
            cmd.repeatCount = 1;
        }
        else if (cmd.name == "POS1" || cmd.name == "PRTCT_OFF" || cmd.name == "PRTCT_ON")
        {
            cmd.repeatCount = 1;
        }
        else
        {
            cmd.repeatCount = juce::jmax(1, rhs.getIntValue());
        }
    }
    else
    {
        cmd.name = spec.trim().toUpperCase();
    }
    return cmd;
}

// ── Process full button sequence ─────────────────────────────────────

static bool processSequence(juce::MidiOutput& out, const juce::String& sequence,
                             juce::uint8 deviceId, int delayMs, bool verbose)
{
    juce::StringArray parts;
    parts.addTokens(sequence, ",", "");
    bool success = true;

    if (verbose)
    {
        std::cout << "\nProcessing button sequence:";
        for (const auto& p : parts)
            std::cout << " " << p.trim().toStdString();
        std::cout << "\n";
    }

    for (const auto& part : parts)
    {
        auto cmd = parseButtonSpec(part);

        // TEXT=... special case
        if (cmd.name == "TEXT")
        {
            if (verbose)
                std::cout << "  Processing TEXT parameter: '" << cmd.textParam.toStdString() << "'\n";
            auto textSeq = core::getButtonSequenceForText(cmd.textParam);
            for (const auto& bp : textSeq)
            {
                for (int r = 0; r < bp.repeat; ++r)
                {
                    if (!pressButton(out, bp.name, deviceId, delayMs))
                        success = false;
                }
            }
        }
        // Macros (POS1, PRTCT_OFF, PRTCT_ON)
        else if (core::isMacro(cmd.name))
        {
            if (verbose)
                std::cout << "  Processing " << cmd.name.toStdString() << " macro\n";
            auto macroSeq = core::expandMacro(cmd.name);
            for (const auto& bp : macroSeq)
            {
                for (int r = 0; r < bp.repeat; ++r)
                {
                    if (!pressButton(out, bp.name, deviceId, delayMs))
                        success = false;
                }
            }
        }
        // WAIT / WAIT=N
        else if (cmd.name == "WAIT")
        {
            int ms = cmd.repeatCount * 1000;
            if (verbose)
                std::cout << "  Waiting " << ms << " ms\n";
            juce::Thread::sleep(ms);
        }
        // Normal button with optional repeat
        else
        {
            for (int r = 0; r < cmd.repeatCount; ++r)
            {
                if (verbose)
                {
                    if (cmd.repeatCount > 1)
                        std::cout << "  Executing '" << cmd.name.toStdString()
                                  << "' (repeat " << (r + 1) << "/" << cmd.repeatCount << ")\n";
                    else
                        std::cout << "  Executing '" << cmd.name.toStdString() << "'\n";
                }
                if (!pressButton(out, cmd.name, deviceId, delayMs))
                    success = false;
            }
        }
    }

    if (verbose)
        std::cout << "Button sequence completed.\n";

    return success;
}

// ── Main ─────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    // Initialise JUCE without a GUI
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Parse arguments manually (no argparse in C++, keep it simple)
    juce::String buttons;
    juce::String portName;
    int deviceId = 1;
    int delayMs = 100;
    bool verbose = true;
    bool listPorts = false;

    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);
        if (arg == "--list-ports")
        {
            listPorts = true;
        }
        else if (arg == "--buttons" && i + 1 < argc)
        {
            buttons = juce::String(argv[++i]);
        }
        else if (arg == "--port" && i + 1 < argc)
        {
            portName = juce::String(argv[++i]);
        }
        else if (arg == "--device-id" && i + 1 < argc)
        {
            deviceId = juce::jlimit(1, 16, juce::String(argv[++i]).getIntValue());
        }
        else if (arg == "--delay" && i + 1 < argc)
        {
            delayMs = juce::jmax(1, juce::String(argv[++i]).getIntValue());
        }
        else if (arg == "--quiet" || arg == "-q")
        {
            verbose = false;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: press_button [options]\n"
                      << "\n"
                      << "Options:\n"
                      << "  --buttons <seq>     Comma-separated button sequence (required)\n"
                      << "                      e.g. \"VOICE_SELECT,TG1,PLUS=5,TEXT=Hello\"\n"
                      << "  --port <name>       MIDI output port name\n"
                      << "  --device-id <1-16>  MIDI device ID (default: 1)\n"
                      << "  --delay <ms>        Delay after each button press in ms (default: 100)\n"
                      << "  --list-ports        List available MIDI output ports and exit\n"
                      << "  --quiet, -q         Suppress status messages\n"
                      << "  --help, -h          Show this help\n"
                      << "\n"
                      << "Button names:\n"
                      << "  RESET, 0-9, PERFORM_SELECT, VOICE_SELECT, SYSTEM_SETUP,\n"
                      << "  UTILITY, PERFORM_EDIT, VOICE_EDIT_I, VOICE_EDIT_II,\n"
                      << "  STORE, TG1-TG8, YES/PLUS_ONE, NO/MINUS_ONE,\n"
                      << "  INT/CURSOR_LEFT, CRT/CURSOR_RIGHT, ENTER/SPACE,\n"
                      << "  LOWERCASE, UPPERCASE, DASH, CODE=<n>\n"
                      << "\n"
                      << "Macros:\n"
                      << "  POS1, PRTCT_OFF, PRTCT_ON, TEXT=<string>, WAIT, WAIT=<seconds>\n";
            return 0;
        }
        else
        {
            std::cerr << "Unknown option: " << arg.toStdString() << "\n";
            return 1;
        }
    }

    // List ports mode
    if (listPorts)
    {
        auto devices = juce::MidiOutput::getAvailableDevices();
        if (devices.isEmpty())
        {
            std::cout << "No MIDI output ports found.\n";
        }
        else
        {
            std::cout << "Available MIDI output ports:\n";
            for (int i = 0; i < devices.size(); ++i)
                std::cout << "  [" << (i + 1) << "] " << devices[i].name.toStdString() << "\n";
        }
        return 0;
    }

    if (buttons.isEmpty())
    {
        std::cerr << "Error: --buttons is required. Use --help for usage.\n";
        return 1;
    }

    // Auto-detect port if not specified: use first available
    if (portName.isEmpty())
    {
        auto devices = juce::MidiOutput::getAvailableDevices();
        if (devices.isEmpty())
        {
            std::cerr << "Error: No MIDI output ports found.\n";
            return 1;
        }
        portName = devices[0].name;
        if (verbose)
            std::cout << "Auto-selected MIDI port: " << portName.toStdString() << "\n";
    }

    // Open port
    auto output = openMidiOutput(portName);
    if (!output)
    {
        std::cerr << "Error: Could not open MIDI port '" << portName.toStdString() << "'\n";
        return 1;
    }
    if (verbose)
        std::cout << "Opened MIDI port: " << portName.toStdString() << "\n";

    // Process sequence
    bool ok = processSequence(*output, buttons, (juce::uint8) deviceId, delayMs, verbose);

    return ok ? 0 : 1;
}
