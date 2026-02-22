#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "midi/MidiSender.h"
#include "midi/MidiThru.h"
#include "storage/PresetsDb.h"

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:

    // Model for the bank list on the right (variable slot count)
    class BankModel : public juce::ListBoxModel
    {
    public:
        BankModel() = default;
        void initOwner(MainComponent* o, int slots = 8)
        {
            owner = o;
            numRows = slots;
            slotNames.clear();
            for (int i = 0; i < numRows; ++i) slotNames.add("empty");
        }
        int getNumRows() override { return numRows; }
        void setNumSlots(int n)
        {
            numRows = n;
            while (slotNames.size() < numRows) slotNames.add("empty");
            while (slotNames.size() > numRows) slotNames.remove(slotNames.size() - 1);
        }
        void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
        juce::Component* refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existing) override;
        void setSlotName(int index, const juce::String& name)
        {
            if (juce::isPositiveAndBelow(index, slotNames.size())) slotNames.set(index, name);
        }
        juce::String getSlotName(int index) const
        {
            return juce::isPositiveAndBelow(index, slotNames.size()) ? slotNames[index] : juce::String();
        }
    private:
        int numRows { 8 };
        juce::StringArray slotNames;
        MainComponent* owner { nullptr };
    };

    // Bank slot row component to accept drops and support reordering
    class BankSlotComponent : public juce::Component, public juce::DragAndDropTarget
    {
    public:
        BankSlotComponent(MainComponent& o, int idx) : owner(o), index(idx) {}
        void updateIndex(int idx, bool isSelected) { index = idx; selected = isSelected; repaint(); }
        void paint(juce::Graphics& g) override;
        // DragAndDropTarget
        bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
        void itemDragEnter(const SourceDetails& dragSourceDetails) override;
        void itemDragExit(const SourceDetails& dragSourceDetails) override;
        void itemDropped(const SourceDetails& dragSourceDetails) override;
        // For reordering: allow dragging a bank row
        void mouseDrag(const juce::MouseEvent& e) override;
    private:
        MainComponent& owner;
        int index { 0 };
        bool over { false };
        bool selected { false };
    };

    // Custom tab component that draws the panel image as its background
    class FrontPanelTab : public juce::Component
    {
    public:
        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colours::black);
            if (image.isValid())
            {
                float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
                g.drawImage(image, 0, 0,
                            juce::roundToInt(image.getWidth()  / scale),
                            juce::roundToInt(image.getHeight() / scale),
                            0, 0, image.getWidth(), image.getHeight());
            }
        }
        juce::Image image;
    };

    // Left panel tab — TX802-Panel-Left.png, same DPI-aware rendering as FrontPanelTab.
    class LeftPanelTab : public FrontPanelTab {};

    // TG LED overlays (LED-On.png) drawn on top of each TG button in the Left Panel tab.
    // Index 0 = TG1 … 7 = TG8. Visibility is controlled via setTgLed().
    juce::ImageComponent tgLedOverlay[8];
    void setTgLed(int tg1to8, bool on);

    // Transparent hit-rect button overlaid on the panel image.
    // momentary=true  → darkens while held, clears on release
    // momentary=false → darkens on press, stays dark (radio group behavior)
    class PanelButton : public juce::Component
    {
    public:
        bool momentary { true };
        bool active    { false };
        std::function<void()> onClick;

        void setActive(bool a) { active = a; repaint(); }

        void paint(juce::Graphics& g) override
        {
            if (active)
                g.fillAll(juce::Colours::black.withAlpha(0.38f));
        }
        void mouseDown(const juce::MouseEvent&) override
        {
            active = true; repaint();
            if (onClick) onClick();
        }
        void mouseUp(const juce::MouseEvent&) override
        {
            if (momentary) { active = false; repaint(); }
        }
    };

    // LCD text display overlaid on the display area of TX802-Panel-Left.png.
    // setLine(0, ...) updates the top line; setLine(1, ...) updates the bottom line.
    // Text is clipped to 40 characters and rendered in the system monospace font.
    class LcdDisplay : public juce::Component
    {
    public:
        void setLine(int line0or1, const juce::String& text)
        {
            lines[juce::jlimit(0, 1, line0or1)] = text.substring(0, 40);
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            const float w = (float)getWidth();
            const float h = (float)getHeight();
            // Font height: constrained so 40 chars fit in width (mono ratio ~0.6) and 2 lines fit in height
            const float fontH = std::min(h * 0.44f, w / (40.0f * 0.6f));
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), fontH, juce::Font::plain));
            g.setColour(juce::Colour(0xFF0F2000)); // dark green on LCD green background
            const float lineH = h * 0.5f;
            const int leftPad   = 4; // px gap from left edge of display area
            const int line0OffY = 6; // px to shift top line downward
            for (int i = 0; i < 2; ++i)
            {
                int y = juce::roundToInt(i * lineH) + (i == 0 ? line0OffY : 0);
                g.drawText(lines[i], leftPad, y,
                           (int)w - leftPad, juce::roundToInt(lineH),
                           juce::Justification::centredLeft, false);
            }
        }

    private:
        juce::String lines[2];
    };

    // (Deprecated) Per-row component approach removed; using painted arrows + click handling instead

    void rebuildMidiOutputs();
    void sendReboot();
    // Bank helpers
    void setBankSlotFromPreset(int slotIndex, int presetId, const juce::String& name);
    void moveBankSlot(int fromIndex, int toIndex);
    void initBank();
    void randomizeBank();
    void sendBankToDevice();

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    // ─── Preset database + bank state (shared with LP browser) ──────────────────
    juce::TextButton selectDbButton { "Select DB File" };  // hosted in rpSettingsSection
    juce::Label      dbPathLabel;
    BankModel        bankModel;
    juce::Array<int> bankSlotIds;   // visibleSlots entries, 0 = empty
    int              visibleSlots { 8 };
    int              pageSize { 100 };  // used by LP browser pagination
    std::unique_ptr<storage::PresetsDb>  presetsDb;
    std::unique_ptr<juce::FileChooser>   dbFileChooser;

    // Performance Editor tab
    juce::Component performanceEditorTab;
    void sendPerfParam(int tg1to8, const juce::String& paramName, int userValue);
    void updatePerfControlsFromConfig();
    void refreshPerfPresetDropdowns();
    void setLpRowVisible(int tg0based, bool visible);

    // Column header labels
    juce::Label perfHdrTg   { {}, "#" };
    juce::Label perfHdrOnOff{ {}, "TG" };
    juce::Label perfHdrPrst { {}, "Preset" };
    juce::Label perfHdrChan { {}, "Chan" };
    juce::Label perfHdrLow  { {}, "Low" };
    juce::Label perfHdrHigh { {}, "High" };
    juce::Label perfHdrDet  { {}, "Det" };
    juce::Label perfHdrShft { {}, "Shift" };
    juce::Label perfHdrVol  { {}, "Vol" };
    juce::Label perfHdrOut  { {}, "Out" };
    juce::Label perfHdrDamp { {}, "Damp" };

    // Per-TG controls (indexed 0-7 = TG1-TG8)
    juce::Label     perfTgNum[8];
    juce::ComboBox  perfTgOnOff[8];
    juce::ComboBox  perfTgPreset[8];
    juce::ComboBox  perfTgRxCh[8];
    juce::Slider    perfTgNoteLow[8];
    juce::Slider    perfTgNoteHigh[8];
    juce::Slider    perfTgDetune[8];
    juce::Slider    perfTgShift[8];
    juce::Slider    perfTgVol[8];
    juce::ComboBox  perfTgOut[8];
    juce::ComboBox  perfTgDamp[8];
    juce::Label     perfStatus;

    // Left Panel inline performance section (independent instances of all TG rows)
    struct LpPerfSection : public juce::Component
    {
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xFF1E1E1E)); }
    };
    LpPerfSection lpPerfSection;
    juce::Label lpHdrTg   { {}, "#" };
    juce::Label lpHdrOnOff{ {}, "TG" };
    juce::Label lpHdrPrst { {}, "Preset" };
    juce::Label lpHdrChan { {}, "Chan" };
    juce::Label lpHdrLow  { {}, "Low" };
    juce::Label lpHdrHigh { {}, "High" };
    juce::Label lpHdrDet  { {}, "Det" };
    juce::Label lpHdrShft { {}, "Shift" };
    juce::Label lpHdrVol  { {}, "Vol" };
    juce::Label lpHdrOut  { {}, "Out" };
    juce::Label lpHdrDamp { {}, "Damp" };
    juce::Label    lpTgNum[8];
    juce::ComboBox lpTgOnOff[8];
    juce::ComboBox lpTgPreset[8];
    juce::ComboBox lpTgRxCh[8];
    juce::Slider   lpTgNoteLow[8];
    juce::Slider   lpTgNoteHigh[8];
    juce::Slider   lpTgDetune[8];
    juce::Slider   lpTgShift[8];
    juce::Slider   lpTgVol[8];
    juce::ComboBox lpTgOut[8];
    juce::ComboBox lpTgDamp[8];

    // ── Left Panel inline Preset Browser ──────────────────────────────────────
    // Per-TG browser state: saved/restored when the user switches TG buttons.
    struct LpBrowserState
    {
        juce::String filterText;
        int  ratingId    { 1 };   // ComboBox selectedId (1=Any)
        int  currentPage { 1 };
        int  totalRows   { 0 };
        juce::Array<storage::PresetRow> currentRows;
    };
    LpBrowserState lpBrowserState[8];
    int lpCurrentPage { 1 };   // mirrors lpBrowserState[selectedTg].currentPage

    // ListBoxModel for the LP preset list (one shared instance)
    class LpPresetModel : public juce::ListBoxModel
    {
    public:
        explicit LpPresetModel(MainComponent& o) : owner(o) {}
        int        getNumRows() override;
        void       paintListBoxItem(int row, juce::Graphics& g,
                                    int width, int height, bool selected) override;
        void       listBoxItemClicked(int row, const juce::MouseEvent&) override;
    private:
        MainComponent& owner;
    };
    LpPresetModel lpPresetModel { *this };

    // Column-header bar above the LP preset list
    class LpPresetHeader : public juce::Component
    {
    public:
        explicit LpPresetHeader(MainComponent& o) : owner(o) {}
        void paint(juce::Graphics& g) override;
    private:
        MainComponent& owner;
    };
    LpPresetHeader lpPresetHeader { *this };

    // Container for all browser controls (needs paint() to be rendered by JUCE)
    struct LpBrowserSection : public juce::Component
    {
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xFF181818)); }
    };
    LpBrowserSection lpBrowserSection;

    // Semi-transparent overlay covering the browser when the selected TG is Off
    struct LpBrowserOverlay : public juce::Component
    {
        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colours::black.withAlpha(0.55f));
            g.setColour(juce::Colours::grey);
            g.setFont(juce::Font(14.0f, juce::Font::bold));
            g.drawText("TG is Off", getLocalBounds(), juce::Justification::centred);
        }
    };
    LpBrowserOverlay lpBrowserOverlay;

    // Browser controls (one shared set, state swapped on TG switch)
    juce::Label      lpFilterLabel         { {}, "Filter:" };
    juce::TextEditor lpFilterEdit;
    juce::Label      lpRatingLabel         { {}, "Rating:" };
    juce::ComboBox   lpRatingFilterCombo;
    juce::ListBox    lpPresetList          { "LP Presets", nullptr };
    juce::Label      lpBrowserStatusLabel;
    juce::TextButton lpFirstPage           { "First" };
    juce::TextButton lpPrevPage            { "Prev" };
    juce::TextButton lpNextPage            { "Next" };
    juce::TextButton lpLastPage            { "Last" };

    // Bank panel (lpBankList shares bankModel with the Preset Browser tab's bankList)
    juce::ListBox    lpBankList            { "LP Bank", nullptr };
    juce::Label      lpBankHeader          { {}, "" };
    juce::TextButton lpInitBankButton      { "Clear" };
    juce::TextButton lpRandomizeBankButton { "Random" };
    juce::TextButton lpSendBankButton      { "Send" };
    juce::Label      lpBankNote            { {}, "NOTE: change # of patches to send in Settings" };

    // LP browser methods
    void lpRefreshPresets();
    void lpPresetItemClicked(int row);
    void lpSaveBrowserState();
    void lpRestoreBrowserState(int tg0based);
    void lpUpdateOverlay();

    // Front Panel tab — maps to TX802-Panel-Right.png
    // See PanelLayout::Right for pixel positions of physical buttons.
    FrontPanelTab frontPanelTab;
    LeftPanelTab  leftPanelTab;
    LcdDisplay    lcdDisplay;
    void setLcdLine(int line0or1, const juce::String& text);
    juce::String  buildLinkLine() const;       // LCD line 1: I0X (On) or <-- (Off) per TG; TG1 always I01
    juce::String  buildVoiceSelectLine() const; // LCD line 0: VOICE SELECT ... Rch= n for selected TG
    void          updateLcdFromConfig(); // line 0: blank (reserved); line 1: buildLinkLine()
    void deactivateModeButtons();
    // Mode select buttons (radio: stays dark until another is pressed)
    PanelButton fpPerformSelect, fpVoiceSelect, fpSystemSetup, fpUtility;
    PanelButton fpPerformEdit, fpVoiceEditI, fpVoiceEditII, fpStore;
    PanelButton* fpModeGroup[8]; // radio group — pointers to the 8 mode buttons above
    // Momentary buttons (dark only while held, physical "-" key = PanelLayout::Right::minus)
    PanelButton fpYes, fpNo, fpInt, fpCrt, fpEnter, fpDash;
    juce::OwnedArray<PanelButton> fpNumButtons; // 0..9
    // TG1-TG8 momentary hit-rect buttons on the Left Panel tab.
    // Clicking toggles the TG On/Off state and sends the corresponding SysEx.
    juce::OwnedArray<PanelButton> lpTgButtons;
    // 0-based index of the currently selected TG (-1 = none selected).
    // Set when the user clicks a TG button; used by the sections below the panel image.
    int selectedTg { -1 };
    // fpTgButtons: TG1–TG8 software references (wired but not shown on panel image; lpTgButtons handles the Left Panel hit-rects)
    juce::OwnedArray<juce::TextButton> fpTgButtons;
    // Software-only macro buttons (no physical panel equivalent)
    juce::TextButton fpReset    { "RESET" };
    juce::TextButton fpLower    { "lowercase" };
    juce::TextButton fpUpper    { "UPPERCASE" };
    juce::TextButton fpPrtctOff { "PRTCT OFF" };
    juce::TextButton fpPrtctOn  { "PRTCT ON" };
    juce::TextButton fpPos1     { "POS1" };
    juce::TextButton fpPlayNotes { "Play Notes" };
    juce::Label frontStatus;

    // Macro button strip — displayed below the Right Panel image, mirroring lpPerfSection style/position on the Left Panel.
    struct RpMacroStrip : public juce::Component
    {
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xFF1E1E1E)); }
    };
    RpMacroStrip rpMacroStrip;

    // Settings section — displayed below the Right Panel image in the Right Panel tab.
    // Controls are children of this container; the Settings tab in the tab bar is now empty.
    struct RpSettingsSection : public juce::Component
    {
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xFF1E1E1E)); }
    };
    RpSettingsSection rpSettingsSection;

    // Settings tab (MIDI + actions)
    juce::Component settingsTab;
    // Section headers
    juce::Label settHdrMidi     { {}, "MIDI Ports" };
    juce::Label settHdrDevice   { {}, "Device Control" };
    juce::Label settHdrSysex    { {}, "SysEx Transfer Settings" };
    // MIDI port labels and combos
    juce::Label settLblInput    { {}, "MIDI Input:" };
    juce::Label settLblOutput   { {}, "MIDI Output:" };
    juce::ComboBox midiOutputCombo;
    juce::ComboBox midiInputCombo;
    juce::TextButton refreshMidiButton { "Refresh MIDI" };
    juce::ToggleButton forwardingToggle { "Enable MIDI Forwarding" };
    // Status message box
    juce::TextEditor midiStatusBox;
    // Device control
    juce::TextButton rebootButton { "Reboot TX802" };
    juce::TextButton prepareBtn { "Prepare Device" };
    juce::TextButton playTest { "Play Test Notes" };
    juce::TextButton panicBtn { "All Notes Off" };
    // SysEx transfer settings
    juce::Label devIdLabel { {}, "Device ID:" };
    juce::Slider devIdSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label chunkLabel { {}, "Chunk size (bytes):" };
    juce::Slider chunkSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label delayLabel { {}, "Inter-chunk delay (ms):" };
    juce::Slider delaySlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label patchesLabel { {}, "Patches to send:" };
    juce::ComboBox patchesCombo;
    std::unique_ptr<midi::MidiSender> midiSender;
    std::unique_ptr<midi::MidiThru> midiThru;

    // Background thread for startup sequence (avoids blocking message thread)
    class StartupThread : public juce::Thread
    {
    public:
        StartupThread(midi::MidiSender& s, int devId,
                      std::function<void(int, bool)> ledCb = nullptr,
                      std::function<void(int)>       lcdCb = nullptr)
            : Thread("TX802 Startup"), sender(s), deviceId(devId),
              ledCallback(std::move(ledCb)), lcdCallback(std::move(lcdCb)) {}
        void run() override;
    private:
        midi::MidiSender& sender;
        int deviceId;
        std::function<void(int, bool)> ledCallback;
        // LCD lifecycle callback — called from the background thread; must use callAsync internally.
        // Stage 0: RESET just sent       → show TX802 boot splash text
        // Stage 1: WAIT=3 expired        → device is up; show "Preparing device, wait..."
        // Stage 2: full sequence done    → call updateLcdFromConfig() for live data
        std::function<void(int)> lcdCallback;
    };
    std::unique_ptr<StartupThread> startupThread;

    // Background thread for bank send sequence (avoids blocking message thread)
    class BankSendThread : public juce::Thread
    {
    public:
        BankSendThread(midi::MidiSender& s, juce::uint8 devId,
                       juce::MemoryBlock bankData, bool partial, int pCount,
                       int chunkBytes, int interChunkMs,
                       std::function<void(bool, int)> onDone)
            : juce::Thread("TX802 Bank Send"), sender(s), deviceId(devId),
              bank(std::move(bankData)), isPartial(partial), patchCount(pCount),
              sysexChunkBytes(chunkBytes), sysexInterChunkMs(interChunkMs),
              doneCallback(std::move(onDone)) {}
        void run() override;
    private:
        midi::MidiSender& sender;
        juce::uint8 deviceId;
        juce::MemoryBlock bank;
        bool isPartial;
        int patchCount;
        int sysexChunkBytes;
        int sysexInterChunkMs;
        std::function<void(bool, int)> doneCallback;
    };
    std::unique_ptr<BankSendThread> bankSendThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
