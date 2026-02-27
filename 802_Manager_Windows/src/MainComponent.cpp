#include "MainComponent.h"
#include "core/Dx7Utils.h"
#include "midi/MidiSender.h"
#include "midi/Tx802HighLevel.h"
#include "midi/Config.h"
#include "PanelLayout.h"
#include <BinaryData.h>

using core::Dx7Utils;

// LCD text constants — shown during and after device reset
static const juce::String kLcdReset0   = "*****        YAMAHA  TX802         *****";
static const juce::String kLcdReset1   = "*****      FM Tone Generator       *****";
static const juce::String kLcdPrepare0 = "Preparing device, wait...";
static const juce::String kLcdPrepare1 = "";

// Hard-coded Init Voice (patch 16608) as full single-voice SysEx.
// Matches Python's DEFAULT_INIT_VOICE_155 packed to 128 bytes at runtime.
static juce::MemoryBlock getInitVoice128()
{
    static const juce::uint8 kInit16608Syx[] = {
        0xF0,0x43,0x00,0x00,0x01,0x1B,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x07,0x63,0x63,0x63,0x63,0x63,
        0x63,0x63,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x07,
        0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x01,0x00,0x07,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x07,0x63,0x63,0x63,0x63,0x63,0x63,
        0x63,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x07,0x63,
        0x63,0x63,0x63,0x63,0x63,0x63,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x63,
        0x00,0x01,0x00,0x07,0x63,0x63,0x63,0x63,0x32,0x32,0x32,0x32,0x00,0x02,0x00,0x23,
        0x00,0x00,0x00,0x01,0x00,0x03,0x18,0x49,0x4E,0x49,0x54,0x20,0x56,0x4F,0x49,0x43,
        0x45,0x50,0xF7
    };
    juce::MemoryBlock syxBlock(kInit16608Syx, sizeof(kInit16608Syx));
    juce::String msg; juce::MemoryBlock v155;
    if (core::Dx7Utils::verifySingleVoiceSysex(syxBlock, msg, v155))
    {
        auto v128 = core::Dx7Utils::packSingleToBankVoice(v155);
        if ((int) v128.getSize() == 128) return v128;
    }
    juce::MemoryBlock zeros(128, true);
    return zeros;
}

MainComponent::~MainComponent()
{
    for (auto* b : { &lpFirstPage, &lpPrevPage, &lpNextPage, &lpLastPage })
        b->setLookAndFeel(nullptr);
    if (bankSendThread)
    {
        bankSendThread->stopThread(8000);
        bankSendThread.reset();
    }
    if (startupThread)
    {
        startupThread->stopThread(8000);
        startupThread.reset();
    }
}

void MainComponent::StartupThread::run()
{
    if (lcdCallback) lcdCallback(0); // RESET sent — show startup text
    midi::Tx802HighLevel::sendStartupSequence(sender, (juce::uint8) deviceId,
        [this] {
            if (lcdCallback) lcdCallback(1); // device booted — show "preparing" text
        });
    if (threadShouldExit()) return;

    midi::ConfigState cfg;
    midi::Config::load(cfg);

    if (! cfg.hasPerformanceParams)
    {
        for (int i = 0; i < 8; ++i)
        {
            auto& t = cfg.tg[i];
            t.tgOnOff   = (i == 0) ? "On" : "Off";
            t.preset    = midi::vnumToPreset(i + 1);
            t.rxch      = "1";
            t.noteLow   = "C-2";
            t.noteHigh  = "G8";
            t.detune    = "0";
            t.noteShift = "0";
            t.outVol    = "90";
            t.pan       = "Center";
            t.fDamp     = "Off";
        }
        cfg.hasPerformanceParams = true;
        midi::Config::save(cfg);
    }

    midi::Tx802HighLevel::restorePerformanceParams(sender, (juce::uint8) deviceId, cfg, ledCallback);
    if (lcdCallback) lcdCallback(2); // startup complete — show ready text
}

void MainComponent::BankSendThread::run()
{
    // Disable memory protection (SYSTEM_SETUP → TG8 → NO, 100 ms between each)
    midi::Tx802HighLevel::sendMacroByName(sender, "PRTCT_OFF", deviceId);
    juce::Thread::sleep(100); // brief pause before data transfer

    // Build send buffer: full 4104-byte bank for full send, truncated for partial
    const size_t sendSize = isPartial
        ? (size_t)(6 + patchCount * 128 + 4)
        : bank.getSize();
    juce::MemoryBlock toSend(bank.getData(), sendSize);

    if (! sender.sendSysexPaced(toSend, sysexChunkBytes, sysexInterChunkMs))
        sender.sendSysex(toSend);

    // Post-send sequence matching Python send_bank() exactly.
    // For partial sends the device is left in an incomplete SysEx receive state;
    // a dedicated VOICE_SELECT is required to exit that state before the
    // normal refresh sequence (VOICE_SELECT → PLUS_ONE → MINUS_ONE) can run.
    if (isPartial)
    {
        juce::Thread::sleep(500);
        midi::Tx802HighLevel::sendButtonByName(sender, "VOICE_SELECT", deviceId); // exit SysEx receive state
        juce::Thread::sleep(1000);
    }
    else
    {
        juce::Thread::sleep(1000);
    }

    // Refresh voice display
    midi::Tx802HighLevel::sendButtonByName(sender, "VOICE_SELECT", deviceId);
    juce::Thread::sleep(midi::Tx802HighLevel::kButtonDelayMs);
    midi::Tx802HighLevel::sendButtonByName(sender, "PLUS_ONE", deviceId);
    juce::Thread::sleep(midi::Tx802HighLevel::kButtonDelayMs);
    midi::Tx802HighLevel::sendButtonByName(sender, "MINUS_ONE", deviceId);

    if (doneCallback)
        doneCallback(true, patchCount);
}

MainComponent::MainComponent()
{
    midi::StartupLog::write("=== MainComponent CTOR BEGIN ===");
    addAndMakeVisible(tabs);

    midi::StartupLog::write("CTOR: Bank + DB setup");
    // ── Bank model + preset database ──
    rpSettingsSection.addAndMakeVisible(selectDbButton);
    rpSettingsSection.addAndMakeVisible(dbPathLabel);

    // Read patchesToSend and preset bank names from config
    midi::ConfigState cfgSlots; midi::Config::load(cfgSlots);
    {
        int p = cfgSlots.patchesToSend;
        if (p == 1 || p == 8 || p == 16 || p == 32) visibleSlots = p; else visibleSlots = 8;
    }
    bankSlotIds.resize(visibleSlots);
    for (int i = 0; i < visibleSlots; ++i) bankSlotIds.set(i, 0);
    bankModel.initOwner(this, visibleSlots);
    // Pre-populate bank slot names from last-sent preset bank
    for (int i = 0; i < visibleSlots && i < cfgSlots.presetBankNames.size(); ++i)
    {
        const auto& name = cfgSlots.presetBankNames[i];
        if (name.isNotEmpty() && name != "empty")
            bankModel.setSlotName(i, name);
    }

    // Load startup_bank.syx for later device restore (sent after startup sequence completes)
    {
        auto bankFile = midi::Config::getConfigFile().getParentDirectory().getChildFile("startup_bank.syx");
        if (bankFile.existsAsFile())
            bankFile.loadFileAsData(startupBankData);
    }

    midi::StartupLog::write("CTOR: Opening DB");
    midi::ConfigState cfgInitial; midi::Config::load(cfgInitial);
    juce::File dbFile = cfgInitial.dbPath.isNotEmpty() ? juce::File(cfgInitial.dbPath)
                                                       : juce::File::getCurrentWorkingDirectory().getChildFile("config/dx_preset_library.sqlite3");
    presetsDb = std::make_unique<storage::PresetsDb>(dbFile);
    presetsDb->open();
    dbPathLabel.setText(dbFile.getFullPathName(), juce::dontSendNotification);

    selectDbButton.onClick = [this]
    {
        dbFileChooser.reset(new juce::FileChooser("Select preset DB file", juce::File(), "*.sqlite3;*.db;*"));
        dbFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [this](const juce::FileChooser& ch)
        {
            auto f = ch.getResult();
            if (f.existsAsFile())
            {
                presetsDb.reset(new storage::PresetsDb(f));
                if (presetsDb->open())
                {
                    dbPathLabel.setText(f.getFullPathName(), juce::dontSendNotification);
                    midi::ConfigState cfg; midi::Config::load(cfg); cfg.dbPath = f.getFullPathName(); midi::Config::save(cfg);
                    lpRefreshPresets();
                }
                else
                    midiStatusBox.setText("Failed to open DB");
            }
        });
    };

    midi::StartupLog::write("CTOR: Front Panel tab setup");
    // ── Right Panel tab — TX802-Panel-Right.png as background ──
    frontPanelTab.image = juce::ImageCache::getFromMemory(
        BinaryData::TX802PanelRight_png, BinaryData::TX802PanelRight_pngSize);

    // ── Left Panel tab — TX802-Panel-Left.png as background ──
    leftPanelTab.image = juce::ImageCache::getFromMemory(
        BinaryData::TX802PanelLeft_png, BinaryData::TX802PanelLeft_pngSize);

    // TG LED overlays — LED-On.png placed over each TG button, hidden by default
    {
        auto ledImg = juce::ImageCache::getFromMemory(BinaryData::LEDOn_png, BinaryData::LEDOn_pngSize);
        for (int i = 0; i < 8; ++i)
        {
            tgLedOverlay[i].setImage(ledImg, juce::RectanglePlacement::stretchToFit);
            tgLedOverlay[i].setVisible(false);
            tgLedOverlay[i].setInterceptsMouseClicks(false, false);
            leftPanelTab.addChildComponent(tgLedOverlay[i]);
        }
    }
    // TG1-TG8 hit-rect buttons on the Left Panel — momentary darken + toggle On/Off
    for (int i = 0; i < 8; ++i)
    {
        auto* btn = lpTgButtons.add(new PanelButton());
        btn->momentary = true;
        leftPanelTab.addAndMakeVisible(btn);
        btn->onClick = [this, i]
        {
            if (startupInProgress) return;
            selectTg(i);

            midi::ConfigState cfg; midi::Config::load(cfg);
            cfg.lastSelectedTg = i;
            midi::Config::save(cfg);

            if (! midiSender || ! midiSender->isOpen()) return;
            const bool currentlyOn = midi::tgOnFromString(cfg.tg[i].tgOnOff);
            sendPerfParam(i + 1, "TG", currentlyOn ? 0 : 1);
            lpTgOnOff[i].setSelectedId(currentlyOn ? 1 : 2, juce::dontSendNotification);
            lpUpdateOverlay();
        };
    }
    // LCD text overlay on the display area (non-interactive, drawn on top of the green LCD region)
    // Lines are left blank at startup; updated as device state changes (see lcdCallback in StartupThread).
    lcdDisplay.setInterceptsMouseClicks(false, false);
    leftPanelTab.addAndMakeVisible(lcdDisplay);

    // ── Left Panel inline performance section ──
    // All eight TG rows as independent component instances; hidden until first TG click.
    {
        for (auto* lbl : { /*&lpHdrTg, &lpHdrOnOff, &lpHdrPrst,*/ &lpHdrChan,
                           &lpHdrLow, &lpHdrHigh, &lpHdrDet, &lpHdrShft,
                           &lpHdrVol, &lpHdrOut, &lpHdrDamp })
        {
            lbl->setFont(juce::Font(12.0f, juce::Font::bold));
            lbl->setJustificationType(juce::Justification::centred);
            lbl->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
            lpPerfSection.addAndMakeVisible(lbl);
        }

        midi::ConfigState lpCfg;
        midi::Config::load(lpCfg);

        for (int i = 0; i < 8; ++i)
        {
            const int tg = i + 1;
            const auto& t = lpCfg.hasPerformanceParams ? lpCfg.tg[i] : midi::TgState();

            lpTgNum[i].setText(juce::String(tg), juce::dontSendNotification);
            lpTgNum[i].setJustificationType(juce::Justification::centred);
            lpTgNum[i].setFont(juce::Font(32.0f, juce::Font::bold | juce::Font::italic));
            lpTgNum[i].setColour(juce::Label::textColourId, juce::Colour(0xFF888888));
            lpPerfSection.addChildComponent(lpTgNum[i]);

            // TG On/Off handled by the panel image buttons — not in strip
            /*
            lpTgOnOff[i].addItem("Off", 1);
            lpTgOnOff[i].addItem("On",  2);
            lpTgOnOff[i].setSelectedId(midi::tgOnFromString(t.tgOnOff) ? 2 : 1, juce::dontSendNotification);
            lpPerfSection.addChildComponent(lpTgOnOff[i]);
            lpTgOnOff[i].onChange = [this, tg, i]{ sendPerfParam(tg, "TG", lpTgOnOff[i].getSelectedId() == 2 ? 1 : 0); };
            */

            // Preset shown on LCD line 0 — not in strip
            /*
            // Items populated by refreshPerfPresetDropdowns() (already called earlier in ctor)
            lpTgPreset[i].setSelectedId(juce::jlimit(1, 32, midi::presetToVnum(t.preset)), juce::dontSendNotification);
            lpPerfSection.addChildComponent(lpTgPreset[i]);
            lpTgPreset[i].onChange = [this, tg, i]{ sendPerfParam(tg, "PRESET", lpTgPreset[i].getSelectedId()); };
            */

            for (int c = 1; c <= 16; ++c) lpTgRxCh[i].addItem(juce::String(c), c);
            lpTgRxCh[i].addItem("Omni", 17);
            lpTgRxCh[i].setSelectedId(juce::jlimit(1, 17, midi::rxchFromString(t.rxch)), juce::dontSendNotification);
            lpPerfSection.addChildComponent(lpTgRxCh[i]);
            lpTgRxCh[i].onChange = [this, tg, i]{ sendPerfParam(tg, "RXCH", lpTgRxCh[i].getSelectedId()); };

            // Low / High — ComboBox with MIDI note names (C-2 … G8, Yamaha convention)
            for (int n = 0; n < 128; ++n)
                lpTgNoteLow[i].addItem(midi::midiToNoteName(n), n + 1);
            lpTgNoteLow[i].setSelectedId(midi::noteNameToMidi(t.noteLow) + 1, juce::dontSendNotification);
            lpPerfSection.addChildComponent(lpTgNoteLow[i]);
            lpTgNoteLow[i].onChange = [this, tg, i]{ sendPerfParam(tg, "NOTELOW", lpTgNoteLow[i].getSelectedId() - 1); };

            for (int n = 0; n < 128; ++n)
                lpTgNoteHigh[i].addItem(midi::midiToNoteName(n), n + 1);
            lpTgNoteHigh[i].setSelectedId(midi::noteNameToMidi(t.noteHigh) + 1, juce::dontSendNotification);
            lpPerfSection.addChildComponent(lpTgNoteHigh[i]);
            lpTgNoteHigh[i].onChange = [this, tg, i]{ sendPerfParam(tg, "NOTEHIGH", lpTgNoteHigh[i].getSelectedId() - 1); };

            // Det / Shft / Vol — linear bar sliders (value drawn inside bar)
            // Explicit colours for visibility against dark strip background (0xFF1E1E1E).
            // TextBoxLeft (read-only): for bar-style JUCE positions the textbox over the full
            // slider bounds with transparent background — value text appears overlaid on the bar.
            auto applyBarColours = [](juce::Slider& s) {
                s.setColour(juce::Slider::trackColourId,             juce::Colour(0xFF3A6EA5));
                s.setColour(juce::Slider::backgroundColourId,        juce::Colour(0xFF2A2A2A));
                s.setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white);
                s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
                // textBox fills full bounds for bar-style → its outline becomes the widget border
                s.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0xFF606060));
            };

            lpTgDetune[i].setRange(-7, 7, 1);
            lpTgDetune[i].setValue(t.detune.getIntValue(), juce::dontSendNotification);
            lpTgDetune[i].setSliderStyle(juce::Slider::LinearBar);
            lpTgDetune[i].setTextBoxStyle(juce::Slider::TextBoxLeft, true, 0, 0);
            applyBarColours(lpTgDetune[i]);
            lpPerfSection.addChildComponent(lpTgDetune[i]);
            lpTgDetune[i].onValueChange = [this, tg, i]{ sendPerfParam(tg, "DETUNE", (int) lpTgDetune[i].getValue()); };

            lpTgShift[i].setRange(-24, 24, 1);
            lpTgShift[i].setValue(t.noteShift.getIntValue(), juce::dontSendNotification);
            lpTgShift[i].setSliderStyle(juce::Slider::LinearBar);
            lpTgShift[i].setTextBoxStyle(juce::Slider::TextBoxLeft, true, 0, 0);
            applyBarColours(lpTgShift[i]);
            lpPerfSection.addChildComponent(lpTgShift[i]);
            lpTgShift[i].onValueChange = [this, tg, i]{ sendPerfParam(tg, "NOTESHIFT", (int) lpTgShift[i].getValue()); };

            lpTgVol[i].setRange(0, 99, 1);
            lpTgVol[i].setValue(t.outVol.getIntValue(), juce::dontSendNotification);
            lpTgVol[i].setSliderStyle(juce::Slider::LinearBar);
            lpTgVol[i].setTextBoxStyle(juce::Slider::TextBoxLeft, true, 0, 0);
            applyBarColours(lpTgVol[i]);
            lpPerfSection.addChildComponent(lpTgVol[i]);
            lpTgVol[i].onValueChange = [this, tg, i]{ sendPerfParam(tg, "OUTVOL", (int) lpTgVol[i].getValue()); };

            // Out — 3-position snap slider (L / C / R). "Off" removed.
            // If "Off" needs to return, add it as position 0 and shift L/C/R up by 1.
            lpTgOut[i].setRange(0, 2, 1);
            lpTgOut[i].setSliderStyle(juce::Slider::LinearHorizontal);
            lpTgOut[i].setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
            // backgroundColourId = full track; trackColourId = filled portion (left of thumb).
            // Setting both to the same grey makes the groove uniformly visible on both sides.
            lpTgOut[i].setColour(juce::Slider::backgroundColourId, juce::Colour(0xFF606060));
            lpTgOut[i].setColour(juce::Slider::trackColourId,      juce::Colour(0xFF606060));
            lpTgOut[i].textFromValueFunction = [](double v) -> juce::String {
                if (v < 0.5) return "L";
                if (v < 1.5) return "C";
                return "R";
            };
            {
                const int outch = midi::panToOutch(t.pan); // 0=Off, 1=L, 2=R, 3=C
                const int sliderPos = (outch == 1) ? 0 : (outch == 2) ? 2 : 1; // Off → C
                lpTgOut[i].setValue(sliderPos, juce::dontSendNotification);
            }
            lpPerfSection.addChildComponent(lpTgOut[i]);
            lpTgOut[i].onValueChange = [this, tg, i]{
                const int panVals[] = {1, 3, 2}; // slider 0/1/2 → device PAN L/C/R
                sendPerfParam(tg, "PAN", panVals[juce::jlimit(0, 2, (int) lpTgOut[i].getValue())]);
            };

            // Damp — latching ToggleButton
            lpTgDamp[i].setButtonText("");
            lpTgDamp[i].setToggleState(midi::fdampFromString(t.fDamp) != 0, juce::dontSendNotification);
            lpPerfSection.addChildComponent(lpTgDamp[i]);
            lpTgDamp[i].onClick = [this, tg, i]{ sendPerfParam(tg, "FDAMP", lpTgDamp[i].getToggleState() ? 1 : 0); };
        }
        for (auto& sep : lpPerfSep)
            lpPerfSection.addAndMakeVisible(sep);
        leftPanelTab.addAndMakeVisible(lpBankStrip);
        leftPanelTab.addAndMakeVisible(lpPerfSection);
    }

    // ── Left Panel inline Preset Browser ──
    {
        // Rating combo — same items as the tab browser
        lpRatingFilterCombo.addItem("Any",     1);
        lpRatingFilterCombo.addItem("Unrated", 2);
        for (int r = 1; r <= 10; ++r) lpRatingFilterCombo.addItem(juce::String(r), 100 + r);
        lpRatingFilterCombo.setText("Any", juce::dontSendNotification);

        // Preset list
        lpPresetList.setModel(&lpPresetModel);
        lpPresetList.setMultipleSelectionEnabled(false);
        lpPresetList.setRowHeight(22);
        lpPresetList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xFF1A1A1A));

        // Bank list — shares bankModel with the Preset Browser tab
        lpBankList.setModel(&bankModel);
        lpBankList.setMultipleSelectionEnabled(false);
        lpBankList.setRowHeight(22);
        lpBankList.setColour(juce::ListBox::backgroundColourId, juce::Colours::black.withAlpha(0.15f));

        lpBankNote.setFont(juce::Font(11.0f));
        lpBankNote.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

        lpFilterEdit.setTextToShowWhenEmpty("Filter...", juce::Colours::grey);

        // Filter/rating callbacks — save state then refresh
        auto lpRefresh = [this]{ lpRefreshPresets(); };
        lpFilterEdit.onTextChange = [this, lpRefresh]{
            if (selectedTg >= 0) lpBrowserState[selectedTg].currentPage = 1;
            lpCurrentPage = 1;
            lpRefresh();
        };
        lpRatingFilterCombo.onChange = [this, lpRefresh]{
            if (selectedTg >= 0) lpBrowserState[selectedTg].currentPage = 1;
            lpCurrentPage = 1;
            lpRefresh();
        };

        // Pagination
        lpFirstPage.onClick = [this]{ lpCurrentPage = 1; lpRefreshPresets(); };
        lpPrevPage.onClick  = [this]{ if (lpCurrentPage > 1) { --lpCurrentPage; lpRefreshPresets(); } };
        lpNextPage.onClick  = [this]{
            if (selectedTg >= 0)
            {
                int maxPage = juce::jmax(1, (lpBrowserState[selectedTg].totalRows + pageSize - 1) / pageSize);
                if (lpCurrentPage < maxPage) { ++lpCurrentPage; lpRefreshPresets(); }
            }
        };
        lpLastPage.onClick = [this]{
            if (selectedTg >= 0)
            {
                int maxPage = juce::jmax(1, (lpBrowserState[selectedTg].totalRows + pageSize - 1) / pageSize);
                lpCurrentPage = maxPage;
                lpRefreshPresets();
            }
        };

        // Bank buttons — delegate to shared action methods
        lpInitBankButton.onClick       = [this]{ initBank(); };
        lpRandomizeBankButton.onClick  = [this]{ randomizeBank(); };
        lpSendBankButton.onClick       = [this]{ sendBankToDevice(); };

        // Auto-send toggle + button
        lpAutoSendButton.onClick = [this]{ sendBankToDevice(); };
        lpAutoSendToggle.onStateChange = [this]{
            const bool on = lpAutoSendToggle.getToggleState();
            lpAutoSendButton.setButtonText(on ? "Auto" : "Send");
            lpAutoSendButton.setEnabled(!on);
        };

        // Add controls to the section container
        lpBrowserSection.addAndMakeVisible(lpFilterEdit);
        lpBrowserSection.addAndMakeVisible(lpRatingFilterCombo);
        for (auto* b : { &lpFirstPage, &lpPrevPage, &lpNextPage, &lpLastPage })
        {
            b->setLookAndFeel(&navButtonLaf);
            lpBrowserSection.addAndMakeVisible(b);
        }
        lpBrowserSection.addAndMakeVisible(lpAutoSendToggle);
        lpBrowserSection.addAndMakeVisible(lpAutoSendButton);
        lpBrowserSection.addAndMakeVisible(lpSep1);
        lpBrowserSection.addAndMakeVisible(lpSep2);
        lpBrowserSection.addAndMakeVisible(lpPresetHeader);
        lpBrowserSection.addAndMakeVisible(lpPresetList);
        lpBrowserSection.addAndMakeVisible(lpBrowserStatusLabel);
        // lpBankHeader, lpBankList, lpInitBankButton, lpSendBankButton, lpBankNote removed from view.
        // lpRandomizeBankButton kept in code (onClick still wired) for future placement.
        leftPanelTab.addAndMakeVisible(lpBrowserSection);
        leftPanelTab.addChildComponent(lpBrowserOverlay);  // shown when TG is Off; covers perf + browser
    }

    // Mode select buttons: non-momentary radio group
    fpModeGroup[0] = &fpPerformSelect;
    fpModeGroup[1] = &fpVoiceSelect;
    fpModeGroup[2] = &fpSystemSetup;
    fpModeGroup[3] = &fpUtility;
    fpModeGroup[4] = &fpPerformEdit;
    fpModeGroup[5] = &fpVoiceEditI;
    fpModeGroup[6] = &fpVoiceEditII;
    fpModeGroup[7] = &fpStore;
    for (auto* b : fpModeGroup) { b->momentary = false; frontPanelTab.addAndMakeVisible(b); }
    // Momentary buttons
    for (auto* b : { &fpYes, &fpNo, &fpInt, &fpCrt, &fpEnter, &fpDash })
        frontPanelTab.addAndMakeVisible(b);
    // Number buttons 0..9
    for (int i = 0; i <= 9; ++i)
        frontPanelTab.addAndMakeVisible(fpNumButtons.add(new PanelButton()));
    frontPanelTab.addAndMakeVisible(frontStatus);
    for (int i = 1; i <= 8; ++i)
        fpTgButtons.add(new juce::TextButton("TG" + juce::String(i)));
    // ── Right Panel macro strip (RESET / PRTCT OFF / PRTCT ON / POS1) ──
    rpMacroStrip.addAndMakeVisible(fpReset);
    rpMacroStrip.addAndMakeVisible(fpPrtctOff);
    rpMacroStrip.addAndMakeVisible(fpPrtctOn);
    rpMacroStrip.addAndMakeVisible(fpPos1);
    frontPanelTab.addAndMakeVisible(rpMacroStrip);

    midi::StartupLog::write("CTOR: Settings tab setup");
    // ── Settings section (hosted in Right Panel tab, below the panel image) ──
    rpSettingsSection.addAndMakeVisible(midiOutputCombo);
    rpSettingsSection.addAndMakeVisible(midiInputCombo);
    settHdrMidi.setFont(juce::Font(15.0f, juce::Font::bold));
    settHdrDevice.setFont(juce::Font(15.0f, juce::Font::bold));
    settHdrSysex.setFont(juce::Font(15.0f, juce::Font::bold));
    rpSettingsSection.addAndMakeVisible(settHdrMidi);
    rpSettingsSection.addAndMakeVisible(settHdrDevice);
    rpSettingsSection.addAndMakeVisible(settHdrSysex);
    rpSettingsSection.addAndMakeVisible(settLblInput);
    rpSettingsSection.addAndMakeVisible(settLblOutput);
    rpSettingsSection.addAndMakeVisible(refreshMidiButton);
    rpSettingsSection.addAndMakeVisible(forwardingToggle);
    midiStatusBox.setMultiLine(false);
    midiStatusBox.setReadOnly(true);
    midiStatusBox.setCaretVisible(false);
    midiStatusBox.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha(0.3f));
    midiStatusBox.setColour(juce::TextEditor::outlineColourId, juce::Colours::grey);
    midiStatusBox.setColour(juce::TextEditor::textColourId, juce::Colours::lightgreen);
    rpSettingsSection.addAndMakeVisible(midiStatusBox);
    rpSettingsSection.addAndMakeVisible(rebootButton);
    rpSettingsSection.addAndMakeVisible(prepareBtn);
    rpSettingsSection.addAndMakeVisible(playTest);
    rpSettingsSection.addAndMakeVisible(panicBtn);
    rpSettingsSection.addAndMakeVisible(devIdLabel);
    rpSettingsSection.addAndMakeVisible(devIdSlider);
    rpSettingsSection.addAndMakeVisible(chunkLabel);
    rpSettingsSection.addAndMakeVisible(chunkSlider);
    rpSettingsSection.addAndMakeVisible(delayLabel);
    rpSettingsSection.addAndMakeVisible(delaySlider);
    rpSettingsSection.addAndMakeVisible(patchesLabel);
    rpSettingsSection.addAndMakeVisible(patchesCombo);
    frontPanelTab.addAndMakeVisible(rpSettingsSection);

    midi::StartupLog::write("CTOR: Wiring callbacks");
    // ── Wiring ──
    refreshMidiButton.onClick = [this] { rebuildMidiOutputs(); rebuildMidiInputs(); };
    rebootButton.onClick = [this] { sendReboot(); };
    playTest.onClick = [this]
    {
        if (! midiSender || ! midiSender->isOpen()) { midiStatusBox.setText("Open a MIDI output first"); return; }
        midi::Tx802HighLevel::playTestNotes(*midiSender);
        midiStatusBox.setText("Played test notes");
    };

    // Front Panel wiring
    auto sendBtn = [this](const juce::String& name)
    {
        if (midiSender && midiSender->isOpen())
            midi::Tx802HighLevel::sendButtonByName(*midiSender, name, 1);
        else
            midiStatusBox.setText("Open a MIDI output first");
    };
    // Radio group helper: deactivates all mode buttons except the one just pressed
    // (the pressed button is already active=true from PanelButton::mouseDown)
    auto deactExcept = [this](PanelButton* keep)
    {
        for (auto* b : fpModeGroup) if (b != keep) b->setActive(false);
    };
    // Mode select buttons (radio)
    fpPerformSelect.onClick = [this, sendBtn, deactExcept]{ deactExcept(&fpPerformSelect); sendBtn("PERFORM_SELECT"); };
    fpVoiceSelect.onClick   = [this, sendBtn, deactExcept]{ deactExcept(&fpVoiceSelect);   sendBtn("VOICE_SELECT"); };
    fpSystemSetup.onClick   = [this, sendBtn, deactExcept]{ deactExcept(&fpSystemSetup);   sendBtn("SYSTEM_SETUP"); };
    fpUtility.onClick       = [this, sendBtn, deactExcept]{ deactExcept(&fpUtility);       sendBtn("UTILITY"); };
    fpPerformEdit.onClick   = [this, sendBtn, deactExcept]{ deactExcept(&fpPerformEdit);   sendBtn("PERFORM_EDIT"); };
    fpVoiceEditI.onClick    = [this, sendBtn, deactExcept]{ deactExcept(&fpVoiceEditI);    sendBtn("VOICE_EDIT_I"); };
    fpVoiceEditII.onClick   = [this, sendBtn, deactExcept]{ deactExcept(&fpVoiceEditII);   sendBtn("VOICE_EDIT_II"); };
    fpStore.onClick         = [this, sendBtn, deactExcept]{ deactExcept(&fpStore);         sendBtn("STORE"); };
    // On returning to Left Panel: clear any depressed mode button and restore VOICE_SELECT
    tabs.onTabChanged = [this](int index)
    {
        if (index != 0) return;
        for (auto* b : fpModeGroup) b->setActive(false);
        if (midiSender && midiSender->isOpen())
        {
            midi::ConfigState cfg; midi::Config::load(cfg);
            midi::Tx802HighLevel::sendButtonByName(*midiSender, "VOICE_SELECT", cfg.deviceId);
        }
    };
    // Momentary buttons
    fpYes.onClick   = [sendBtn]{ sendBtn("YES"); };
    fpNo.onClick    = [sendBtn]{ sendBtn("NO"); };
    fpInt.onClick   = [sendBtn]{ sendBtn("INT"); };
    fpCrt.onClick   = [sendBtn]{ sendBtn("CRT"); };
    fpEnter.onClick = [sendBtn]{ sendBtn("ENTER"); };
    fpDash.onClick  = [sendBtn]{ sendBtn("DASH"); };
    // Number buttons
    for (int i = 0; i <= 9; ++i)
    {
        fpNumButtons[i]->onClick = [this, sendBtn, i]
        {
            sendBtn(juce::String(i));
            frontStatus.setText("Sent " + juce::String(i), juce::dontSendNotification);
        };
    }
    for (int i = 0; i < fpTgButtons.size(); ++i)
    {
        fpTgButtons[i]->onClick = [this, sendBtn, i]
        {
            sendBtn("TG" + juce::String(i + 1));
            frontStatus.setText("Sent TG" + juce::String(i + 1), juce::dontSendNotification);
        };
    }
    fpReset.onClick = [sendBtn]{ sendBtn("RESET"); };
    fpLower.onClick  = [sendBtn]{ sendBtn("LOWERCASE"); };
    fpUpper.onClick  = [sendBtn]{ sendBtn("UPPERCASE"); };
    fpPrtctOff.onClick = [this, deactExcept]
    {
        if (midiSender && midiSender->isOpen())
            midi::Tx802HighLevel::sendMacroByName(*midiSender, "PRTCT_OFF", 1);
        deactExcept(&fpVoiceSelect);
        fpVoiceSelect.setActive(true);
        frontStatus.setText("Sent PRTCT OFF", juce::dontSendNotification);
    };
    fpPrtctOn.onClick = [this, deactExcept]
    {
        if (midiSender && midiSender->isOpen())
            midi::Tx802HighLevel::sendMacroByName(*midiSender, "PRTCT_ON", 1);
        deactExcept(&fpVoiceSelect);
        fpVoiceSelect.setActive(true);
        frontStatus.setText("Sent PRTCT ON", juce::dontSendNotification);
    };
    fpPos1.onClick = [this]
    {
        if (midiSender && midiSender->isOpen())
            midi::Tx802HighLevel::sendMacroByName(*midiSender, "POS1", 1);
        frontStatus.setText("Sent POS1", juce::dontSendNotification);
    };
    fpPlayNotes.onClick = [this]
    {
        if (midiSender && midiSender->isOpen())
            midi::Tx802HighLevel::playTestNotes(*midiSender);
        frontStatus.setText("Played test notes", juce::dontSendNotification);
    };

    midi::StartupLog::write("CTOR: MIDI setup");
    // MIDI setup
    midiSender = std::make_unique<midi::MidiSender>();
    midiStatusBox.setText("Select MIDI In/Out");
    rebuildMidiOutputs();

    // Populate inputs
    rebuildMidiInputs();
    midiThru = std::make_unique<midi::MidiThru>();
    panicBtn.onClick = [this]
    {
        if (! midiSender || ! midiSender->isOpen()) return;
        for (int ch = 0; ch < 16; ++ch)
            midiSender->sendNoteOff(ch, 0);
    };

    // Auto-restore MIDI ports from config
    {
        midi::ConfigState cfg; midi::Config::load(cfg);
        if (cfg.outputPort.isNotEmpty())
        {
            juce::MessageManager::callAsync([this, cfg]
            {
                midiOutputCombo.setText(cfg.outputPort, juce::dontSendNotification);
                const bool ok = midiSender->openByName(cfg.outputPort);
                midiStatusBox.setText(ok ? juce::String("Opened OUT: ") + cfg.outputPort : juce::String("Open OUT failed: ") + cfg.outputPort);
                if (ok)
                {
                    midiStatusBox.setText("Sending startup sequence...");
                    startupThread = std::make_unique<StartupThread>(*midiSender, cfg.deviceId,
                    [this](int tg, bool on) {
                        juce::MessageManager::callAsync([this, tg, on] { setTgLed(tg, on); });
                    },
                    [this](int stage) {
                        juce::MessageManager::callAsync([this, stage] {
                            if      (stage == 0) { setLcdLine(0, kLcdReset0);   setLcdLine(1, kLcdReset1);   }
                            else if (stage == 1) { setLcdLine(0, kLcdPrepare0); setLcdLine(1, kLcdPrepare1); }
                            else {
                                startupInProgress = false;
                                updateLcdFromConfig();
                                if (selectedTg < 0) {
                                    midi::ConfigState cfg2; midi::Config::load(cfg2);
                                    selectTg(cfg2.lastSelectedTg);  // also calls lpUpdateOverlay()
                                }
                                else lpUpdateOverlay();
                                startupBankRestore();
                            }
                        });
                    });
                    startupInProgress = true;
                    startupThread->startThread();
                }
                if (cfg.inputPort.isNotEmpty())
                {
                    midiInputCombo.setText(cfg.inputPort, juce::dontSendNotification);
                    if (cfg.forwardingEnabled && midiSender->isOpen())
                    {
                        if (midiThru->start(midiInputCombo.getText(), midiSender.get()))
                        {
                            forwardingToggle.setToggleState(true, juce::dontSendNotification);
                            midiStatusBox.setText("Forwarding ON (" + midiInputCombo.getText() + " \xe2\x86\x92 " + midiOutputCombo.getText() + ")");
                        }
                        else
                        {
                            midiStatusBox.setText("Forwarding: saved input not found (" + midiInputCombo.getText() + ")");
                        }
                    }
                }
            });
        }
    }

    // Restore last-selected TG unconditionally (covers no-MIDI case too).
    // Captured by value so there's no dangling reference after constructor returns.
    {
        midi::ConfigState cfg; midi::Config::load(cfg);
        const int tgToRestore = cfg.lastSelectedTg;
        juce::MessageManager::callAsync([this, tgToRestore] { selectTg(tgToRestore); });
    }

    midiOutputCombo.onChange = [this]
    {
        const auto name = midiOutputCombo.getText();
        const bool ok = midiSender->openByName(name);
        midiStatusBox.setText(ok ? juce::String("Opened OUT: ") + name : juce::String("Open OUT failed: ") + name);
        midi::ConfigState cfg; midi::Config::load(cfg); cfg.outputPort = name; midi::Config::save(cfg);
        if (ok)
        {
            if (startupThread) startupThread->stopThread(8000);
            midiStatusBox.setText("Sending startup sequence...");
            startupThread = std::make_unique<StartupThread>(*midiSender, cfg.deviceId,
                    [this](int tg, bool on) {
                        juce::MessageManager::callAsync([this, tg, on] { setTgLed(tg, on); });
                    },
                    [this](int stage) {
                        if (stage == 2)
                            juce::MessageManager::callAsync([this] { startupInProgress = false; lpUpdateOverlay(); startupBankRestore(); });
                    });
            startupInProgress = true;
            startupThread->startThread();
        }
        if (forwardingToggle.getToggleState() && midiSender->isOpen() && midiInputCombo.getText().isNotEmpty())
        {
            if (midiThru && midiThru->isRunning()) midiThru->stop();
            if (midiThru->start(midiInputCombo.getText(), midiSender.get()))
                midiStatusBox.setText("Forwarding ON (" + midiInputCombo.getText() + " \xe2\x86\x92 " + midiOutputCombo.getText() + ")");
        }
    };

    midiInputCombo.onChange = [this]
    {
        midi::ConfigState cfg; midi::Config::load(cfg); cfg.inputPort = midiInputCombo.getText(); midi::Config::save(cfg);
        // TODO (Bug 3): if forwardingToggle is ON, stop and restart midiThru with the new input
    };

    forwardingToggle.onClick = [this]
    {
        if (forwardingToggle.getToggleState())
        {
            if (midiSender && midiSender->isOpen() && midiInputCombo.getText().isNotEmpty())
            {
                const auto inName = midiInputCombo.getText();
                if (midiThru->start(inName, midiSender.get()))
                    midiStatusBox.setText("Forwarding ON (" + inName + " \xe2\x86\x92 " + midiOutputCombo.getText() + ")");
                else
                    midiStatusBox.setText("Failed to start forwarding");
            }
            else
            {
                forwardingToggle.setToggleState(false, juce::dontSendNotification);
                midiStatusBox.setText("Select input and open output first");
            }
        }
        else
        {
            if (midiThru && midiThru->isRunning()) midiThru->stop();
            midiStatusBox.setText("Forwarding OFF");
        }
        midi::ConfigState cfg; midi::Config::load(cfg); cfg.forwardingEnabled = forwardingToggle.getToggleState(); midi::Config::save(cfg);
    };

    // Load pacing and device ID config
    {
        midi::ConfigState cfg; midi::Config::load(cfg);
        devIdSlider.setRange(1, 16, 1);
        devIdSlider.setValue(cfg.deviceId, juce::dontSendNotification);
        chunkSlider.setRange(32, 4096, 1);
        chunkSlider.setValue(cfg.sysexChunkBytes, juce::dontSendNotification);
        delaySlider.setRange(1, 500, 1);
        delaySlider.setValue(cfg.sysexInterChunkMs, juce::dontSendNotification);
    }
    {
        patchesCombo.addItem("1",  1);
        patchesCombo.addItem("8",  2);
        patchesCombo.addItem("16", 3);
        patchesCombo.addItem("32", 4);
        midi::ConfigState cfg; midi::Config::load(cfg);
        int p = cfg.patchesToSend;
        int comboId = (p == 1) ? 1 : (p == 16) ? 3 : (p == 32) ? 4 : 2; // default to 8
        patchesCombo.setSelectedId(comboId, juce::dontSendNotification);
    }
    patchesCombo.onChange = [this]{
        const int map[] = { 1, 8, 16, 32 };
        int idx = patchesCombo.getSelectedId() - 1;
        if (idx >= 0 && idx < 4)
        {
            midi::ConfigState cfg; midi::Config::load(cfg);
            cfg.patchesToSend = map[idx];
            midi::Config::save(cfg);

            visibleSlots = map[idx];
            bankSlotIds.resize(visibleSlots);
            for (int i = bankSlotIds.size(); i < visibleSlots; ++i) bankSlotIds.set(i, 0);
            bankModel.setNumSlots(visibleSlots);
            lpBankList.updateContent();
            lpBankList.repaint();
            lpBankStrip.repaint();
        }
    };
    devIdSlider.onValueChange = [this]{
        midi::ConfigState cfg; midi::Config::load(cfg); cfg.deviceId = (int) devIdSlider.getValue(); midi::Config::save(cfg);
    };
    chunkSlider.onValueChange = [this]{
        midi::ConfigState cfg; midi::Config::load(cfg); cfg.sysexChunkBytes = (int) chunkSlider.getValue(); midi::Config::save(cfg);
    };
    delaySlider.onValueChange = [this]{
        midi::ConfigState cfg; midi::Config::load(cfg); cfg.sysexInterChunkMs = (int) delaySlider.getValue(); midi::Config::save(cfg);
    };

    prepareBtn.onClick = [this]
    {
        if (midiSender && midiSender->isOpen())
        {
            midi::ConfigState cfg; midi::Config::load(cfg);
            if (startupThread) startupThread->stopThread(8000);
            midiStatusBox.setText("Sending startup sequence...");
            startupThread = std::make_unique<StartupThread>(*midiSender, cfg.deviceId,
                    [this](int tg, bool on) {
                        juce::MessageManager::callAsync([this, tg, on] { setTgLed(tg, on); });
                    },
                    [this](int stage) {
                        if (stage == 2)
                            juce::MessageManager::callAsync([this] { startupInProgress = false; lpUpdateOverlay(); startupBankRestore(); });
                    });
            startupInProgress = true;
            startupThread->startThread();
        }
        else midiStatusBox.setText("Open MIDI Out first");
    };

    midi::StartupLog::write("CTOR: Adding tabs");
    // ── Tabs ──
    tabs.addTab("Left Panel",  juce::Colours::darkgrey, &leftPanelTab,  false);
    tabs.addTab("Right Panel", juce::Colours::darkgrey, &frontPanelTab, false);
    tabs.setCurrentTabIndex(0);

    midi::StartupLog::write("CTOR: setSize + refreshPage");
    {
        auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        float scale = disp ? (float)disp->scale : 1.0f;
        setSize(juce::roundToInt(PanelLayout::kPanelWidth / scale), 964);
    }
    lpBrowserOverlay.setVisible(true);  // block interaction until startup completes
    midi::StartupLog::write("=== MainComponent CTOR END ===");
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);
}

void MainComponent::resized()
{
    midi::StartupLog::write("resized() called");
    auto area = getLocalBounds();
    tabs.setBounds(area);
    tabs.resized();
    midi::StartupLog::write("resized() tabs done");

    // ── Front Panel layout — fixed-size panel image with PanelButton hit-rects ──
    {
        using namespace PanelLayout;
        auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        float scale = disp ? (float)disp->scale : 1.0f;
        auto place = [scale](juce::Component& c, const Rect& r) {
            c.setBounds(juce::roundToInt(r.x / scale), juce::roundToInt(r.y / scale),
                        juce::roundToInt(r.w / scale), juce::roundToInt(r.h / scale));
        };

        // Mode select buttons (radio)
        place(fpPerformSelect, Right::performSelect);
        place(fpVoiceSelect,   Right::voiceSelect);
        place(fpSystemSetup,   Right::systemSetup);
        place(fpUtility,       Right::utility);
        place(fpPerformEdit,   Right::performEdit);
        place(fpVoiceEditI,    Right::voiceEditI);
        place(fpVoiceEditII,   Right::voiceEditII);
        place(fpStore,         Right::storeCompare);

        // Momentary buttons
        place(fpYes,   Right::onYes);
        place(fpNo,    Right::offNo);
        place(fpInt,   Right::intBtn);
        place(fpCrt,   Right::crtBtn);
        place(fpEnter, Right::enterSpace);
        place(fpDash,  Right::minus);

        // Number buttons 0..9
        for (int i = 0; i <= 9; ++i)
            place(*fpNumButtons[i], Right::numPad[i]);

        // Status label sits just below the panel image
        frontStatus.setBounds(0, juce::roundToInt(kPanelHeight / scale) + 4,
                              juce::roundToInt(kPanelWidth / scale), 22);

    }

    // ── Left Panel LED overlay layout ──
    {
        using namespace PanelLayout;
        auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        float scale = disp ? (float)disp->scale : 1.0f;
        for (int i = 0; i < 8; ++i)
        {
            const auto& pt = Left::tgLed[i];
            tgLedOverlay[i].setBounds(
                juce::roundToInt(pt.x / scale),
                juce::roundToInt(pt.y / scale),
                juce::roundToInt(kLedWidth  / scale),
                juce::roundToInt(kLedHeight / scale));
        }
    }

    // ── Left Panel TG button hit-rects ──
    {
        using namespace PanelLayout;
        auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        float scale = disp ? (float)disp->scale : 1.0f;
        for (int i = 0; i < 8; ++i)
        {
            const auto& r = Left::tgButton[i];
            lpTgButtons[i]->setBounds(
                juce::roundToInt(r.x / scale), juce::roundToInt(r.y / scale),
                juce::roundToInt(r.w / scale), juce::roundToInt(r.h / scale));
        }
    }

    // ── Left Panel LCD display overlay ──
    {
        using namespace PanelLayout;
        auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        float scale = disp ? (float)disp->scale : 1.0f;
        const auto& r = Left::displayArea;
        lcdDisplay.setBounds(
            juce::roundToInt(r.x / scale), juce::roundToInt(r.y / scale),
            juce::roundToInt(r.w / scale), juce::roundToInt(r.h / scale));
    }

    // ── Left Panel inline performance section layout ──
    {
        using namespace PanelLayout;
        auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        float scale = disp ? (float)disp->scale : 1.0f;
        const int panelBottom = juce::roundToInt(kPanelHeight / scale);
        const int sectionW    = juce::roundToInt(kPanelWidth  / scale);
        // Horizontal bank strip sits immediately below the panel image
        const int bankStripH  = 28;
        lpBankStrip.setBounds(0, panelBottom + 4, sectionW, bankStripH);
        // Strip height: 8px reduced padding + 16px header + 0px gap + 44px row + 8px padding = 76px
        const int lpStripH    = 76;
        lpPerfSection.setBounds(0, panelBottom + 4 + bankStripH + 4, sectionW, lpStripH);

        const int colW[] = { 25, 50, 120, 55, 65, 65, 50, 50, 50, 75, 44 };
        const int rowH = 44, hdrH = 16, gap = 2, sepGap = 11; // sepGap = 4px + 2px VRule (line at +1) + 5px → 5px visual on each side

        auto area = lpPerfSection.getLocalBounds().reduced(8);
        auto hdrRow = area.removeFromTop(hdrH);
        // lpHdrTg removed — TG number now displayed on the right in a larger font
        // lpHdrOnOff.setBounds(hdrRow.removeFromLeft(colW[1])); hdrRow.removeFromLeft(gap);  // hidden: panel buttons
        // lpHdrPrst.setBounds(hdrRow.removeFromLeft(colW[2]));  hdrRow.removeFromLeft(gap);  // hidden: LCD line 0
        lpHdrChan.setBounds(hdrRow.removeFromLeft(colW[3]));      hdrRow.removeFromLeft(sepGap); // | Sep
        lpHdrLow.setBounds(hdrRow.removeFromLeft(colW[4]));       hdrRow.removeFromLeft(gap);
        lpHdrHigh.setBounds(hdrRow.removeFromLeft(colW[5]));      hdrRow.removeFromLeft(sepGap); // | Sep
        lpHdrDet.setBounds(hdrRow.removeFromLeft(colW[6]));       hdrRow.removeFromLeft(sepGap); // | Sep
        lpHdrShft.setBounds(hdrRow.removeFromLeft(colW[7]));      hdrRow.removeFromLeft(sepGap); // | Sep
        lpHdrVol.setBounds(hdrRow.removeFromLeft(colW[8]));       hdrRow.removeFromLeft(sepGap); // | Sep
        lpHdrOut.setBounds(hdrRow.removeFromLeft(colW[9]));       hdrRow.removeFromLeft(sepGap); // | Sep
        lpHdrDamp.setBounds(hdrRow.removeFromLeft(colW[10]));
        // TG number on the right, ~100px past Damp (fine-tune the offset as needed)
        const int dampRight = 8 + colW[3] + sepGap + colW[4] + gap + colW[5] + sepGap
                                + colW[6] + sepGap + colW[7] + sepGap + colW[8] + sepGap
                                + colW[9] + sepGap + colW[10];
        const int tgNumX = dampRight + 100;
        const int tgNumW = 60;

        // All 8 rows occupy the same slot — only the selected one is visible at a time.
        const int comboH = 24; // match browser filter row height
        auto rowSlot = area.removeFromTop(rowH);
        for (int i = 0; i < 8; ++i)
        {
            auto row = rowSlot;
            // lpTgNum moved to the right side (large font); no longer at left of row
            // lpTgOnOff[i].setBounds(row.removeFromLeft(colW[1]));  row.removeFromLeft(gap);  // hidden: panel buttons
            // lpTgPreset[i].setBounds(row.removeFromLeft(colW[2])); row.removeFromLeft(gap);  // hidden: LCD line 0
            lpTgNum[i].setBounds(tgNumX, 0, tgNumW, lpPerfSection.getHeight());
            lpTgRxCh[i].setBounds(row.removeFromLeft(colW[3]).withSizeKeepingCentre(colW[3], comboH));      row.removeFromLeft(sepGap); // | Sep
            lpTgNoteLow[i].setBounds(row.removeFromLeft(colW[4]).withSizeKeepingCentre(colW[4], comboH));   row.removeFromLeft(gap);
            lpTgNoteHigh[i].setBounds(row.removeFromLeft(colW[5]).withSizeKeepingCentre(colW[5], comboH));  row.removeFromLeft(sepGap); // | Sep
            lpTgDetune[i].setBounds(row.removeFromLeft(colW[6]).withSizeKeepingCentre(colW[6], comboH));    row.removeFromLeft(sepGap); // | Sep
            lpTgShift[i].setBounds(row.removeFromLeft(colW[7]).withSizeKeepingCentre(colW[7], comboH));     row.removeFromLeft(sepGap); // | Sep
            lpTgVol[i].setBounds(row.removeFromLeft(colW[8]).withSizeKeepingCentre(colW[8], comboH));       row.removeFromLeft(sepGap); // | Sep
            lpTgOut[i].setBounds(row.removeFromLeft(colW[9]));                                               row.removeFromLeft(sepGap); // | Sep
            lpTgDamp[i].setBounds(row.removeFromLeft(colW[10]).withSizeKeepingCentre(comboH, comboH));
        }

        // Vertical separators spanning the full strip height.
        // VRule is 2px wide; drawVerticalLine draws at getWidth()/2 = x+1 inside the component.
        // Placing component at cx+4 gives: 5px visual left of line, 5px visual right of line.
        // sepGap=11 = 4px (before component) + 2px (component) + 5px (after component).
        // Groups: Chan | Low+High | Det | Shft | Vol | Out | Damp
        {
            const int sh = lpPerfSection.getHeight();
            int cx = 8;                                    // inset only (# column removed)
            cx += colW[3]; lpPerfSep[0].setBounds(cx + 4, 0, 2, sh); cx += sepGap; // Chan | Low
            cx += colW[4] + gap + colW[5];                 // skip Low + gap + High
            lpPerfSep[1].setBounds(cx + 4, 0, 2, sh); cx += sepGap;                // High | Det
            cx += colW[6]; lpPerfSep[2].setBounds(cx + 4, 0, 2, sh); cx += sepGap; // Det  | Shft
            cx += colW[7]; lpPerfSep[3].setBounds(cx + 4, 0, 2, sh); cx += sepGap; // Shft | Vol
            cx += colW[8]; lpPerfSep[4].setBounds(cx + 4, 0, 2, sh); cx += sepGap; // Vol  | Out
            cx += colW[9]; lpPerfSep[5].setBounds(cx + 4, 0, 2, sh);               // Out  | Damp
        }
    }

    // ── Left Panel inline Preset Browser layout ──
    {
        using namespace PanelLayout;
        auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        float scale = disp ? (float)disp->scale : 1.0f;
        const int panelBottom = juce::roundToInt(kPanelHeight / scale);
        const int sectionW    = juce::roundToInt(kPanelWidth  / scale);
        const int bankStripH  = 28;
        const int lpStripH    = 76;
        const int browserTop  = panelBottom + 4 + bankStripH + 4 + lpStripH + 4;
        const int browserH    = juce::jmax(0, getHeight() - tabs.getTabBarDepth() - browserTop);
        lpBrowserSection.setBounds(0, browserTop, sectionW, browserH);

        // Overlay covers both the TG strip and the browser section (child of leftPanelTab)
        lpBrowserOverlay.setBounds(lpPerfSection.getBoundsInParent().getUnion(lpBrowserSection.getBoundsInParent()));

        auto area = lpBrowserSection.getLocalBounds().reduced(6);
        const int rowH = 24, gap = 4, btnW = 50;

        // Top row: filter | rating | ⏮ ◀ ▶ ⏭  (all left-aligned, compact)
        const int navW = 28;
        auto filterRow = area.removeFromTop(rowH);
        lpFilterEdit.setBounds(filterRow.removeFromLeft(200));          filterRow.removeFromLeft(6);
        lpRatingFilterCombo.setBounds(filterRow.removeFromLeft(100));   filterRow.removeFromLeft(6);
        lpSep1.setBounds(filterRow.removeFromLeft(1));                  filterRow.removeFromLeft(6);
        lpFirstPage.setBounds(filterRow.removeFromLeft(navW));          filterRow.removeFromLeft(gap);
        lpPrevPage.setBounds(filterRow.removeFromLeft(navW));           filterRow.removeFromLeft(gap);
        lpNextPage.setBounds(filterRow.removeFromLeft(navW));           filterRow.removeFromLeft(gap);
        lpLastPage.setBounds(filterRow.removeFromLeft(navW));           filterRow.removeFromLeft(6);
        lpSep2.setBounds(filterRow.removeFromLeft(1));                  filterRow.removeFromLeft(6);
        lpAutoSendToggle.setBounds(filterRow.removeFromLeft(rowH));     filterRow.removeFromLeft(gap);
        lpAutoSendButton.setBounds(filterRow.removeFromLeft(42));
        area.removeFromTop(gap);

        // Preset list fills full width
        auto statusRow = area.removeFromBottom(18);
        lpBrowserStatusLabel.setBounds(statusRow);
        auto presetHdrRow = area.removeFromTop(18);
        lpPresetHeader.setBounds(presetHdrRow);
        lpPresetList.setBounds(area);
    }

    // ── Right Panel macro strip + settings section layout ──
    {
        using namespace PanelLayout;
        auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        float scale = disp ? (float)disp->scale : 1.0f;
        const int panelBottom  = juce::roundToInt(kPanelHeight / scale);
        const int sectionW     = juce::roundToInt(kPanelWidth  / scale);
        const int macroStripH  = 68;
        const int macroTop     = panelBottom + 4;
        rpMacroStrip.setBounds(0, macroTop, sectionW, macroStripH);
        // Internal macro strip layout
        {
            auto ms  = rpMacroStrip.getLocalBounds().reduced(8);
            const int btnH = 36, btnW = 110, gap = 8;
            auto row = ms.withSizeKeepingCentre(ms.getWidth(), btnH);
            fpReset.setBounds(row.removeFromLeft(btnW));    row.removeFromLeft(gap);
            fpPrtctOff.setBounds(row.removeFromLeft(btnW)); row.removeFromLeft(gap);
            fpPrtctOn.setBounds(row.removeFromLeft(btnW));  row.removeFromLeft(gap);
            fpPos1.setBounds(row.removeFromLeft(btnW));
        }
        const int settingsTop  = macroTop + macroStripH + 4;
        const int sectionH     = juce::jmax(0, getHeight() - tabs.getTabBarDepth() - settingsTop);
        rpSettingsSection.setBounds(0, settingsTop, sectionW, sectionH);
    }
    {
        auto st = rpSettingsSection.getLocalBounds().reduced(16);
        const int rowH = 28, labelH = 18, gap = 6, sectionGap = 14, comboW = 340, btnW = 150;

        // DB file selection — above all other settings
        { auto r = st.removeFromTop(rowH); selectDbButton.setBounds(r.removeFromLeft(120)); r.removeFromLeft(8); dbPathLabel.setBounds(r); }
        st.removeFromTop(sectionGap);

        settHdrMidi.setBounds(st.removeFromTop(rowH)); st.removeFromTop(gap);
        { auto r = st.removeFromTop(labelH); settLblInput.setBounds(r.removeFromLeft(comboW)); r.removeFromLeft(gap); settLblOutput.setBounds(r.removeFromLeft(comboW)); }
        { auto r = st.removeFromTop(rowH); midiInputCombo.setBounds(r.removeFromLeft(comboW)); r.removeFromLeft(gap); midiOutputCombo.setBounds(r.removeFromLeft(comboW)); r.removeFromLeft(gap); refreshMidiButton.setBounds(r.removeFromLeft(btnW)); }
        st.removeFromTop(gap);
        forwardingToggle.setBounds(st.removeFromTop(rowH)); st.removeFromTop(gap);
        midiStatusBox.setBounds(st.removeFromTop(rowH)); st.removeFromTop(sectionGap);

        settHdrDevice.setBounds(st.removeFromTop(rowH)); st.removeFromTop(gap);
        { auto r = st.removeFromTop(rowH); prepareBtn.setBounds(r.removeFromLeft(btnW)); r.removeFromLeft(gap); rebootButton.setBounds(r.removeFromLeft(btnW)); r.removeFromLeft(gap); playTest.setBounds(r.removeFromLeft(btnW)); r.removeFromLeft(gap); panicBtn.setBounds(r.removeFromLeft(btnW)); }
        st.removeFromTop(sectionGap);

        settHdrSysex.setBounds(st.removeFromTop(rowH)); st.removeFromTop(gap);
        { auto r = st.removeFromTop(rowH); devIdLabel.setBounds(r.removeFromLeft(100)); devIdSlider.setBounds(r.removeFromLeft(140)); r.removeFromLeft(gap*3); chunkLabel.setBounds(r.removeFromLeft(150)); chunkSlider.setBounds(r.removeFromLeft(180)); }
        st.removeFromTop(gap);
        { auto r = st.removeFromTop(rowH); r.removeFromLeft(100+140+gap*3); delayLabel.setBounds(r.removeFromLeft(150)); delaySlider.setBounds(r.removeFromLeft(180)); }
        st.removeFromTop(gap);
        { auto r = st.removeFromTop(rowH); patchesLabel.setBounds(r.removeFromLeft(100)); patchesCombo.setBounds(r.removeFromLeft(140)); }
    }
}


// ── LP Preset Browser — model + header ──

int MainComponent::LpPresetModel::getNumRows()
{
    if (owner.selectedTg < 0) return 0;
    return owner.lpBrowserState[owner.selectedTg].currentRows.size();
}

void MainComponent::LpPresetModel::paintListBoxItem(int row, juce::Graphics& g,
                                                    int width, int height, bool selected)
{
    if (owner.selectedTg < 0) return;
    const auto& rows = owner.lpBrowserState[owner.selectedTg].currentRows;
    if (! juce::isPositiveAndBelow(row, rows.size())) return;
    const auto& r = rows[row];

    if (selected) g.fillAll(juce::Colour(0xFF003366).withAlpha(0.8f));
    g.setColour(selected ? juce::Colours::white : juce::Colours::lightgrey);
    g.setFont(13.0f);
    const int idW = 44, nameW = 130, x0 = 4;
    g.drawText(juce::String(r.id),   x0,           0, idW,             height, juce::Justification::centredLeft);
    g.drawText(r.presetName,         x0 + idW,     0, nameW,           height, juce::Justification::centredLeft);
    g.drawText(r.category,           x0 + idW + nameW, 0, width - x0 - idW - nameW, height, juce::Justification::centredLeft);
}

void MainComponent::LpPresetModel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    owner.lpPresetItemClicked(row);
}

void MainComponent::LpPresetHeader::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey.darker(0.3f));
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    const int h = getHeight(), idW = 44, nameW = 130, x0 = 4;
    g.drawText("ID",       x0,           0, idW,             h, juce::Justification::centredLeft);
    g.drawText("Patch",    x0 + idW,     0, nameW,           h, juce::Justification::centredLeft);
    g.drawText("Category", x0 + idW + nameW, 0, getWidth() - x0 - idW - nameW, h, juce::Justification::centredLeft);
}


// ── Bank slot DnD components ──

void MainComponent::LpBankStrip::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1E1E1E));

    const int n = owner.visibleSlots;
    if (n == 0) return;

    const float cellW = (float)getWidth() / (float)n;
    const int   h     = getHeight();

    g.setFont(juce::Font(12.0f));

    for (int i = 0; i < n; ++i)
    {
        const int x0 = juce::roundToInt(i * cellW);
        const int x1 = juce::roundToInt((i + 1) * cellW);
        const int cw = x1 - x0;

        // Subtle alternating tint
        if (i % 2 == 1)
        {
            g.setColour(juce::Colours::white.withAlpha(0.04f));
            g.fillRect(x0, 0, cw, h);
        }

        // Cell divider
        if (i > 0)
        {
            g.setColour(juce::Colours::white.withAlpha(0.2f));
            g.drawVerticalLine(x0, 2.0f, (float)(h - 2));
        }

        // Label: centred patch name, empty slots shown as "-"
        const auto slotName = owner.bankModel.getSlotName(i);
        const bool empty    = slotName.isEmpty() || slotName == "empty";
        const auto text     = empty ? juce::String("-") : slotName;
        g.setColour(empty ? juce::Colours::darkgrey : juce::Colours::lightgrey);
        g.drawText(text, x0 + 4, 0, cw - 8, h, juce::Justification::centred, true);
    }
}

void MainComponent::BankSlotComponent::paint(juce::Graphics& g)
{
    if (owner.bankModel.getSlotName(index).isEmpty()) return;
    auto bounds = getLocalBounds();
    if (selected)
        g.fillAll(juce::Colours::steelblue);
    else if (over)
        g.fillAll(juce::Colours::steelblue.withAlpha(0.3f));
    else
        g.fillAll(index % 2 == 0 ? juce::Colours::transparentBlack : juce::Colour(0x10ffffff));

    g.setFont(juce::Font(13.0f));
    g.setColour(juce::Colours::white);
    g.drawText(juce::String(index + 1), 6, 0, 28, bounds.getHeight(), juce::Justification::centredLeft);
    g.setColour(juce::Colours::lightgrey);
    g.drawText(owner.bankModel.getSlotName(index), 40, 0, bounds.getWidth() - 46, bounds.getHeight(), juce::Justification::centredLeft);
}

bool MainComponent::BankSlotComponent::isInterestedInDragSource(const SourceDetails& d)
{
    auto desc = d.description.toString();
    return desc.startsWith("preset:") || desc.startsWith("bank:");
}

void MainComponent::BankSlotComponent::itemDragEnter(const SourceDetails&) { over = true; repaint(); }
void MainComponent::BankSlotComponent::itemDragExit(const SourceDetails&)  { over = false; repaint(); }

void MainComponent::BankSlotComponent::itemDropped(const SourceDetails& d)
{
    over = false; repaint();
    auto desc = d.description.toString();
    if (desc.startsWith("bank:"))
    {
        int srcIdx = desc.fromFirstOccurrenceOf("bank:", false, false).getIntValue();
        owner.moveBankSlot(srcIdx, index);
    }
}

void MainComponent::BankSlotComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 5) return;
    juce::Image dragImg(juce::Image::ARGB, 120, 20, true);
    { juce::Graphics ig(dragImg); ig.setColour(juce::Colours::white.withAlpha(0.6f)); ig.fillAll(); ig.setColour(juce::Colours::black); ig.drawText(owner.bankModel.getSlotName(index), 4, 0, 112, 20, juce::Justification::centredLeft); }
    owner.startDragging(juce::var(juce::String("bank:") + juce::String(index)), this, dragImg, true);
}

juce::Component* MainComponent::BankModel::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existing)
{
    if (owner == nullptr) return existing;
    auto* c = dynamic_cast<MainComponent::BankSlotComponent*>(existing);
    if (c == nullptr)
        c = new BankSlotComponent(*owner, rowNumber);
    c->updateIndex(rowNumber, isRowSelected);
    return c;
}


// ── Bank helpers ──
void MainComponent::setBankSlotFromPreset(int slotIndex, int presetId, const juce::String& name)
{
    if (! juce::isPositiveAndBelow(slotIndex, bankSlotIds.size())) return;
    bankSlotIds.set(slotIndex, presetId);
    bankModel.setSlotName(slotIndex, name);
    lpBankList.updateContent();
    lpBankList.repaintRow(slotIndex);
}

void MainComponent::moveBankSlot(int fromIndex, int toIndex)
{
    if (fromIndex == toIndex) return;
    const int fromId = bankSlotIds[fromIndex];
    const juce::String fromName = bankModel.getSlotName(fromIndex);
    bankSlotIds.set(fromIndex, bankSlotIds[toIndex]);
    bankModel.setSlotName(fromIndex, bankModel.getSlotName(toIndex));
    bankSlotIds.set(toIndex, fromId);
    bankModel.setSlotName(toIndex, fromName);
    lpBankList.updateContent();
    lpBankList.repaint();
    lpBankStrip.repaint();
}

void MainComponent::initBank()
{
    for (int i = 0; i < visibleSlots; ++i)
    {
        bankSlotIds.set(i, 0);
        bankModel.setSlotName(i, "empty");
    }
    lpBankList.updateContent();
    lpBankList.repaint();
    lpBankStrip.repaint();
}

void MainComponent::randomizeBank()
{
    if (! presetsDb) return;
    juce::String err; int total = 0;
    auto all = presetsDb->queryPresets("", "", 99999, 0, total, err);
    if (all.isEmpty()) return;

    // Shuffle indices
    juce::Array<int> indices;
    for (int i = 0; i < all.size(); ++i) indices.add(i);
    juce::Random rng;
    for (int i = indices.size() - 1; i > 0; --i)
    {
        int j = rng.nextInt(i + 1);
        indices.swap(i, j);
    }

    for (int slot = 0; slot < visibleSlots; ++slot)
    {
        if (slot < indices.size())
        {
            auto r = all[indices[slot]];
            bankSlotIds.set(slot, r.id);
            bankModel.setSlotName(slot, r.presetName);
        }
        else
        {
            bankSlotIds.set(slot, 0);
            bankModel.setSlotName(slot, "empty");
        }
    }
    lpBankList.updateContent();
    lpBankList.repaint();
    lpBankStrip.repaint();
}

void MainComponent::startupBankRestore()
{
    if (startupBankData.getSize() != 4104) return;
    if (! midiSender || ! midiSender->isOpen()) return;

    midi::ConfigState cfg; midi::Config::load(cfg);

    if (bankSendThread)
    {
        bankSendThread->stopThread(5000);
        bankSendThread.reset();
    }

    midiStatusBox.setText("Restoring bank...");
    bankSendThread = std::make_unique<BankSendThread>(
        *midiSender, (juce::uint8) cfg.deviceId,
        startupBankData, false, 32,
        cfg.sysexChunkBytes, cfg.sysexInterChunkMs,
        [this](bool ok, int)
        {
            juce::MessageManager::callAsync([this, ok]
            {
                midiStatusBox.setText(ok ? "Bank restored" : "Bank restore failed");
            });
        });
    bankSendThread->startThread();
}

void MainComponent::sendBankToDevice()
{
    if (! midiSender || ! midiSender->isOpen()) { midiStatusBox.setText("Open a MIDI output first"); return; }
    if (! presetsDb) { midiStatusBox.setText("No DB"); return; }

    midi::ConfigState cfg;
    midi::Config::load(cfg);
    const juce::uint8 deviceId = (juce::uint8) cfg.deviceId;
    const int patchCount = juce::jlimit(1, 32, cfg.patchesToSend);
    const bool isPartial = patchCount < 32;

    juce::MemoryBlock packed4096(32 * 128);
    auto* out = static_cast<juce::uint8*>(packed4096.getData());
    const bool hasStartupBank = (startupBankData.getSize() == 4104);
    const auto* startupVoices = hasStartupBank
        ? static_cast<const juce::uint8*>(startupBankData.getData()) + 6  // skip 6-byte header
        : nullptr;

    for (int i = 0; i < 32; ++i)
    {
        juce::MemoryBlock v128;
        const int id = (i < bankSlotIds.size()) ? bankSlotIds[i] : 0;
        if (id > 0)
        {
            // Slot has a newly selected preset — fetch from database
            juce::MemoryBlock syx; juce::String err; juce::String msg; juce::MemoryBlock v155;
            if (! presetsDb->getSysexById(id, syx, err) || ! core::Dx7Utils::verifySingleVoiceSysex(syx, msg, v155))
            { midiStatusBox.setText("Invalid slot " + juce::String(i+1) + ": " + (err.isNotEmpty()?err:msg)); return; }
            v128 = core::Dx7Utils::packSingleToBankVoice(v155);
        }
        else if (startupVoices != nullptr)
        {
            // Slot unchanged — preserve the existing voice from the last sent bank
            v128.setSize(128);
            std::memcpy(v128.getData(), startupVoices + i * 128, 128);
        }
        else
        {
            // No previous bank exists (first run) — use INIT VOICE
            v128 = getInitVoice128();
        }
        std::memcpy(out + i * 128, v128.getData(), 128);
    }

    juce::MemoryBlock bank(4104);
    auto* b = static_cast<juce::uint8*>(bank.getData());
    b[0]=0xF0; b[1]=0x43; b[2]=0x00; b[3]=0x09; b[4]=0x20; b[5]=0x00;
    std::memcpy(b + 6, packed4096.getData(), 4096);
    juce::MemoryBlock dataForChecksum; dataForChecksum.append(b + 6, (size_t)4096);
    b[4102] = core::Dx7Utils::calculateChecksum(dataForChecksum);
    b[4103] = 0xF7;

    {
        juce::String msg;
        if (! core::Dx7Utils::isValidDx7Bank(bank, msg))
            midiStatusBox.setText("Bank validate failed: " + msg);
    }

    {
        auto cfgFile = midi::Config::getConfigFile();
        auto bankFile = cfgFile.getParentDirectory().getChildFile("startup_bank.syx");
        bankFile.replaceWithData(bank.getData(), (int) bank.getSize());
    }

    // Save slot names to config now (main thread, before background send starts)
    {
        midi::ConfigState cfgS; midi::Config::load(cfgS);
        cfgS.presetBankNames.clear();
        for (int i = 0; i < 32; ++i)
            cfgS.presetBankNames.add(bankModel.getSlotName(i));
        midi::Config::save(cfgS);
        refreshPerfPresetDropdowns();
        lpBankStrip.repaint();
        setLcdLine(0, buildVoiceSelectLine());
    }

    // Keep a copy of the new bank so startupBankRestore() can re-send it later
    startupBankData = bank;

    // Stop any in-progress send before starting a new one
    if (bankSendThread)
    {
        bankSendThread->stopThread(5000);
        bankSendThread.reset();
    }

    midiStatusBox.setText("Sending...");

    bankSendThread = std::make_unique<BankSendThread>(
        *midiSender, deviceId, std::move(bank), isPartial, patchCount,
        cfg.sysexChunkBytes, cfg.sysexInterChunkMs,
        [this, isPartial, patchCount](bool /*ok*/, int count)
        {
            juce::MessageManager::callAsync([this, isPartial, count]
            {
                midiStatusBox.setText(
                    isPartial ? "Sent " + juce::String(count) + " voices (partial transfer)"
                              : "Sent 32-voice bank");
            });
        });
    bankSendThread->startThread();
}

// ── Helpers ──

void MainComponent::deactivateModeButtons()
{
    for (auto* b : fpModeGroup) b->setActive(false);
}


void MainComponent::rebuildMidiOutputs()
{
    midiOutputCombo.clear();
    auto names = juce::MidiOutput::getDevices();
    for (int i = 0; i < names.size(); ++i)
        midiOutputCombo.addItem(names[i], i + 1);
    if (names.isEmpty())
        midiStatusBox.setText("No MIDI outputs found");
}

void MainComponent::rebuildMidiInputs()
{
    const auto prev = midiInputCombo.getText();
    midiInputCombo.clear(juce::dontSendNotification);   // don't clobber saved config
    int id = 1;
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        midiInputCombo.addItem(d.name, id++);
    if (prev.isNotEmpty())
        midiInputCombo.setText(prev, juce::dontSendNotification);  // restore silently
}

void MainComponent::sendReboot()
{
    if (! midiSender || ! midiSender->isOpen())
    {
        midiStatusBox.setText("Open a MIDI output first");
        return;
    }
    const juce::uint8 deviceId = 1;
    const bool ok = midi::Tx802HighLevel::sendButtonByName(*midiSender, "RESET", deviceId);
    midiStatusBox.setText(ok ? "Sent REBOOT (RESET)" : "Failed to send REBOOT");
    if (ok)
    {
        setLcdLine(0, kLcdReset0);
        setLcdLine(1, kLcdReset1);
        juce::Timer::callAfterDelay(3000, [this] { updateLcdFromConfig(); });
    }
}


// ── Performance parameter send (used by Left Panel parameter strip) ──

void MainComponent::sendPerfParam(int tg1to8, const juce::String& paramName, int userValue)
{
    if (! midiSender || ! midiSender->isOpen())
        return;

    midi::ConfigState cfg;
    midi::Config::load(cfg);
    const juce::uint8 devId = (juce::uint8) cfg.deviceId;
    const int i = tg1to8 - 1;

    // TG1 On/Off: PCED LINK (param 0) is accepted by the device but has no effect —
    // TG1 is the anchor TG and cannot be linked/unlinked via SysEx.
    // The only mechanism that controls TG1 on the device is Remote Switch code 89
    // (the physical TG1 button). We send it only when the state is actually changing
    // to avoid toggling in the wrong direction (code 89 is a toggle, not set/clear).
    // All other parameters (VNUM, RXCH, note range, etc.) use PCED normally for TG1.
    if (tg1to8 == 1 && paramName == "TG")
    {
        const bool currentlyOn = midi::tgOnFromString(cfg.tg[0].tgOnOff);
        if (currentlyOn != (userValue != 0))
            midi::Tx802HighLevel::sendButtonByName(*midiSender, "TG1", devId);
        cfg.tg[0].tgOnOff = midi::tgOnToString(userValue != 0);
        cfg.hasPerformanceParams = true;
        midi::Config::save(cfg);
        setTgLed(1, userValue != 0);
        updateLcdFromConfig();
        return;
    }

    int paramNum = -1;
    int internalValue = -1;
    bool twoByteVnum = false;

    if (paramName == "TG")
    {
        paramNum = i;
        internalValue = (userValue != 0) ? i : 0;
        cfg.tg[i].tgOnOff = midi::tgOnToString(userValue != 0);
    }
    else if (paramName == "PRESET")
    {
        paramNum = 16 + i;
        internalValue = userValue - 1;
        twoByteVnum = true;
        cfg.tg[i].preset = midi::vnumToPreset(userValue);
    }
    else if (paramName == "RXCH")
    {
        paramNum = 8 + i;
        internalValue = (userValue == 17) ? 16 : userValue - 1;
        cfg.tg[i].rxch = midi::rxchToString(userValue);
    }
    else if (paramName == "NOTELOW")
    {
        paramNum = 48 + i;
        internalValue = userValue;
        cfg.tg[i].noteLow = midi::midiToNoteName(userValue);
    }
    else if (paramName == "NOTEHIGH")
    {
        paramNum = 56 + i;
        internalValue = userValue;
        cfg.tg[i].noteHigh = midi::midiToNoteName(userValue);
    }
    else if (paramName == "DETUNE")
    {
        paramNum = 24 + i;
        internalValue = userValue + 7;
        cfg.tg[i].detune = juce::String(userValue);
    }
    else if (paramName == "NOTESHIFT")
    {
        paramNum = 64 + i;
        internalValue = userValue + 24;
        cfg.tg[i].noteShift = juce::String(userValue);
    }
    else if (paramName == "OUTVOL")
    {
        paramNum = 32 + i;
        internalValue = userValue;
        cfg.tg[i].outVol = juce::String(userValue);
    }
    else if (paramName == "PAN")
    {
        paramNum = 40 + i;
        internalValue = userValue;
        cfg.tg[i].pan = midi::outchToPan(userValue);
    }
    else if (paramName == "FDAMP")
    {
        paramNum = 72 + i;
        internalValue = userValue;
        cfg.tg[i].fDamp = midi::fdampToString(userValue);
    }

    if (paramNum < 0) return;

    juce::Array<juce::uint8> vals;
    if (twoByteVnum)
    {
        vals.add((juce::uint8)((internalValue >> 7) & 0x7F));
        vals.add((juce::uint8)(internalValue & 0x7F));
    }
    else
    {
        vals.add((juce::uint8)(internalValue & 0x7F));
    }

    bool ok = midiSender->sendPcedParamChange(devId, (juce::uint8) paramNum, vals);

    if (paramName == "TG")
    {
        // LINK param sent — LED reflects On/Off directly
        setTgLed(tg1to8, userValue != 0);
        // Device side effect: toggling any TG2-8 while TG1 is Off forces TG1 On,
        // regardless of direction (On or Off). Send code 89 immediately after to
        // restore TG1 Off. Device processes MIDI in order so no sleep needed.
        if (tg1to8 > 1 && ! midi::tgOnFromString(cfg.tg[0].tgOnOff))
            midi::Tx802HighLevel::sendButtonByName(*midiSender, "TG1", devId);
    }
    else if (paramName == "PRESET")
    {
        // VNUM sent — device activates TG as side effect, mirror that in the overlay
        setTgLed(tg1to8, true);
        if (! midi::tgOnFromString(cfg.tg[i].tgOnOff))
        {
            // TG is Off in config: send LINK=0 to re-silence it, then extinguish overlay
            juce::Thread::sleep(20);
            juce::Array<juce::uint8> offVal;
            offVal.add(0);
            midiSender->sendPcedParamChange(devId, (juce::uint8) i, offVal);
            setTgLed(tg1to8, false);
        }
    }

    cfg.hasPerformanceParams = true;
    midi::Config::save(cfg);

    if (paramName == "TG")
        updateLcdFromConfig();
    else if ((paramName == "PRESET" || paramName == "RXCH") && i == selectedTg)
        setLcdLine(0, buildVoiceSelectLine());

    juce::ignoreUnused(ok);
}

void MainComponent::setTgLed(int tg1to8, bool on)
{
    const int i = tg1to8 - 1;
    if (juce::isPositiveAndBelow(i, 8))
    {
        tgLedOverlay[i].setVisible(on);
        tgLedOverlay[i].repaint();
    }
}

void MainComponent::setLcdLine(int line0or1, const juce::String& text)
{
    lcdDisplay.setLine(line0or1, text);
}

juce::String MainComponent::buildLinkLine() const
{
    midi::ConfigState cfg;
    midi::Config::load(cfg);
    // Format: 1 leading space + (3-char slot + 2-char gap) × 7 + 3-char slot + 1 trailing space = 40 chars
    juce::String line = " ";
    for (int i = 0; i < 8; ++i)
    {
        if (i > 0) line += "  ";
        // TG1 is always On on the device — never show "<--" for it
        bool on = (i == 0) ? true : midi::tgOnFromString(cfg.tg[i].tgOnOff);
        line += on ? ("I0" + juce::String(i + 1)) : "<--";
    }
    line += " ";
    return line;
}

juce::String MainComponent::buildVoiceSelectLine() const
{
    if (selectedTg < 0) return {};

    midi::ConfigState cfg;
    midi::Config::load(cfg);
    const auto& t = cfg.tg[selectedTg];

    // Patch name from presetBankNames (already without I0n prefix); fallback to slot string
    int slot = midi::presetToVnum(t.preset);  // 1-32
    juce::String name;
    if (slot >= 1 && slot - 1 < cfg.presetBankNames.size())
        name = cfg.presetBankNames[slot - 1];
    if (name.isEmpty() || name == "empty")
        name = t.preset;  // e.g. "I01"
    name = "<" + name.substring(0, 10).paddedRight(' ', 10) + ">";

    // Channel: " 1"–" 9" (leading space for single digit), "10"–"16", or " A" for Omni
    int ch = midi::rxchFromString(t.rxch);  // 1–16 or 17 (Omni)
    juce::String chStr = (ch == 17) ? " A"
                       : (ch  < 10) ? (" " + juce::String(ch))
                                    : juce::String(ch);

    return "VOICE SELECT       " + name + " Rch=" + chStr;
}

void MainComponent::updateLcdFromConfig()
{
    setLcdLine(0, buildVoiceSelectLine());
    setLcdLine(1, buildLinkLine());
}

void MainComponent::selectTg(int i)
{
    lpSaveBrowserState();
    for (int j = 0; j < 8; ++j)
        setLpRowVisible(j, j == i);
    selectedTg = i;
    setLcdLine(0, buildVoiceSelectLine());
    lpRestoreBrowserState(i);
    lpRefreshPresets();
    lpUpdateOverlay();
}

void MainComponent::setLpRowVisible(int tg0based, bool visible)
{
    lpTgNum[tg0based].setVisible(visible);
    lpTgOnOff[tg0based].setVisible(visible);
    lpTgPreset[tg0based].setVisible(visible);
    lpTgRxCh[tg0based].setVisible(visible);
    lpTgNoteLow[tg0based].setVisible(visible);
    lpTgNoteHigh[tg0based].setVisible(visible);
    lpTgDetune[tg0based].setVisible(visible);
    lpTgShift[tg0based].setVisible(visible);
    lpTgVol[tg0based].setVisible(visible);
    lpTgOut[tg0based].setVisible(visible);
    lpTgDamp[tg0based].setVisible(visible);
}

// ── LP Preset Browser — methods ──

void MainComponent::lpRefreshPresets()
{
    if (selectedTg < 0 || ! presetsDb)
    {
        lpPresetList.updateContent();
        lpBrowserStatusLabel.setText({}, juce::dontSendNotification);
        return;
    }

    auto& state = lpBrowserState[selectedTg];
    state.currentPage = lpCurrentPage;

    juce::String err;
    int total = 0;
    int offset = (lpCurrentPage - 1) * pageSize;
    state.currentRows = presetsDb->queryPresets(
        lpFilterEdit.getText(), lpRatingFilterCombo.getText(),
        pageSize, offset, total, err);
    state.totalRows = total;

    lpPresetList.updateContent();
    lpPresetList.repaint();
    lpPresetHeader.repaint();
    lpBrowserStatusLabel.setText(
        "Rows: " + juce::String(total) + "  Page " + juce::String(lpCurrentPage),
        juce::dontSendNotification);
}

void MainComponent::lpPresetItemClicked(int row)
{
    if (selectedTg < 0) return;
    const auto& state = lpBrowserState[selectedTg];
    if (! juce::isPositiveAndBelow(row, state.currentRows.size())) return;
    const auto& r = state.currentRows[row];
    // Place the preset into the bank slot corresponding to the selected TG (0-based)
    if (juce::isPositiveAndBelow(selectedTg, bankSlotIds.size()))
        setBankSlotFromPreset(selectedTg, r.id, r.presetName);

    // Auto-send: if the toggle is on, send immediately as if the user clicked Send
    if (lpAutoSendToggle.getToggleState())
        sendBankToDevice();
}

void MainComponent::lpSaveBrowserState()
{
    if (selectedTg < 0) return;
    auto& state = lpBrowserState[selectedTg];
    state.filterText  = lpFilterEdit.getText();
    state.ratingId    = lpRatingFilterCombo.getSelectedId();
    state.currentPage = lpCurrentPage;
    // currentRows and totalRows are already up-to-date in the state struct
}

void MainComponent::lpRestoreBrowserState(int tg0based)
{
    const auto& state = lpBrowserState[tg0based];
    lpFilterEdit.setText(state.filterText, juce::dontSendNotification);
    lpRatingFilterCombo.setSelectedId(
        state.ratingId > 0 ? state.ratingId : 1, juce::dontSendNotification);
    lpCurrentPage = state.currentPage;
    // Rows will be (re)loaded by lpRefreshPresets() called after this
}

void MainComponent::lpUpdateOverlay()
{
    if (startupInProgress)      { lpBrowserOverlay.setVisible(true);  return; }
    if (selectedTg < 0)         { lpBrowserOverlay.setVisible(false); return; }
    midi::ConfigState cfg;
    midi::Config::load(cfg);
    bool tgOn = midi::tgOnFromString(cfg.tg[selectedTg].tgOnOff);
    lpBrowserOverlay.setVisible(! tgOn);
}

void MainComponent::refreshPerfPresetDropdowns()
{
    midi::ConfigState cfg;
    midi::Config::load(cfg);

    for (int i = 0; i < 8; ++i)
    {
        const int lpId = lpTgPreset[i].getSelectedId();
        lpTgPreset[i].clear(juce::dontSendNotification);
        for (int p = 1; p <= 32; ++p)
        {
            juce::String label = "I" + juce::String(p).paddedLeft('0', 2);
            if (p - 1 < cfg.presetBankNames.size())
            {
                auto name = cfg.presetBankNames[p - 1];
                if (name.isNotEmpty() && name != "empty")
                    label += " " + name;
            }
            lpTgPreset[i].addItem(label, p);
        }
        lpTgPreset[i].setSelectedId(lpId, juce::dontSendNotification);
    }
}
