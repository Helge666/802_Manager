#pragma once

#include <functional>
#include <juce_core/juce_core.h>
#include "core/Tx802Utils.h"     // All TX802 knowledge lives here
#include "midi/MidiSender.h"
#include "midi/Config.h"

namespace midi {

// Simple file logger for startup diagnostics
class StartupLog {
public:
    static juce::File getLogFile()
    {
        // Write log next to the executable so it's easy to find
        auto exeFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        return exeFile.getParentDirectory().getChildFile("tx802-startup.log");
    }

    static void clear()
    {
        auto f = getLogFile();
        f.deleteFile();
    }

    static void write(const juce::String& msg)
    {
        auto f = getLogFile();
        auto ts = juce::Time::getCurrentTime().toString(true, true, true, true);
        f.appendText(ts + "  " + msg + "\n");
    }
};

/**
 * High-level TX802 operations that require MIDI I/O.
 *
 * This is now a thin wrapper: all TX802 "knowledge" (button codes, macros,
 * conversion helpers, SysEx message formats) lives in core::Tx802Utils.
 * This class only adds the actual MIDI sending via MidiSender.
 */
class Tx802HighLevel {
public:
    static void playTestNotes(MidiSender& sender)
    {
        const int ch = 0;
        const int velocity = 100;
        const int notes[] = { 60, 64, 67 };
        for (int n : notes)
        {
            sender.sendNoteOn(ch, n, velocity);
            juce::Thread::sleep(500);
            sender.sendNoteOff(ch, n);
            juce::Thread::sleep(100);
        }
        for (int n : notes) sender.sendNoteOn(ch, n, velocity);
        juce::Thread::sleep(2000);
        for (int n : notes) sender.sendNoteOff(ch, n);
    }

    // Send a single button press via Remote Switch.
    // Delegates to core::getButtonCode() for the mapping.
    static bool sendButtonByName(MidiSender& sender, const juce::String& buttonName, juce::uint8 deviceId1to16)
    {
        const juce::String name = buttonName.toUpperCase().trim();
        int code = core::getButtonCode(name);

        if (code < 0)
        {
            StartupLog::write("sendButtonByName FAILED: unknown button '" + buttonName + "'");
            return false;
        }

        auto sysex = core::makeRemoteSwitchSysex(deviceId1to16, (juce::uint8) code, 0x00);
        bool ok = sender.sendSysex(sysex);
        StartupLog::write("sendButtonByName '" + name + "' code=" + juce::String(code)
                          + " devId=" + juce::String((int)deviceId1to16)
                          + " -> " + (ok ? "OK" : "FAIL"));
        return ok;
    }

    // Default inter-button delay matching Python's delay=0.1 (100ms)
    static constexpr int kButtonDelayMs = 100;

    // Send a macro (multi-button sequence).
    // Delegates to core::expandMacro() for the sequence definition.
    static bool sendMacroByName(MidiSender& sender, const juce::String& macroName, juce::uint8 deviceId1to16)
    {
        StartupLog::write("sendMacroByName '" + macroName + "'");
        auto seq = core::expandMacro(macroName);
        if (seq.isEmpty())
        {
            StartupLog::write("sendMacroByName: unknown macro '" + macroName + "'");
            return false;
        }
        for (const auto& bp : seq)
        {
            for (int r = 0; r < bp.repeat; ++r)
            {
                sendButtonByName(sender, bp.name, deviceId1to16);
                juce::Thread::sleep(kButtonDelayMs);
            }
        }
        return true;
    }

    // Bring device into a well-known state for bank reception.
    // Uses core::getStartupSequence() for the command list.
    // onDeviceBooted (optional): called once, right after the WAIT that follows RESET —
    // i.e. the device has finished booting but the remaining init steps are still running.
    static void sendStartupSequence(MidiSender& sender, juce::uint8 deviceId1to16,
                                    std::function<void()> onDeviceBooted = nullptr)
    {
        StartupLog::clear();
        StartupLog::write("=== sendStartupSequence BEGIN  deviceId=" + juce::String((int)deviceId1to16)
                          + "  senderOpen=" + juce::String(sender.isOpen() ? "true" : "false") + " ===");

        auto sequence = core::getStartupSequence();
        int step = 0;
        bool resetSeen = false;

        for (const auto& cmd : sequence)
        {
            ++step;
            StartupLog::write("Step " + juce::String(step) + ": " + cmd);

            if (cmd.startsWith("WAIT"))
            {
                int ms = 1000; // default 1 second
                if (cmd.contains("="))
                    ms = cmd.fromFirstOccurrenceOf("=", false, false).getIntValue() * 1000;
                StartupLog::write("  sleeping " + juce::String(ms) + "ms");
                juce::Thread::sleep(ms);
                StartupLog::write("  done sleeping");
                if (resetSeen && onDeviceBooted)
                {
                    onDeviceBooted();
                    onDeviceBooted = nullptr; // fire once only
                }
            }
            else if (core::isMacro(cmd))
            {
                sendMacroByName(sender, cmd, deviceId1to16);
            }
            else
            {
                if (cmd == "RESET" || cmd == "REBOOT") resetSeen = true;
                sendButtonByName(sender, cmd, deviceId1to16);
                juce::Thread::sleep(kButtonDelayMs);
            }
        }

        StartupLog::write("=== sendStartupSequence END ===");
    }

    // Restore performance parameters from saved config.
    // Uses core:: conversion helpers for string-to-internal-value mapping.
    static void restorePerformanceParams(MidiSender& sender, juce::uint8 deviceId1to16,
                                          const ConfigState& cfg,
                                          std::function<void(int tg1to8, bool on)> ledCallback = nullptr)
    {
        StartupLog::write("=== restorePerformanceParams BEGIN ===");

        const int delayMs = 50; // Python uses delay_after=0.05 for edit_performance

        auto sendParam = [&](int paramNum, int value) {
            juce::Array<juce::uint8> vals;
            vals.add((juce::uint8)(value & 0x7F));
            auto sysex = core::makePcedParamChangeSysex(deviceId1to16, (juce::uint8) paramNum, vals);
            bool ok = sender.sendSysex(sysex);
            StartupLog::write("  PCED param=" + juce::String(paramNum) + " val=" + juce::String(value)
                              + " -> " + (ok ? "OK" : "FAIL"));
            juce::Thread::sleep(delayMs);
            return ok;
        };

        auto sendVnum = [&](int paramNum, int internalValue) {
            juce::uint8 msb = (juce::uint8)((internalValue >> 7) & 0x7F);
            juce::uint8 lsb = (juce::uint8)(internalValue & 0x7F);
            juce::Array<juce::uint8> vals;
            vals.add(msb);
            vals.add(lsb);
            auto sysex = core::makePcedParamChangeSysex(deviceId1to16, (juce::uint8) paramNum, vals);
            bool ok = sender.sendSysex(sysex);
            StartupLog::write("  PCED VNUM param=" + juce::String(paramNum) + " internal=" + juce::String(internalValue)
                              + " (msb=" + juce::String((int)msb) + " lsb=" + juce::String((int)lsb) + ")"
                              + " -> " + (ok ? "OK" : "FAIL"));
            juce::Thread::sleep(delayMs);
            return ok;
        };

        // Track which TGs should be turned off (sent last, matching Python behavior)
        juce::Array<int> offTGs;

        for (int tg = 1; tg <= 8; ++tg)
        {
            const auto& t = cfg.tg[tg - 1];
            bool isOn = core::tgOnFromString(t.tgOnOff);
            StartupLog::write("  TG" + juce::String(tg) + " TG=" + t.tgOnOff);

            // LINK: on -> tg-1, off -> 0 (deferred)
            if (isOn)
                sendParam(tg - 1, tg - 1);
            else
                offTGs.add(tg);

            // VNUM: PRESET string -> vnum (1-based) -> internal (0-based)
            // NOTE: Sending VNUM activates the TG on the device as a side effect,
            // lighting its LED regardless of whether the TG is On or Off in config.
            int vnum = core::presetToVnum(t.preset);
            sendVnum(16 + (tg - 1), vnum - 1);
            if (ledCallback) ledCallback(tg, true);

            // RXCH: string -> user (1-16 or 17=Omni) -> internal (user-1)
            int rxUser = core::rxchFromString(t.rxch);
            sendParam(8 + (tg - 1), rxUser == 17 ? 16 : rxUser - 1);

            // NTMTL: note name -> MIDI number
            sendParam(48 + (tg - 1), core::noteNameToMidi(t.noteLow));

            // NTMTH: note name -> MIDI number
            sendParam(56 + (tg - 1), core::noteNameToMidi(t.noteHigh));

            // DETUNE: "-7".."+7" -> internal 0-14 (user + 7)
            sendParam(24 + (tg - 1), t.detune.getIntValue() + 7);

            // NSHFT: "-24".."+24" -> internal 0-48 (user + 24)
            sendParam(64 + (tg - 1), t.noteShift.getIntValue() + 24);

            // OUTVOL: "0"-"99" -> direct
            sendParam(32 + (tg - 1), t.outVol.getIntValue());

            // OUTCH: PAN string -> 0-3
            sendParam(40 + (tg - 1), core::panToOutch(t.pan));

            // FDAMP: "On"/"Off" -> 1/0
            sendParam(72 + (tg - 1), core::fdampFromString(t.fDamp));
        }

        // Second pass: send LINK=0 for TGs that are OFF (matching Python's final_off_commands).
        // This extinguishes TG LEDs that were temporarily activated by their VNUM send above.
        // TG1 is handled last: linking any TG2-8 to TG1 (first pass) forces TG1 On on the
        // device, so TG1's Remote Switch toggle must fire after all other sends to stay Off.
        bool tg1NeedsOff = false;
        for (int tg : offTGs)
        {
            if (tg == 1) { tg1NeedsOff = true; continue; } // handled last
            StartupLog::write("  Sending LINK OFF for TG" + juce::String(tg));
            sendParam(tg - 1, 0);
            if (ledCallback) ledCallback(tg, false);
        }
        if (tg1NeedsOff)
        {
            StartupLog::write("  TG1 Off: sending Remote Switch code 89 (fires last, after all TG2-8 links)");
            sendParam(0, 0); // no effect on device, but consistent with other Off TGs
            sendButtonByName(sender, "TG1", deviceId1to16);
            juce::Thread::sleep(kButtonDelayMs);
            if (ledCallback) ledCallback(1, false);
        }

        StartupLog::write("=== restorePerformanceParams END ===");
    }
};

}
