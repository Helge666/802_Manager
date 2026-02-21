#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "midi/MidiSender.h"
#include "midi/MidiThru.h"
#include "storage/PresetsDb.h"

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      public juce::ListBoxModel
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ListBoxModel (Preset Browser placeholder)
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    juce::Component* refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;

private:
    // Small header component for the preset list
    class PresetHeader : public juce::Component
    {
    public:
        explicit PresetHeader(MainComponent& ownerRef) : owner(ownerRef) {}
        void paint(juce::Graphics& g) override;
    private:
        MainComponent& owner;
    };

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

    void openDx7Bank();
    void openSingleVoice();
    void setStatus(const juce::String& text);
    void rebuildMidiOutputs();
    void sendReboot();
    void refreshPresets();
    void changeRatingForRow(int rowIndex, int delta);
    // Drag from preset list
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void beginDragFromPresetRow(int rowIndex);
    // Bank helpers
    void setBankSlotFromPreset(int slotIndex, int presetId, const juce::String& name);
    void moveBankSlot(int fromIndex, int toIndex);
    void initBank();
    void randomizeBank();
    void sendBankToDevice();

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    // Preset Browser tab
    juce::Component presetBrowserTab;
    juce::TextButton openBankButton { "Open DX7 Bank (.syx)" };
    juce::TextButton openVoiceButton { "Open Single Voice (.syx)" };
    juce::Label filterLabel { {}, "Filter:" };
    juce::TextEditor filterEdit;
    juce::Label ratingLabel { {}, "Rating:" };
    juce::ComboBox ratingFilterCombo;
    juce::TextButton selectDbButton { "Select DB File" };
    juce::Label dbPathLabel;
    juce::ListBox presetList { "Presets", this };
    PresetHeader presetHeader { *this };
    BankModel bankModel;
    juce::ListBox bankList { "Bank", &bankModel };
    juce::Label bankHeader { {}, "" };
    juce::TextButton initBankButton { "Clear" };
    juce::TextButton randomizeBankButton { "Random" };
    juce::TextButton sendBankButton { "Send" };
    juce::Label statusLabel;
    std::unique_ptr<storage::PresetsDb> presetsDb;
    std::unique_ptr<juce::FileChooser> dbFileChooser;
    juce::TextButton firstPage { "First" };
    juce::TextButton prevPage  { "Prev" };
    juce::TextButton nextPage  { "Next" };
    juce::TextButton lastPage  { "Last" };
    int currentPage { 1 };
    int totalRows { 0 };
    int pageSize { 100 };

    // Preset Browser data and column layout
    juce::Array<storage::PresetRow> currentRows;
    int colIdWidth { 60 };
    int colNameWidth { 110 };
    int colCategoryWidth { 120 };
    int colCommentsWidth { 280 };
    int colRatingWidth { 36 };
    bool showCommentsColumn { true };
    float avgCharWidthPx { 8.0f }; // measured at runtime using JUCE Font
    int visibleSlots { 8 };
    juce::Label bankNote { {}, "NOTE: change # of patches to send in Settings" };
    juce::Array<int> bankSlotIds; // visibleSlots entries, 0 = empty
    int dragStartRow { -1 };
    bool dragging { false };

    // Performance Editor tab
    juce::Component performanceEditorTab;
    void sendPerfParam(int tg1to8, const juce::String& paramName, int userValue);
    void updatePerfControlsFromConfig();
    void refreshPerfPresetDropdowns();

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

    // Front Panel tab — maps to TX802-Panel-Right.png
    // See PanelLayout::Right for pixel positions of physical buttons.
    FrontPanelTab frontPanelTab;
    LeftPanelTab  leftPanelTab;
    LcdDisplay    lcdDisplay;
    void setLcdLine(int line0or1, const juce::String& text);
    juce::String  buildLinkLine() const; // 40-char line 2: TG On/Off state
    void          updateLcdFromConfig(); // line 1 blank, line 2 = buildLinkLine()
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
    // TODO: TG1-TG8 buttons belong to TX802-Panel-Left.png — implement with left panel later
    juce::OwnedArray<juce::TextButton> fpTgButtons;
    // TODO: RESET — no dedicated physical button; implement as software action later
    juce::TextButton fpReset { "RESET" };
    // TODO: lowercase / UPPERCASE — text-entry mode helpers, no physical buttons; implement later
    juce::TextButton fpLower { "lowercase" };
    juce::TextButton fpUpper { "UPPERCASE" };
    // TODO: PRTCT OFF / PRTCT ON / POS1 — software macros; implement later
    juce::TextButton fpPrtctOff { "PRTCT OFF" };
    juce::TextButton fpPrtctOn  { "PRTCT ON" };
    juce::TextButton fpPos1     { "POS1" };
    // TODO: Play Notes — software test utility; implement later
    juce::TextButton fpPlayNotes { "Play Notes" };
    juce::Label frontStatus;

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
    juce::StringArray presetNames;
    std::unique_ptr<juce::FileChooser> fileChooser;
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
        std::function<void(int)>       lcdCallback; // 0=reset sent, 1=preparing, 2=ready
    };
    std::unique_ptr<StartupThread> startupThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
