# TX802 Manager — Startup / Init Sequence

## Why the Init Sequence Exists

The TX802 has no way to report its own state back to the app. There are no SysEx
status or query messages the device can send, so the app cannot simply ask
"what are your current settings?"

To work around this, on startup the app **forces** the device into a completely
known state by sending a RESET followed by a fixed sequence of button commands.
This is not just housekeeping — it is a prerequisite. Only once the device is in
that known state can the app confidently layer its saved configuration on top, by
sending PCED parameter changes read from the JSON config file. If the app skipped
the init sequence and went straight to restoring config, it would be writing
parameters onto an unknown device state, which could produce unpredictable results.

**In short: RESET → known state → restore saved config.**

---

## Startup Flow

### Phase 1 — `MainComponent` constructor (message thread)

1. UI is built and all tabs are created.
2. Config (`config.json`) is loaded; the previously-used MIDI output port is
   auto-restored via `juce::MessageManager::callAsync`.
3. If the saved output port opens successfully → **`StartupThread` is launched**
   on a background thread.

The same thread is also launched when the user:
- Changes the MIDI output port in the Settings combo box.
- Clicks **"Prepare Device"** in the Settings tab.

---

### Phase 2 — `StartupThread::run()` (background thread)

#### Step A — `sendStartupSequence()` — bring device to known state

Defined in `core::getStartupSequence()` (`Tx802Utils.cpp`):

| Step | Command | Detail |
|------|---------|--------|
| 1 | `RESET` | Hardware reset — puts device in known default state |
| 2 | `WAIT=3` | Wait 3 seconds for device to finish booting (mandatory) |
| 3 | `PRTCT_OFF` | Macro: `SYSTEM_SETUP` → `TG8` → `NO` — disables memory protection |
| 4–7 | `UTILITY` → `TG5` → `YES` → `YES` | Init Performance: links TG2–TG8 to TG1 |
| 8 | `WAIT` | Sleep 1 second |
| 9–12 | `SYSTEM_SETUP` → `TG4` → `TG4` → `MINUS_ONE` | Set Voice Bank receive to I1–I32 |
| 13 | `VOICE_SELECT` | Return LCD to main voice select menu |
| 14 | `TG1` | Remote Switch code 89 — toggles TG1 Off for known state (Init Performance always leaves TG1 On) |

Between each button press: **100 ms delay** (`kButtonDelayMs`).

> **CRITICAL:** This sequence is only safe and correct when sent to a device that
> has just been powered on or hardware-reset. The navigation steps (steps 4–12)
> rely on the device being in its exact default post-reset state. If sent to a
> device already running in any other state, the button presses will land on
> unpredictable parameters and may corrupt the device configuration.
> **The sequence must always be treated as an indivisible unit starting from RESET.**

Each button press is a **Remote Switch SysEx**:
```
F0  43  1n  1B  <code>  00  F7
```
where `n` = deviceId − 1 (e.g. device 1 → `0x10`).

---

#### Step B — `restorePerformanceParams()` — write saved config to device

If no config exists yet, defaults are written first:
- TG1: On, preset I01, channel 1, full note range, vol 90, Center pan, Freeze Damp Off.
- TG2–8: Off, presets I02–I08, same defaults otherwise.

For each TG (1–8), the following **PCED Parameter Change SysEx** messages are sent
(`F0 43 1n 1A <paramNum> <val...> F7`), with **50 ms between each**:

| Param # | Parameter | Encoding |
|---------|-----------|----------|
| `tg-1` (0–7) | LINK (TG on/off) | On=tg-1, Off=0; Off TGs deferred to second pass |
| `16 + tg-1` | VNUM (preset) | 2-byte MSB+LSB, 0-based |
| `8 + tg-1` | RXCH | 0=ch1 … 15=ch16, 16=Omni |
| `48 + tg-1` | NTMTL (note low) | MIDI note number 0–127 |
| `56 + tg-1` | NTMTH (note high) | MIDI note number 0–127 |
| `24 + tg-1` | DETUNE | user value + 7 (range 0–14) |
| `64 + tg-1` | NSHFT (note shift) | user value + 24 (range 0–48) |
| `32 + tg-1` | OUTVOL | 0–99 direct |
| `40 + tg-1` | OUTCH (pan) | 0=Off, 1=Left, 2=Right, 3=Center |
| `72 + tg-1` | FDAMP | 0=Off, 1=On |

**Second pass**: any TGs whose state is Off have their LINK param sent as 0.
This mirrors Python's `final_off_commands` behaviour and ensures Off TGs are
correctly silenced after all presets have been assigned.

---

## SysEx Message Formats

### Remote Switch (button press)
```
F0  43  1n  1B  <buttonCode>  00  F7
```

### PCED Parameter Change (performance edit)
```
F0  43  1n  1A  <paramNum>  <value...>  F7
```

In both formats, `n` = deviceId − 1 (device 1 → byte `0x10`).

---

## Key Source Locations

| Concern | File |
|---------|------|
| Startup sequence definition | `modules/core/src/Tx802Utils.cpp` → `getStartupSequence()` |
| Macro expansion (PRTCT_OFF etc.) | `modules/core/src/Tx802Utils.cpp` → `expandMacro()` |
| Button code table | `modules/core/src/Tx802Utils.cpp` → `getButtonCode()` |
| SysEx message building | `modules/core/src/Tx802Utils.cpp` → `makeRemoteSwitchSysex()`, `makePcedParamChangeSysex()` |
| High-level send logic | `modules/midi/include/midi/Tx802HighLevel.h` → `sendStartupSequence()`, `restorePerformanceParams()` |
| Thread launch / MIDI wiring | `src/MainComponent.cpp` → `MainComponent::MainComponent()` |
