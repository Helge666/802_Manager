# TODO — Left Panel Integrated Controls

## Context
The Left Panel tab (`leftPanelTab`) currently shows the TX802 panel image
(TX802-Panel-Left.png, 1050×350px) with clickable TG1–TG8 buttons and LED overlays.
**Below** the panel image there is unused space. We are filling that space with
two stacked sections (top to bottom):

1. **TG parameter strip** — one compact interactive row of parameters for the currently selected TG
2. **Inline Preset Browser** — the existing preset browser, without the bank column

Together these replace the need to switch tabs for day-to-day use.

---

## Selected TG concept
- Clicking a TG button **still toggles On/Off** (no change to existing behaviour).
- The click also **selects** that TG, making it the "active" TG for the sections below.
- Only one TG is active at a time.
- **Startup**: preload the TG strip controls from config (same as Performance Editor
  does today — see `updatePerfControlsFromConfig()` in MainComponent.cpp).
  The Preset Browser comes up fresh (no special preload needed).

---

## MAIN — Inline Preset Browser (lower section)

- Reuse the existing preset browser infrastructure (filter, rating, list, pagination).
- **Hide the bank column** — the in-memory bank still exists and is still managed,
  but the right-hand bank slot panel is not shown here.
- **On preset selection**: place the chosen preset into the in-memory bank at the
  slot index matching the active TG (slot 1 = TG1 … slot 8 = TG8), then immediately
  send Y patches to the device (Y = "patches to send" from Settings, default 8).
  Leave open the option of Y = 32 (full bank) for firmware-compatibility reasons.
- The browser comes up fresh (no special preload needed).

---

## SECONDARY — Grey-out when TG is Off

- When the active TG is **Off** (LED off): the TG strip and browser are
  **visible but inaccessible** — covered by a semi-transparent grey overlay
  (suggested: `Colours::grey.withAlpha(0.55f)`), mouse events blocked.
- When the active TG is **On**: overlay is hidden, controls are fully interactive.
- Updates immediately when the TG button is clicked (On/Off state changes).
- If no TG is selected yet, the sections below are hidden.

---

## TERTIARY — TG Parameter Strip (upper section, above browser)

Parameters to show (all others are handled elsewhere or deferred):
- **Excluded — On/Off**: already handled by the clickable TG buttons on the panel image.
- **Excluded — Preset name**: will be shown on the LCD display line 1 (not yet implemented).

Parameters to include — all fully interactive:
`Chan | Low | High | Det | Shift | Vol | Out | Damp`

- Same MIDI/config logic as `sendPerfParam()` in MainComponent.cpp. Reuse unchanged.
- When active TG changes, update all controls to reflect that TG's config values.
- Visual goal: compact and polished. Consider labels above (or as tooltips only)
  rather than a header row. Exact layout to be decided during implementation.

---

## Key files to read first (fresh context)

| File | Why |
|------|-----|
| `802_Manager_Windows/src/MainComponent.h` | Full component/class structure |
| `802_Manager_Windows/src/MainComponent.cpp` | All existing logic: TG buttons, sendPerfParam(), updatePerfControlsFromConfig(), preset browser, bank management |
| `802_Manager_Windows/src/PanelLayout.h` | Pixel positions for Left Panel elements (displayArea, tgButton, tgLed) |
| `assets/gui/TX802-Panel-Left.png` | Visual reference for the left panel image |

---

## Implementation notes / decisions already made

- The Left Panel tab uses DPI-scale-aware bounds: all pixel coords from
  `PanelLayout::Left` are divided by `display->scale` before use as component bounds.
  New sections below the panel image must follow the same pattern.
- `sendPerfParam(tg1to8, paramName, userValue)` is the single entry point for all
  performance parameter changes. It handles MIDI send, config save, LED update,
  and LCD update. Reuse it unchanged from the new TG strip.
- The existing `lpTgButtons` (OwnedArray<PanelButton>) are the TG1–TG8 hit-rects
  on the left panel image. Their `onClick` lambdas currently only toggle On/Off.
  They need to **also** set the selected TG and refresh the sections below.
- `updateLcdFromConfig()` is called automatically via `sendPerfParam()` on any
  TG On/Off change — no additional wiring needed.
- Bank slot indices are 0-based internally (`bankSlotIds[0]` = TG1 slot).

---

## Open implementation questions

- Should the TG strip + browser appear immediately on first launch (defaulting to
  TG1 selected), or only after the user first clicks a TG button?
- Exact height split between TG strip and browser — to be decided visually
  during implementation.
