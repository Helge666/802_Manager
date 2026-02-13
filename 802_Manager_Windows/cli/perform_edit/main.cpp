/**
 * perform_edit – Edit TX802 Performance Edit Buffer (PCED) parameters via SysEx.
 *
 * Port of Python cli/tx802/perform_edit.py.
 * Accepts comma-separated KEY=VALUE pairs like "VNUM1=45,OUTCH2=3,OUTVOL3=99".
 *
 * Usage:
 *   perform_edit --edits "TG1=On,PRESET1=I05,OUTVOL1=99,PAN1=Center"
 *   perform_edit --edits "VNUM1=45,RXCH2=Omni,DETUNE3=3,FDAMP4=On"
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

static bool sendPced(juce::MidiOutput& out, juce::uint8 deviceId,
                      juce::uint8 paramNum, const juce::Array<juce::uint8>& vals, int delayMs)
{
    auto syx = core::makePcedParamChangeSysex(deviceId, paramNum, vals);
    out.sendMessageNow(juce::MidiMessage(syx.getData(), (int)syx.getSize()));
    juce::Thread::sleep(delayMs);
    return true;
}

static bool sendSingleByte(juce::MidiOutput& out, juce::uint8 deviceId,
                             int paramNum, int value, int delayMs)
{
    juce::Array<juce::uint8> vals;
    vals.add((juce::uint8)(value & 0x7F));
    return sendPced(out, deviceId, (juce::uint8)paramNum, vals, delayMs);
}

static bool sendVnum(juce::MidiOutput& out, juce::uint8 deviceId,
                      int paramNum, int internalValue, int delayMs)
{
    juce::Array<juce::uint8> vals;
    vals.add((juce::uint8)((internalValue >> 7) & 0x7F));
    vals.add((juce::uint8)(internalValue & 0x7F));
    return sendPced(out, deviceId, (juce::uint8)paramNum, vals, delayMs);
}

// Parse a key like "VNUM1" into (baseName="VNUM", tgIndex=1)
static bool parseKeyTg(const juce::String& key, juce::String& baseName, int& tg)
{
    // Find where digits start at the end
    int digitStart = key.length();
    while (digitStart > 0 && juce::CharacterFunctions::isDigit(key[digitStart - 1]))
        --digitStart;
    if (digitStart == key.length()) return false; // no digits
    baseName = key.substring(0, digitStart).toUpperCase();
    tg = key.substring(digitStart).getIntValue();
    return tg >= 1 && tg <= 8;
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String edits, portName;
    int deviceId = 1, delayMs = 50;

    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);
        if (arg == "--edits" && i + 1 < argc) edits = juce::String(argv[++i]);
        else if (arg == "--port" && i + 1 < argc) portName = juce::String(argv[++i]);
        else if (arg == "--device-id" && i + 1 < argc) deviceId = juce::jlimit(1, 16, juce::String(argv[++i]).getIntValue());
        else if (arg == "--delay" && i + 1 < argc) delayMs = juce::jmax(1, juce::String(argv[++i]).getIntValue());
        else if (arg == "--list-ports")
        {
            for (const auto& d : juce::MidiOutput::getAvailableDevices())
                std::cout << "  " << d.name.toStdString() << "\n";
            return 0;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: perform_edit --edits <params> [options]\n"
                      << "  --port <name>       MIDI output port\n"
                      << "  --device-id <1-16>  Device ID (default: 1)\n"
                      << "  --delay <ms>        Delay after each param (default: 50)\n"
                      << "  --list-ports        List MIDI ports\n\n"
                      << "Parameters (append TG number 1-8):\n"
                      << "  TG=On/Off           Toggle TG on/off\n"
                      << "  PRESET=I01..B64     Preset by bank/number\n"
                      << "  VNUM=1..256         Voice number (raw)\n"
                      << "  RXCH=1..16/Omni     Receive channel\n"
                      << "  NOTELOW=C-2..G8     Note limit low\n"
                      << "  NOTEHIGH=C-2..G8    Note limit high\n"
                      << "  DETUNE=-7..+7       Detune\n"
                      << "  NOTESHIFT=-24..+24  Note shift\n"
                      << "  OUTVOL=0..99        Output volume\n"
                      << "  PAN=Off/Left/Right/Center  Output assign\n"
                      << "  OUTCH=0..3          Output assign (raw)\n"
                      << "  FDAMP=On/Off        EG forced damp\n"
                      << "  LINK=0..8           Link (0=off, N=TG N)\n"
                      << "  NTMTL=0..127        Note limit low (raw MIDI)\n"
                      << "  NTMTH=0..127        Note limit high (raw MIDI)\n"
                      << "  NSHFT=0..48         Note shift (raw, 24=center)\n\n"
                      << "Example: perform_edit --edits \"TG1=On,PRESET1=I05,OUTVOL1=99\"\n";
            return 0;
        }
        else { std::cerr << "Unknown option: " << arg.toStdString() << "\n"; return 1; }
    }

    if (edits.isEmpty()) { std::cerr << "Error: --edits required\n"; return 1; }

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

    const juce::uint8 devId = (juce::uint8)deviceId;

    // Parse and send each parameter
    juce::StringArray parts;
    parts.addTokens(edits, ",", "");
    bool allOk = true;

    for (const auto& part : parts)
    {
        auto trimmed = part.trim();
        if (!trimmed.contains("=")) { std::cerr << "Invalid format: " << trimmed.toStdString() << "\n"; allOk = false; continue; }

        juce::String key = trimmed.upToFirstOccurrenceOf("=", false, false).trim();
        juce::String val = trimmed.fromFirstOccurrenceOf("=", false, false).trim();

        juce::String baseName;
        int tg;
        if (!parseKeyTg(key, baseName, tg))
        {
            // Handle PNAM separately (index 1-20)
            if (key.toUpperCase().startsWith("PNAM"))
            {
                int idx = key.substring(4).getIntValue();
                if (idx >= 1 && idx <= 20)
                {
                    int charVal = val.length() > 0 ? (int)(char)val[0] : 32;
                    std::cout << "  PNAM" << idx << " = '" << (char)charVal << "' (" << charVal << ")\n";
                    sendSingleByte(*output, devId, 96 + (idx - 1), charVal, delayMs);
                    continue;
                }
            }
            std::cerr << "Cannot parse key: " << key.toStdString() << "\n";
            allOk = false;
            continue;
        }

        int i = tg - 1; // 0-based

        // Dispatch based on parameter name
        if (baseName == "TG")
        {
            bool on = core::tgOnFromString(val);
            int linkVal = on ? i : 0;
            std::cout << "  TG" << tg << " = " << (on ? "On" : "Off") << " (LINK=" << linkVal << ")\n";
            sendSingleByte(*output, devId, i, linkVal, delayMs);
        }
        else if (baseName == "PRESET")
        {
            int vnum = core::presetToVnum(val);
            std::cout << "  PRESET" << tg << " = " << val.toStdString() << " (VNUM=" << vnum << ")\n";
            sendVnum(*output, devId, 16 + i, vnum - 1, delayMs);
        }
        else if (baseName == "VNUM")
        {
            int v = juce::jlimit(1, 256, val.getIntValue());
            std::cout << "  VNUM" << tg << " = " << v << "\n";
            sendVnum(*output, devId, 16 + i, v - 1, delayMs);
        }
        else if (baseName == "RXCH")
        {
            int rxUser = core::rxchFromString(val);
            int internal = (rxUser == 17) ? 16 : rxUser - 1;
            std::cout << "  RXCH" << tg << " = " << val.toStdString() << " (internal=" << internal << ")\n";
            sendSingleByte(*output, devId, 8 + i, internal, delayMs);
        }
        else if (baseName == "NOTELOW")
        {
            int midi = core::noteNameToMidi(val);
            std::cout << "  NOTELOW" << tg << " = " << val.toStdString() << " (MIDI=" << midi << ")\n";
            sendSingleByte(*output, devId, 48 + i, midi, delayMs);
        }
        else if (baseName == "NOTEHIGH")
        {
            int midi = core::noteNameToMidi(val);
            std::cout << "  NOTEHIGH" << tg << " = " << val.toStdString() << " (MIDI=" << midi << ")\n";
            sendSingleByte(*output, devId, 56 + i, midi, delayMs);
        }
        else if (baseName == "NTMTL")
        {
            int v = juce::jlimit(0, 127, val.getIntValue());
            std::cout << "  NTMTL" << tg << " = " << v << "\n";
            sendSingleByte(*output, devId, 48 + i, v, delayMs);
        }
        else if (baseName == "NTMTH")
        {
            int v = juce::jlimit(0, 127, val.getIntValue());
            std::cout << "  NTMTH" << tg << " = " << v << "\n";
            sendSingleByte(*output, devId, 56 + i, v, delayMs);
        }
        else if (baseName == "DETUNE")
        {
            int user = juce::jlimit(-7, 7, val.getIntValue());
            std::cout << "  DETUNE" << tg << " = " << user << " (internal=" << (user + 7) << ")\n";
            sendSingleByte(*output, devId, 24 + i, user + 7, delayMs);
        }
        else if (baseName == "NOTESHIFT" || baseName == "NSHFT")
        {
            int user = juce::jlimit(-24, 24, val.getIntValue());
            std::cout << "  NOTESHIFT" << tg << " = " << user << " (internal=" << (user + 24) << ")\n";
            sendSingleByte(*output, devId, 64 + i, user + 24, delayMs);
        }
        else if (baseName == "OUTVOL")
        {
            int v = juce::jlimit(0, 99, val.getIntValue());
            std::cout << "  OUTVOL" << tg << " = " << v << "\n";
            sendSingleByte(*output, devId, 32 + i, v, delayMs);
        }
        else if (baseName == "PAN")
        {
            int outch = core::panToOutch(val);
            std::cout << "  PAN" << tg << " = " << val.toStdString() << " (OUTCH=" << outch << ")\n";
            sendSingleByte(*output, devId, 40 + i, outch, delayMs);
        }
        else if (baseName == "OUTCH")
        {
            int v = juce::jlimit(0, 3, val.getIntValue());
            std::cout << "  OUTCH" << tg << " = " << v << "\n";
            sendSingleByte(*output, devId, 40 + i, v, delayMs);
        }
        else if (baseName == "FDAMP")
        {
            int v = core::fdampFromString(val);
            std::cout << "  FDAMP" << tg << " = " << val.toStdString() << " (" << v << ")\n";
            sendSingleByte(*output, devId, 72 + i, v, delayMs);
        }
        else if (baseName == "LINK")
        {
            int v = juce::jlimit(0, 8, val.getIntValue());
            std::cout << "  LINK" << tg << " = " << v << "\n";
            sendSingleByte(*output, devId, i, v, delayMs);
        }
        else
        {
            std::cerr << "Unknown parameter: " << baseName.toStdString() << "\n";
            allOk = false;
        }
    }

    std::cout << (allOk ? "All parameters sent.\n" : "Completed with errors.\n");
    return allOk ? 0 : 1;
}
