#pragma once

#include <juce_core/juce_core.h>

namespace core {

/**
 * TX802 utility library – self-contained, no MIDI I/O dependency.
 *
 * Mirrors the Python tx802_utils.py: constants, button codes, macros,
 * startup sequence, performance parameter helpers, SysEx message building,
 * character-to-button mapping, and human-readable value conversions.
 *
 * Any application can link against this library without needing the midi module.
 */

// ──────────────────────────────────────────────────────────────────────
//  Constants
// ──────────────────────────────────────────────────────────────────────

constexpr juce::uint8 kYamahaId                          = 0x43;
constexpr juce::uint8 kPcedParamChangeGroupSubgroup      = 0x1A;
constexpr juce::uint8 kRemoteSwitchGroupSubgroup         = 0x1B;

constexpr int kVmemVoiceSize      = 128;   // packed voice in bank
constexpr int kVmemHeaderSize     = 6;     // F0 43 0n 09 20 00
constexpr int kVmemExpectedSize   = 4104;  // full 32-voice bank SysEx
constexpr int kPmemBulkSize       = 11589; // performance memory bank

// VMEM header bytes following F0 43 0n
constexpr juce::uint8 kVmemHeaderStart[] = { 0x09, 0x20, 0x00 };
// PMEM header bytes following F0 43 0n
constexpr juce::uint8 kPmemHeaderStart[] = { 0x7E, 0x01, 0x28 };

// ──────────────────────────────────────────────────────────────────────
//  Device ID helper (trivial, kept inline)
// ──────────────────────────────────────────────────────────────────────

/** Convert 1-based device ID (1–16) to the byte used in SysEx (0x10–0x1F). */
inline juce::uint8 toDeviceIdByte(juce::uint8 deviceId1to16)
{
    return (juce::uint8)(0x10 + ((deviceId1to16 - 1) & 0x0F));
}

// ──────────────────────────────────────────────────────────────────────
//  SysEx message building (no I/O – returns raw MemoryBlock)
// ──────────────────────────────────────────────────────────────────────

/** Build Remote Switch payload (without F0/F7 framing). */
juce::MemoryBlock buildRemoteSwitchPayload(juce::uint8 deviceId1to16,
                                            juce::uint8 buttonCode,
                                            juce::uint8 value = 0x00);

/** Build PCED Parameter Change payload (without F0/F7 framing). */
juce::MemoryBlock buildPcedParamChangePayload(juce::uint8 deviceId1to16,
                                               juce::uint8 paramNumber,
                                               const juce::Array<juce::uint8>& values);

/** Wrap a payload with F0 … F7 to make a complete SysEx message. */
juce::MemoryBlock wrapWithSysex(const juce::MemoryBlock& payload);

/** Build complete Remote Switch SysEx (F0 … F7). */
juce::MemoryBlock makeRemoteSwitchSysex(juce::uint8 deviceId1to16,
                                         juce::uint8 buttonCode,
                                         juce::uint8 value = 0x00);

/** Build complete PCED Parameter Change SysEx (F0 … F7). */
juce::MemoryBlock makePcedParamChangeSysex(juce::uint8 deviceId1to16,
                                            juce::uint8 paramNumber,
                                            const juce::Array<juce::uint8>& values);

// ──────────────────────────────────────────────────────────────────────
//  Button codes  (matches Python BUTTON_CODES dict in press_button())
// ──────────────────────────────────────────────────────────────────────

/** Return the Remote Switch code for a button name, or -1 if unknown. */
int getButtonCode(const juce::String& buttonName);

// ──────────────────────────────────────────────────────────────────────
//  Macros – virtual multi-button commands
// ──────────────────────────────────────────────────────────────────────

struct ButtonPress {
    juce::String name;
    int repeat { 1 };
};

/** Expand a macro name into a sequence of button presses.
 *  Known macros: POS1, PRTCT_OFF, PRTCT_ON.
 *  Returns empty array for unknown macros. */
juce::Array<ButtonPress> expandMacro(const juce::String& macroName);

/** Check whether a name is a known macro. */
bool isMacro(const juce::String& name);

// ──────────────────────────────────────────────────────────────────────
//  Startup sequence  (matches Python tx802_startup_items())
// ──────────────────────────────────────────────────────────────────────

/** Return the standard TX802 startup command list.
 *  Items starting with "WAIT" are sleep commands (WAIT = 1s, WAIT=N = N seconds).
 *  Items like "PRTCT_OFF" are macros expanded by expandMacro(). */
juce::StringArray getStartupSequence();

// ──────────────────────────────────────────────────────────────────────
//  MIDI note name helpers  (matches Python get_midi_note_name / MIDI_NOTES)
// ──────────────────────────────────────────────────────────────────────

/** Convert MIDI note number (0–127) to name like "C-2", "C#3", "G8". */
juce::String midiToNoteName(int note);

/** Convert note name (e.g. "C#3", "G8") back to MIDI number (0–127). */
int noteNameToMidi(const juce::String& name);

// ──────────────────────────────────────────────────────────────────────
//  Performance parameter conversion helpers
//  (matches Python edit_performance() mappings)
// ──────────────────────────────────────────────────────────────────────

int panToOutch(const juce::String& pan);
juce::String outchToPan(int outch);

int fdampFromString(const juce::String& s);
juce::String fdampToString(int v);

bool tgOnFromString(const juce::String& s);
juce::String tgOnToString(bool on);

int presetToVnum(const juce::String& preset);
juce::String vnumToPreset(int vnum);

int rxchFromString(const juce::String& s);
juce::String rxchToString(int rxch);

// ──────────────────────────────────────────────────────────────────────
//  Character-to-button mapping  (matches Python get_button_sequence_for_char)
// ──────────────────────────────────────────────────────────────────────

/** Return button-press sequence to type a single character on the TX802. */
juce::Array<ButtonPress> getButtonSequenceForChar(char ch);

/** Convert a text string (max 20 chars) into a full button-press sequence
 *  for entering text on the TX802, including POS1 and cursor movement.
 *  Matches Python process_text_parameter(). */
juce::Array<ButtonPress> getButtonSequenceForText(const juce::String& text);

// ──────────────────────────────────────────────────────────────────────
//  Per-TG performance state  (Python-compatible config format)
// ──────────────────────────────────────────────────────────────────────

struct TgState {
    juce::String tgOnOff   { "Off" };
    juce::String preset    { "I01" };
    juce::String rxch      { "1" };
    juce::String noteLow   { "C-2" };
    juce::String noteHigh  { "G8" };
    juce::String detune    { "0" };
    juce::String noteShift { "0" };
    juce::String outVol    { "90" };
    juce::String pan       { "Center" };
    juce::String fDamp     { "Off" };
};

// ──────────────────────────────────────────────────────────────────────
//  Backward-compat class interface (wraps the free functions above)
// ──────────────────────────────────────────────────────────────────────

class Tx802Utils {
public:
    static juce::MemoryBlock buildRemoteSwitch(juce::uint8 deviceId, juce::uint8 code, juce::uint8 value = 0x00);
    static juce::MemoryBlock buildPcedParamChange(juce::uint8 deviceId, juce::uint8 paramNum, const juce::Array<juce::uint8>& vals);
};

} // namespace core
