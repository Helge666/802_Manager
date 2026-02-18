# Next Session Prompt

**Project:** `802_Manager_Windows` — a JUCE C++ app (Windows cross-compile only, build dir: `build-win64`) that manages a Yamaha TX802 synthesizer via MIDI SysEx.

**Branch:** `panelgui`

**What's done:**
- The **Front Panel tab** shows `TX802-Panel-Right.png` (1050×350 px) as background, with transparent `PanelButton` hit-rects positioned via `PanelLayout::Right` constants in `PanelLayout.h`. Buttons are DPI-aware (125% Windows DPI supported). Mode buttons (8) are radio-style; all others are momentary. Images are embedded as `BinaryData` (no files to ship).

**Next task — major redesign around the Left Panel:**

**1. Left Panel image** (`TX802-Panel-Left.png`, same 1050×350 px):
- **Power button** (`PanelLayout::Left::buttonPower`) — momentary send
- **TG1–TG8 buttons** (`PanelLayout::Left::tgButton[8]`) — stateful toggle. TG on/off state is **already tracked by the app and persisted in `802_manager_settings.json`** — connect to that existing state.
- **LED-On.png** overlays (`PanelLayout::Left::tgLed[8]`, 37×16 px, `BinaryData::LEDOn_png`) — drawn on top of active TG buttons in paint()

**2. Tab unification:** The current **Preset Browser** and **Performance Editor** tabs will be **merged into a single tab**, with the left panel image integrated into that combined view.

**3. LCD emulation:** In **Voice Select mode** (when `fpVoiceSelect` is active), the display area (`PanelLayout::Left::displayArea`, 671×72 px) will emulate the TX802's LCD — showing the currently selected voice for the active TG. Other modes do not need LCD emulation yet.

**4. Patch selection workflow change:** The workflow for assigning patches to TGs will be redesigned to be more intuitive. The details of the new workflow need to be discussed at the start of the session.

Key files: `src/MainComponent.h`, `src/MainComponent.cpp`, `src/PanelLayout.h`, `CMakeLists.txt`, `802_manager_settings.json`.
