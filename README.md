# 🎛️ TX802 Manager
v0.5.0 | 2025-04-21 | Helge T. Kautz

TX802 Manager is a feature-rich control suite for the legendary Yamaha TX802 FM tone generator. It’s powered by two core Python libraries that were developed entirely from scratch:

- `tx802_utils.py`
- `dx7_utils.py`

These libraries enable:

- A graphical user interface (Gradio-based), including a patch browser with filtering functionality.
- A suite of optional command-line tools
- Sysex patch extraction and creation, integrity checks, hash-based deduplication, and more
- SQLite database handling: storing sysex + metadata (origin, source syxes file, etc.)
- Full remote operation of the TX802 panel — all buttons, plus new macros like `REBOOT`, `PRTCT_OFF`, `TEXT`
- Live Sysex parameter editing

## 🎨 Design Philosophy and Limitations

TX802 Manager was designed with modern studio users in mind — people who love this vintage classic, but don’t necessarily want to operate it from its (*let’s say*) *“period-accurate”* front panel.

### As such:
- **Cartridge operation is not necessary.** Just push voice data into the device with the click of a button.
- **Full performances are not stored on the device** - that isn't necessary either for studio usage. However, if you really want to, the included CLI tools *can* send performance banks, and you *can* store them using the press_button functionality.
- **Patch editing is not currently included**, but you're encouraged to use [Dexed](https://asb2m10.github.io/dexed/) to create your patches and import them using the included tools. This feature might be added in the future — the core libraries already support voice editing under the hood.

(Besides, the dx_patch_library already contains tens of thousands of patches. Think you'd come up with an entirely new one? Well... who knows 😉)

---

## 📚 Sources & Acknowledgements

**DX7 Patch Library**  
The `dx_patch_library.sqlite3` database includes curated patches from:

- https://bobbyblues.recup.ch/yamaha_dx7/dx7_patches.html
- https://homepages.abdn.ac.uk/d.j.benson/pages/html/dx7.html
- https://www.reddit.com/r/synthesizers/comments/e4jkt7/my_curated_dexeddx7_patches_3_banks/

Patches were deduplicated by their parameter data (not names). Identical-sounding patches with different names were excluded; different-sounding patches with the same name were retained. All meta-information (place of origin, source sysex file, etc. are stored alongside the patch in the database.

**TX802 Brochure Images**  
Scanned and uploaded by:  
- https://retrosynthads.blogspot.com/2013/06/yamaha-tx802-fm-ton-generator-truly.html

**Documentation PDFs & Manuals**  
Collected from various Usenet archives, forums, and synth enthusiast sites over the years. Some new reference documents were created during the development of this tool suite.

---

## 🔧 Installation

It is recommended to use a virtual environment. On Windows, for example:

> git clone <repo-url>  
> cd <repo-folder>  
> python -m venv .venv  
> .venv\Scripts\activate  
> pip install -r requirements.txt

After intallation, start the TX802 Manager GUI with
> python app/main.py

After startup, open the GUI in e.g. Chrome by clicking on http://localhost:7860


## ✅ Tested With

- Windows 11 Pro
- Python 3.12+
- Gradio 5.25.2+
- Mido 1.3.3+
- python-rtmidi 1.5.8+

The application should also work on Linux and macOS, though it hasn't been tested yet. If you add support, please do so in an OS-agnostic way so the tool remains portable and platform-independent.

## 🧪 CLI Test Scenarios

**Tip:** Instead of the GUI, you can use the provided boilerplate scripts from the commandline.

Observe the TX802 Panel; it should reflect the changes these scripts are sending.

**Send a complete patch bank (32 patches) to INT1-INT32**  
> python cli/tx802/send_patch_bank.py --bankfile assets/banks/surprise01.syx

**Send only first two patches**  
*(Unofficial workaround, may not correctly work depending on TX802 firmware)*  
> python cli/tx802/send_patch_bank.py --bankfile assets/banks/surprise01.syx --stopafter 2

**Send a single patch to the edit buffer**  
> python cli/tx802/send_single_patch.py --db config/dx_patch_library.sqlite3 --patchid 666

**Send a button macro**  
> python cli/tx802/press_button.py --buttons PERFORM_EDIT,TG8,TEXT="My Test Case"

**Send a performance bank**  
> python cli/tx802/send_perform_bank.py --bankfile banks/Factory_Sysex/TX802_Factory_Performances.syx

**Send performance parameter changes**  
> python cli/tx802/press_button.py --buttons PERFORM_EDIT,TG2  
> python cli/tx802/perform_edit.py --edits OUTVOL1=95,OUTVOL2=98

**Select the first patch in TG1**  
*(YAMAHA documentation calls patches 'voices', hence the V in VNUM)*  
> python cli/tx802/perform_edit.py --edits VNUM1=1

## 🤖 A Note on AI Assistance

This project was developed with the help of AI tools, but as a veteran coder (since before the TX802 ever hit the market!), I steered the AI diligently, and reviewed the generated code to the best of my abilities. Or, as Inspector Sledge Hammer used to say, "Trust me, I know what I'm doing!"

## 📌 TODO

- Commit patch bank to unit on PE tab switch (if uncommitted)
- Avoid updating unchanged slots in PE
- Add DB selector to Settings
- Ensure Settings setup_tab() runs even if not first tab
- Use resolve_output_port() consistenly.
- Add librarian features (add/save/delete patches and banks)
- Save bank to .syx file in Patch Librarian
- Make rating, category, and comments editable
- Add support for saving performances to syx or DB
- Maybe add a Patch Editor?

## License

This project is licensed under the [Creative Commons Attribution-NonCommercial 4.0 International License](https://creativecommons.org/licenses/by-nc/4.0/).
