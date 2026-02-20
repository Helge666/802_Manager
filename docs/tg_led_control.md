# TX802 Manager — TG LED Control

## Overview

As established in `startup_sequence.md`, the TX802 has no way to report its own
state back to the app. Controlling the TG LEDs is therefore not cosmetic — it is
a functional requirement. The LEDs are a direct reflection of which TGs are
active on the device. By explicitly driving each LED to a known On or Off state,
the app ensures the device's TG configuration matches what is stored in the
config file. Without this, the device state and the app state could silently
diverge.

The app controls TG LEDs entirely by sending the right commands at the right
times. There are two distinct mechanisms.

---

## Mechanism 1 — PCED LINK Parameter (params 0–7)

This is the **direct** LED control path. Each of the 8 LINK parameters maps
one-to-one to a TG LED.

SysEx format: `F0 43 1n 1A <tg-1> <value> F7`

| Value sent | Meaning |
|------------|---------|
| `tg - 1` (self-index) | TG active → LED **On** |
| `0` | TG inactive → LED **Off** |

> **Note:** For TG1 (index 0), both On and Off produce value `0` — this is
> **BUG-001** (see `BUGS.md`).

### Locations

**`modules/midi/include/midi/Tx802HighLevel.h` → `restorePerformanceParams()`**
Runs automatically at startup (and when "Prepare Device" is clicked).

- *First pass* (line 191): for each TG whose state is On →
  `sendParam(tg - 1, tg - 1)` — sets LINK to self-index.
- *Second pass* (line 229): for each TG whose state is Off →
  `sendParam(tg - 1, 0)` — deferred to ensure preset is loaded before silencing.

**`src/MainComponent.cpp` → `sendPerfParam("TG")`**
Triggered by the Performance Editor TG On/Off dropdown (line 1338/1411).

- On:  `paramNum = i`, value = `i`
- Off: `paramNum = i`, value = `0`
- **BUG-001 applies for TG1** (i=0, both cases send value 0).

**`src/MainComponent.cpp` → `sendPerfParam("PRESET")` secondary send (line 1418)**
After changing the preset on a TG that is currently Off, LINK is immediately
re-sent as 0 to prevent the preset load from accidentally re-activating the TG.

---

## Mechanism 2 — Remote Switch TG Button Presses (codes 89–96)

These simulate physically pressing a TG button on the front panel. Whether the
LED is affected depends on what mode the device is in at the time — in most
cases below they are used purely for **menu navigation**, not to toggle TG
active state.

SysEx format: `F0 43 1n 1B <code> 00 F7`

| Button | Code |
|--------|------|
| TG1 | 89 |
| TG2 | 90 |
| TG3 | 91 |
| TG4 | 92 |
| TG5 | 93 |
| TG6 | 94 |
| TG7 | 95 |
| TG8 | 96 |

### Locations

**`modules/core/src/Tx802Utils.cpp` → `getStartupSequence()`**
Three TG button presses occur as part of menu navigation during startup:

- `TG5` (code 93) — part of `UTILITY → TG5 → YES → YES` sequence
- `TG4` (code 92) — pressed twice as part of `SYSTEM_SETUP → TG4 → TG4 → MINUS_ONE`
- `TG8` (code 96) — part of the `PRTCT_OFF` macro (`SYSTEM_SETUP → TG8 → NO`)

**`src/MainComponent.cpp` → `sendBankToDevice()` (line 1156)**
`PRTCT_OFF` macro is sent before every bank transfer, which again presses TG8
as menu navigation.

**`src/MainComponent.cpp` → Front Panel `fpTgButtons` onClick (lines 434–440)**
Each TG1–TG8 button sends the corresponding Remote Switch press when clicked.
Currently **TODO / not yet wired** to the Left Panel tab.

---

## Summary Table

| Location | Mechanism | TGs affected | Purpose |
|----------|-----------|--------------|---------|
| `restorePerformanceParams()` pass 1 | PCED LINK 0–7 | All On TGs | Set active state on startup |
| `restorePerformanceParams()` pass 2 | PCED LINK 0–7 | All Off TGs | Silence Off TGs after presets load |
| `sendPerfParam("TG")` | PCED LINK 0–7 | One TG at a time | Performance Editor dropdown (**BUG-001 for TG1**) |
| `sendPerfParam("PRESET")` secondary | PCED LINK `i` = 0 | One Off TG | Re-silence after preset change on Off TG |
| `sendStartupSequence()` | Remote Switch TG4, TG5, TG8 | 4, 5, 8 | Menu navigation only |
| `sendBankToDevice()` PRTCT_OFF | Remote Switch TG8 | 8 | Menu navigation only |
| Front Panel `fpTgButtons` onClick | Remote Switch TG1–8 | Any | Direct panel simulation — **not yet wired** |
