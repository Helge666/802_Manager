# TX802 Manager — Known Bugs

---

## BUG-001 — TG1 On/Off behaviour is not fully understood

**File:** `src/MainComponent.cpp` → `sendPerfParam()`
**Severity:** Medium
**Status:** Fixed — see Resolution below

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

### Device behaviour (confirmed)

Per `assets/documentation/TX802 TG Linking Info.md`:
> "TG1 values are accepted, but have no function, since TG1 is the anchor TG
> and can't be linked."

TG1's PCED LINK parameter is ignored by the device entirely — TG1 is the anchor
TG and cannot be linked or unlinked via SysEx regardless of the value sent.

The only mechanism that controls TG1 On/Off on the device is **Remote Switch
code 89** (the physical TG1 button). This was confirmed by observation: sending
code 89 causes the physical TG1 LED to flicker briefly, proving the device receives
and responds to it.

The reason it only flickered and did not stay toggled during initial testing was
that the `sendPerfParam` call that followed was sending PCED LINK param 0 with
value 0, which the device interprets as "TG1 self-link = anchor = On", immediately
re-enabling TG1.

### Resolution

`sendPerfParam()` was given a TG1-specific early-return path for the `"TG"` param:

- Reads the current TG1 state from config
- Sends Remote Switch code 89 **only if the state is actually changing** (code 89
  is a toggle, not a set/clear — sending it when state is already correct would
  flip it the wrong way)
- Updates config, LED overlay, and status label directly
- Returns before reaching the PCED send block

All other parameters for TG1 (VNUM/PRESET, RXCH, note range, detune, shift, vol,
pan, damp) continue to use PCED parameter changes as normal — this fix is strictly
limited to the `"TG"` On/Off branch.

### Why not fix the code-side arithmetic bug?

Fixing the arithmetic (`internalValue = (userValue != 0) ? i : 0` for i=0) would
not help, because the device ignores TG1's LINK parameter entirely regardless of
the value. The root cause is a device limitation, not just a code bug. The correct
solution is to use a different SysEx mechanism (Remote Switch) for TG1 On/Off only.
