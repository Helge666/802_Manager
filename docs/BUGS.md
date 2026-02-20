# TX802 Manager — Known Bugs

---

## BUG-001 — TG1 On/Off behaviour is not fully understood

**File:** `src/MainComponent.cpp` → `sendPerfParam()`
**Severity:** Medium
**Status:** Open — needs further investigation before fixing

### Description

In the Performance Editor, toggling TG1 On/Off sends identical PCED Parameter
Change SysEx messages in both cases, so the physical TG1 LED on the device never
responds to the control via SysEx.

### Root Cause (code side)

In `sendPerfParam()`, the LINK parameter value for "On" is computed as `i`
(the 0-based TG index). For TG1, `i = 0`, which is the same value used for "Off":

```cpp
paramNum      = i;                              // 0 for TG1
internalValue = (userValue != 0) ? i : 0;      // On → 0, Off → 0 — identical!
```

For TG2–8 the values are distinct (e.g. TG3 On → value 2, Off → value 0),
so only TG1 is affected.

### Device behaviour (partially understood)

Per `assets/documentation/TX802 TG Linking Info.md`:
> "TG1 values are accepted, but have no function, since TG1 is the anchor TG
> and can't be linked."

This suggests TG1's LINK PCED parameter is ignored by the device entirely —
TG1 is permanently the anchor and always On via SysEx. However, it has been
observed on the physical device that **pressing the physical TG1 button does
turn TG1 off**, which contradicts the documentation. This means there is likely
a distinction between:
- The PCED LINK SysEx parameter (no function for TG1 per docs)
- The Remote Switch TG1 button press (code 89), which may toggle TG1 differently

### What needs investigation

- Confirm whether Remote Switch code 89 (TG1 button) can toggle TG1 off/on
- Determine whether there is any SysEx mechanism to turn TG1 off
- Understand how the app should handle TG1 On/Off in the Performance Editor
  and how the LED overlay should reflect it

### Where to Fix (once understood)

`src/MainComponent.cpp`, `sendPerfParam()`, the `"TG"` branch.
The same logic exists in `Tx802HighLevel.h` → `restorePerformanceParams()`
and should be reviewed there too.
