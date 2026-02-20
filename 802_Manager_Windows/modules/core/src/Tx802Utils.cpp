#include "core/Tx802Utils.h"

namespace core {

// ──────────────────────────────────────────────────────────────────────
//  SysEx message building
// ──────────────────────────────────────────────────────────────────────

juce::MemoryBlock buildRemoteSwitchPayload(juce::uint8 deviceId1to16,
                                            juce::uint8 buttonCode,
                                            juce::uint8 value)
{
    const juce::uint8 dev = toDeviceIdByte(deviceId1to16);
    const juce::uint8 bytes[] { kYamahaId, dev, kRemoteSwitchGroupSubgroup, buttonCode, value };
    return { bytes, sizeof(bytes) };
}

juce::MemoryBlock buildPcedParamChangePayload(juce::uint8 deviceId1to16,
                                               juce::uint8 paramNumber,
                                               const juce::Array<juce::uint8>& values)
{
    const juce::uint8 dev = toDeviceIdByte(deviceId1to16);
    const juce::uint8 header[] { kYamahaId, dev, kPcedParamChangeGroupSubgroup, paramNumber };
    juce::MemoryBlock msg(header, sizeof(header));
    if (values.isEmpty())
    {
        const juce::uint8 zero = 0;
        msg.append(&zero, 1);
    }
    else
    {
        for (auto v : values)
        {
            const juce::uint8 nv = (juce::uint8)(v & 0x7F);
            msg.append(&nv, 1);
        }
    }
    return msg;
}

juce::MemoryBlock wrapWithSysex(const juce::MemoryBlock& payload)
{
    juce::MemoryBlock msg;
    const juce::uint8 f0 = 0xF0, f7 = 0xF7;
    msg.append(&f0, 1);
    msg.append(payload.getData(), payload.getSize());
    msg.append(&f7, 1);
    return msg;
}

juce::MemoryBlock makeRemoteSwitchSysex(juce::uint8 deviceId1to16,
                                         juce::uint8 buttonCode,
                                         juce::uint8 value)
{
    return wrapWithSysex(buildRemoteSwitchPayload(deviceId1to16, buttonCode, value));
}

juce::MemoryBlock makePcedParamChangeSysex(juce::uint8 deviceId1to16,
                                            juce::uint8 paramNumber,
                                            const juce::Array<juce::uint8>& values)
{
    return wrapWithSysex(buildPcedParamChangePayload(deviceId1to16, paramNumber, values));
}

// ──────────────────────────────────────────────────────────────────────
//  Button codes
// ──────────────────────────────────────────────────────────────────────

int getButtonCode(const juce::String& buttonName)
{
    const juce::String n = buttonName.toUpperCase().trim();

    if (n == "RESET" || n == "REBOOT")   return 64;

    if (n.length() == 1 && n[0] >= '0' && n[0] <= '9')
        return 65 + ((char)n[0] - '0');

    if (n == "INT" || n == "CURSOR_LEFT")  return 75;
    if (n == "CRT" || n == "CURSOR_RIGHT") return 76;
    if (n == "SPACE" || n == "ENTER")      return 77;
    if (n == "LOWERCASE")                  return 78;
    if (n == "UPPERCASE")                  return 79;
    if (n == "DASH")                       return 80;

    if (n == "MINUS_ONE" || n == "OFF" || n == "NO")  return 78;
    if (n == "PLUS_ONE"  || n == "ON"  || n == "YES") return 79;
    if (n == "STORE" || n == "COMPARE")               return 88;

    if (n == "PERFORM_SELECT") return 81;
    if (n == "VOICE_SELECT")   return 82;
    if (n == "SYSTEM_SETUP")   return 83;
    if (n == "UTILITY")        return 84;
    if (n == "PERFORM_EDIT")   return 85;
    if (n == "VOICE_EDIT_I")   return 86;
    if (n == "VOICE_EDIT_II")  return 87;

    if (n == "TG1") return 89;
    if (n == "TG2") return 90;
    if (n == "TG3") return 91;
    if (n == "TG4") return 92;
    if (n == "TG5") return 93;
    if (n == "TG6") return 94;
    if (n == "TG7") return 95;
    if (n == "TG8") return 96;

    if (n.startsWith("CODE="))
    {
        int v = n.fromFirstOccurrenceOf("=", false, false).getIntValue();
        if (v >= 0 && v <= 127) return v;
    }

    return -1;
}

// ──────────────────────────────────────────────────────────────────────
//  Macros
// ──────────────────────────────────────────────────────────────────────

juce::Array<ButtonPress> expandMacro(const juce::String& macroName)
{
    const juce::String n = macroName.toUpperCase().trim();
    juce::Array<ButtonPress> seq;

    if (n == "POS1")
    {
        seq.add({ "CURSOR_LEFT", 19 });
    }
    else if (n == "PRTCT_OFF")
    {
        seq.add({ "SYSTEM_SETUP", 1 });
        seq.add({ "TG8", 1 });
        seq.add({ "NO", 1 });
    }
    else if (n == "PRTCT_ON")
    {
        seq.add({ "SYSTEM_SETUP", 1 });
        seq.add({ "TG8", 1 });
        seq.add({ "YES", 1 });
    }
    return seq;
}

bool isMacro(const juce::String& name)
{
    const juce::String n = name.toUpperCase().trim();
    return n == "POS1" || n == "PRTCT_OFF" || n == "PRTCT_ON";
}

// ──────────────────────────────────────────────────────────────────────
//  Startup sequence
// ──────────────────────────────────────────────────────────────────────

juce::StringArray getStartupSequence()
{
    // IMPORTANT: This sequence is only safe and correct when sent to a device
    // that has just been powered on or hardware-reset. The navigation steps that
    // follow RESET rely on the device being in its exact default post-reset state.
    // If sent to a device already running in any other state, the button presses
    // will land on unpredictable parameters and may corrupt the device configuration.
    // The sequence must always be treated as an indivisible unit starting from RESET.
    return {
        "RESET",                                         // Hardware reset — puts device in known default state
        "WAIT=3",                                        // Wait for device to finish booting (mandatory)
        "PRTCT_OFF",                                     // Disable memory protection (SYSTEM_SETUP -> TG8 -> NO)
        "UTILITY", "TG5", "YES", "YES", "WAIT",          // Init Performance: links TG2-TG8 to TG1
        "SYSTEM_SETUP", "TG4", "TG4", "MINUS_ONE",       // Set Voice Bank receive to I1-I32
        "VOICE_SELECT",                                  // Return LCD to main voice select menu
        "TG1"                                            // Init Performance leaves TG1 always On; toggle Off for known state
    };
}

// ──────────────────────────────────────────────────────────────────────
//  MIDI note name helpers
// ──────────────────────────────────────────────────────────────────────

juce::String midiToNoteName(int note)
{
    if (note < 0 || note > 127) return "C-2";
    static const char* notes[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    int octave = (note / 12) - 2;
    return juce::String(notes[note % 12]) + juce::String(octave);
}

int noteNameToMidi(const juce::String& name)
{
    for (int i = 0; i <= 127; ++i)
        if (midiToNoteName(i).equalsIgnoreCase(name))
            return i;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
//  Performance parameter conversion helpers
// ──────────────────────────────────────────────────────────────────────

int panToOutch(const juce::String& pan)
{
    auto p = pan.toUpperCase().trim();
    if (p == "OFF")                   return 0;
    if (p == "I" || p == "LEFT")      return 1;
    if (p == "II" || p == "RIGHT")    return 2;
    if (p == "I+II" || p == "CENTER") return 3;
    return 3;
}

juce::String outchToPan(int outch)
{
    switch (outch)
    {
        case 0: return "Off";
        case 1: return "Left";
        case 2: return "Right";
        case 3: return "Center";
        default: return "Center";
    }
}

int fdampFromString(const juce::String& s)
{
    return s.toUpperCase().trim() == "ON" ? 1 : 0;
}

juce::String fdampToString(int v)
{
    return v != 0 ? "On" : "Off";
}

bool tgOnFromString(const juce::String& s)
{
    return s.toUpperCase().trim() == "ON";
}

juce::String tgOnToString(bool on)
{
    return on ? "On" : "Off";
}

int presetToVnum(const juce::String& preset)
{
    auto p = preset.toUpperCase().trim();
    if (p.length() < 2) return 1;
    char bank = (char)p[0];
    int num = p.substring(1).getIntValue();
    if (num < 1 || num > 64) num = 1;
    int base = 0;
    switch (bank)
    {
        case 'I': base = 0;   break;
        case 'C': base = 64;  break;
        case 'A': base = 128; break;
        case 'B': base = 192; break;
        default:  base = 0;   break;
    }
    return base + num;
}

juce::String vnumToPreset(int vnum)
{
    if (vnum < 1) vnum = 1;
    if (vnum > 256) vnum = 256;
    int v = vnum - 1;
    char bank;
    if (v < 64)       bank = 'I';
    else if (v < 128) bank = 'C';
    else if (v < 192) bank = 'A';
    else               bank = 'B';
    int num = (v % 64) + 1;
    return juce::String::charToString(bank) + juce::String(num).paddedLeft('0', 2);
}

int rxchFromString(const juce::String& s)
{
    if (s.toUpperCase().trim() == "OMNI") return 17;
    return juce::jlimit(1, 16, s.getIntValue());
}

juce::String rxchToString(int rxch)
{
    if (rxch == 17) return "Omni";
    return juce::String(juce::jlimit(1, 16, rxch));
}

// ──────────────────────────────────────────────────────────────────────
//  Character-to-button mapping
// ──────────────────────────────────────────────────────────────────────

juce::Array<ButtonPress> getButtonSequenceForChar(char ch)
{
    juce::Array<ButtonPress> seq;

    // Lowercase letters
    if (ch >= 'a' && ch <= 'z')
    {
        int idx = ch - 'a';
        int digit = idx / 3;
        int press = (idx % 3) + 2;
        seq.add({ "LOWERCASE", 1 });
        seq.add({ juce::String(digit), press });
        return seq;
    }

    // Uppercase letters
    if (ch >= 'A' && ch <= 'Z')
    {
        int idx = ch - 'A';
        int digit = idx / 3;
        int press = (idx % 3) + 2;
        seq.add({ "UPPERCASE", 1 });
        seq.add({ juce::String(digit), press });
        return seq;
    }

    // Digits
    if (ch >= '0' && ch <= '9')
    {
        seq.add({ juce::String((int)(ch - '0')), 1 });
        return seq;
    }

    // Special characters
    switch (ch)
    {
        case ' ':  seq.add({ "SPACE", 1 }); break;
        case '!':  seq.add({ "8", 4 }); break;
        case '#':  seq.add({ "9", 2 }); break;
        case '&':  seq.add({ "9", 3 }); break;
        case '-':  seq.add({ "DASH", 1 }); break;
        case '/':  seq.add({ "DASH", 2 }); break;
        case '.':  seq.add({ "DASH", 3 }); break;
        case '\'': seq.add({ "DASH", 4 }); break;
        default:   seq.add({ "DASH", 3 }); break; // fallback = '.'
    }
    return seq;
}

juce::Array<ButtonPress> getButtonSequenceForText(const juce::String& text)
{
    juce::Array<ButtonPress> seq;
    juce::String padded = text.substring(0, 20).paddedRight(' ', 20);

    // Go to first position
    seq.add({ "CURSOR_LEFT", 19 });

    juce::String currentCase;

    for (int i = 0; i < 20; ++i)
    {
        char ch = (char) padded[i];
        auto charSeq = getButtonSequenceForChar(ch);

        for (const auto& bp : charSeq)
        {
            if (bp.name == "UPPERCASE" || bp.name == "LOWERCASE")
            {
                if (bp.name != currentCase)
                {
                    seq.add(bp);
                    currentCase = bp.name;
                }
            }
            else
                seq.add(bp);
        }

        // Advance cursor after each char except spaces (auto-advance) and last char
        if (ch != ' ' && i < 19)
            seq.add({ "CURSOR_RIGHT", 1 });
    }
    return seq;
}

// ──────────────────────────────────────────────────────────────────────
//  Backward-compat class methods
// ──────────────────────────────────────────────────────────────────────

juce::MemoryBlock Tx802Utils::buildRemoteSwitch(juce::uint8 deviceId, juce::uint8 code, juce::uint8 value)
{
    return buildRemoteSwitchPayload(deviceId, code, value);
}

juce::MemoryBlock Tx802Utils::buildPcedParamChange(juce::uint8 deviceId, juce::uint8 paramNum, const juce::Array<juce::uint8>& vals)
{
    return buildPcedParamChangePayload(deviceId, paramNum, vals);
}

} // namespace core
