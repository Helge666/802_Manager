# 802 Manager — Project Guide for Claude

## Repository layout
- **Source root**: `src/` — `MainComponent.h` / `MainComponent.cpp` + `Main.cpp`
- **Modules**: `modules/core`, `modules/midi`, `modules/storage`
- **CLI tools**: `cli/`
- **Build**: `build-win64/` — Ninja, MinGW cross-compile → Windows x86_64
- **Assets**: `../assets/gui/` (PNG panel images), `../assets/fonts/`
- **Build command**: `cd build-win64 && ninja 802_Manager_Windows_App`

## Version scheme
- Format: `vMAJOR.MINOR.BUILD` — e.g. `v1.1.7`
- **MAJOR / MINOR**: hand-edited in `scripts/bump_build.sh` (lines `VERSION_MAJOR` / `VERSION_MINOR`)
- **BUILD**: auto-incremented by `scripts/bump_build.sh` on every `ninja` run
  - Stored in `build_number.txt` (committed, integer, contains the number of the *last completed* build)
  - Generated into `src/Version.h` (gitignored) before each compile
- **Window title**: `802 Manager  vMAJOR.MINOR.BUILD` (set in `src/Main.cpp`)
- **To start a new minor series** (e.g. v1.2): edit `VERSION_MINOR` in the script and reset
  `build_number.txt` to `0`
- **To release**: whatever build number is current becomes the release (e.g. ninja produced
  `v1.1.47` → tag `v1.1.47` on GitHub; no manual version bump needed)

## Build number file contract
| File | Committed? | Contents |
|---|---|---|
| `build_number.txt` | **Yes** | integer N — last completed build was `vMAJOR.MINOR.N`; next build produces `vMAJOR.MINOR.N+1` |
| `src/Version.h` | No (gitignored) | `#define APP_VERSION_*` constants, re-generated each build |
| `scripts/bump_build.sh` | **Yes** | owns MAJOR/MINOR, reads/writes the two files above |

## Git workflow
- Main branch: `main`
- Feature branches: `feature/<name>`, merged back to main
- **Do NOT commit or push without explicit user approval**
- Release: `gh release create vX.Y.Z` with `802_Manager.exe` + `dx_preset_library.sqlite3` as zip

## Key architectural notes
- JUCE 7.0.11; GUI is `MainComponent` (single component, two tabs)
- Left Panel tab: bank strip → TG perf strip → preset browser
- Right Panel tab: macro strip → settings section
- Config file (JSON): `%APPDATA%\802_Manager\802_manager_settings.json` on Windows
- DB: `dx_preset_library.sqlite3` — path stored in config
- `startup_bank.syx` written on every bank send; restored on startup
