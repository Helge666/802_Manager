# TX802 Manager — Known Bugs

---

## BUG-001 — TG1 On/Off has no effect on device

**File:** `src/MainComponent.cpp` → `sendPerfParam()`
**Severity:** Medium
**Status:** Open

### Description

In the Performance Editor, toggling TG1 On/Off sends identical PCED Parameter
Change SysEx messages in both cases, so the physical TG1 LED on the device never
responds to the control.

### Root Cause

In `sendPerfParam()`, the LINK parameter value for "On" is computed as `i`
(the 0-based TG index). For TG1, `i = 0`, which is the same value used for "Off":

```cpp
paramNum      = i;                              // 0 for TG1
internalValue = (userValue != 0) ? i : 0;      // On → 0, Off → 0 — identical!
```

For TG2–8 the values are distinct (e.g. TG3 On → value 2, Off → value 0),
so only TG1 is affected.

### Expected Behaviour

Selecting "On" for TG1 should send a value that the TX802 interprets as
"TG1 active", and "Off" should send a value it interprets as "TG1 inactive",
so that the physical LED reflects the selection.

### Where to Fix

`src/MainComponent.cpp`, `sendPerfParam()`, the `"TG"` branch.
The same logic exists in `Tx802HighLevel.h` → `restorePerformanceParams()` and
should be reviewed there too.
