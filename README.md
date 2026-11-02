# 802 Manager

**Turn your Yamaha TX802 into a modern, effortless multi-timbral sound module.**

## ⬇ [Download for Windows](https://github.com/Helge666/802_Manager/releases/latest)

Open the link, then click the `.zip` file under **Assets**. Unzip it and start `802_Manager.exe` -
nothing else to install. The steps are in `INSTALL.txt` inside the zip and under
[Getting Started](#getting-started). Windows only for now - macOS and Linux versions are planned
(see [Roadmap](#roadmap)).

The TX802 is one of the most powerful FM synthesizers ever made — eight independent Tone Generators, each capable of playing a different sound simultaneously, with full per-voice control over channel, note range, volume, pan, detune, and more. Sonically, it still holds up in 2026. Operationally, navigating its tiny 1980s LCD and cryptic button sequences is a different story.

802 Manager connects to your TX802 via MIDI and takes over all configuration from the device itself. Set up your sounds once in a clean, modern interface. Every setting is saved automatically and restored on the next launch — your TX802 comes up exactly where you left it, every time.

---

<img src="docs/images/left-panel.png" width="45%"/> &nbsp; <img src="docs/images/right-panel.png" width="45%"/>
<br><sub>Left: Tone Generator Control & Preset Browser &nbsp;|&nbsp; Right: Macros & Settings</sub>

---

## What it does

**31,000+ presets, instantly searchable.**
The included patch database is curated from the best DX7/TX802 patch collections available — deduplicated by sound data, with ratings, categories, and comments you can edit yourself directly in the browser.

**Layer and split across all 8 Tone Generators.**
Each TG gets its own preset, MIDI channel, note range, volume, pan, detune, and fine-tuning — all from a single screen, without touching the device.

**Fire and forget.**
The app initialises the TX802 on startup and restores your last session automatically. No RAM cartridges, no on-device storage. The app is the single source of truth.

**No LCD diving, ever.**
The TX802's arcane Voice Linking system, performance parameters, and front-panel functions are all surfaced as plain, readable controls. Even the terminology is simplified — *Link* becomes simply *On/Off*, *Voice Data* becomes *Preset*.

**MIDI thru.**
Route your keyboard or DAW through the app to the TX802. Optional Aftertouch-to-Breath (CC2) conversion included.

---

## Getting Started

1. Connect your computer's MIDI OUT to the TX802's MIDI IN. No MIDI return cable is needed.
2. Download the latest release: [github.com/Helge666/802_Manager/releases](https://github.com/Helge666/802_Manager/releases/)
3. Run `802_Manager.exe`. Windows will show an *Unknown Publisher* warning — choose *More Info → Run Anyway*.
4. In the Settings tab, point the database path to `dx_preset_library.sqlite3` from the zip.
5. Select your MIDI output port in the Settings tab. The app will initialise the TX802 and restore your last session.

> **Note on MIDI interfaces:** Some USB MIDI interfaces struggle with the large SysEx bursts the TX802 requires. The popular MIDIFACE 2x2, for example, is unreliable for this purpose. The MIDI output of a Behringer XR18 rack mixer works well.

Settings, the last session and `startup_bank.syx` live in `%APPDATA%\802_Manager`. With logging switched on (Settings → MISCELLANEOUS) the app writes `tx802-startup.log` next to `802_Manager.exe`.

---

## Roadmap

- **macOS version**
- **Linux version**

## Versions

The window title shows the version, e.g. `802 Manager  v1.2.0`. The first two numbers are set by hand in the `VERSION` file: the first for a complete rework or a new framework, the second for larger changes. The third counts itself: the commits that changed the app's code (`app`, `modules`, `assets`) since `VERSION` last changed - same code, same number.

## Building from source (developers)

Requires Visual Studio 2022 (C++ workload) and CMake ≥ 3.22.

```
git clone --recurse-submodules https://github.com/Helge666/802_Manager.git
cd 802_Manager
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The app ends up in `build\802_Manager_App_artefacts\Release`, the command line tools in `build\tools\<tool>\<tool>_artefacts\Release`. `make-release.ps1 -Version vX.Y.Z` packs the app, the preset database, `INSTALL.txt` and the license into the zip for a GitHub release.

## Layout

| Path | Content |
|---|---|
| `app/Source` | The GUI app: `MainComponent` (left panel: banks, TG strip, preset browser; right panel: macros, settings) |
| `modules/core` | TX802/DX7 data: SysEx messages, button codes and macros, startup sequence, performance parameters, voice and bank formats |
| `modules/midi` | MIDI sending, MIDI thru, settings file, startup log |
| `modules/storage` | Preset database (SQLite, compiled in from `modules/thirdparty/sqlite`) |
| `tools` | Command line tools: send banks, presets and performances, press panel buttons, extract, import and create banks, random banks |
| `assets` | Panel images and font, compiled into the app |
| `data` | Preset database `dx_preset_library.sqlite3`, two random example banks |
| `docs` | TX802 behaviour found while building the app: startup sequence, TG LEDs, linking, performance parameters, display texts |
| `external/JUCE` | JUCE 7.0.11 (submodule) |

---

## A Note on AI Assistance

This project was developed with the help of Claude (Anthropic). As a veteran coder since before the TX802 hit the market, I reviewed and contributed code throughout. Or, as Inspector Sledge Hammer used to say — right around the time the TX802 was state of the art — *"Trust me, I know what I'm doing."*

---

## Sources & Acknowledgements

**Preset Database**
The `dx_preset_library.sqlite3` database includes presets curated from:
- https://bobbyblues.recup.ch/yamaha_dx7/dx7_patches.html
- https://homepages.abdn.ac.uk/d.j.benson/pages/html/dx7.html
- https://www.reddit.com/r/synthesizers/comments/e4jkt7/my_curated_dexeddx7_patches_3_banks/

Presets were deduplicated by parameter data (not name). Identical-sounding presets with different names were excluded; different-sounding presets sharing a name were retained. Origin, source file, and other metadata are stored alongside each preset in the database.

**TX802 Brochure Images**
Scanned and uploaded by:
- https://retrosynthads.blogspot.com/2013/06/yamaha-tx802-fm-ton-generator-truly.html

---

## License

Copyright © 2025–2026 Helge T. Kautz

The original source code and assets in this repository are licensed under the
[GNU Affero General Public License v3.0 (AGPLv3)](https://www.gnu.org/licenses/agpl-3.0.html).
You are free to use, modify, and distribute this software — including for porting it to
other platforms — provided that any derivative work is also released under the AGPLv3.

**Third-party components**
- [JUCE](https://juce.com/) — © Raw Material Software / JUCE Ltd., licensed under the AGPLv3 for open-source use
- [SQLite](https://www.sqlite.org/) — public domain
