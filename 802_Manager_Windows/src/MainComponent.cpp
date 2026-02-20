#include "MainComponent.h"
#include "core/Dx7Utils.h"
#include "midi/MidiSender.h"
#include "midi/Tx802HighLevel.h"
#include "midi/Config.h"
#include "PanelLayout.h"
#include <BinaryData.h>

using core::Dx7Utils;

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
    if (startupThread)
    {
        startupThread->stopThread(8000);
        startupThread.reset();
    }
}

void MainComponent::StartupThread::run()
{
    midi::Tx802HighLevel::sendStartupSequence(sender, (juce::uint8) deviceId);
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
}

MainComponent::MainComponent()
{
    midi::StartupLog::write("=== MainComponent CTOR BEGIN ===");
    addAndMakeVisible(tabs);

    midi::StartupLog::write("CTOR: Preset Browser tab setup");
    // ── Preset Browser tab ──
    presetBrowserTab.addAndMakeVisible(openBankButton);
    presetBrowserTab.addAndMakeVisible(openVoiceButton);
    presetBrowserTab.addAndMakeVisible(filterLabel);
    presetBrowserTab.addAndMakeVisible(filterEdit);
    presetBrowserTab.addAndMakeVisible(ratingLabel);
    presetBrowserTab.addAndMakeVisible(ratingFilterCombo);
    presetBrowserTab.addAndMakeVisible(selectDbButton);
    presetBrowserTab.addAndMakeVisible(dbPathLabel);
    presetBrowserTab.addAndMakeVisible(presetHeader);
    presetBrowserTab.addAndMakeVisible(presetList);
    presetBrowserTab.addAndMakeVisible(bankList);
    presetBrowserTab.addAndMakeVisible(bankHeader);
    presetBrowserTab.addAndMakeVisible(initBankButton);
    presetBrowserTab.addAndMakeVisible(randomizeBankButton);
    presetBrowserTab.addAndMakeVisible(sendBankButton);
    bankNote.setFont(juce::Font(11.0f));
    bankNote.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    presetBrowserTab.addAndMakeVisible(bankNote);

    initBankButton.onClick = [this]{ initBank(); };
    randomizeBankButton.onClick = [this]{ randomizeBank(); };
    sendBankButton.onClick = [this]{ sendBankToDevice(); };

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
    bankList.setModel(&bankModel);
    bankList.setMultipleSelectionEnabled(false);
    bankList.setRowHeight(22);
    bankList.setColour(juce::ListBox::backgroundColourId, juce::Colours::black.withAlpha(0.15f));
    presetList.addMouseListener(this, true);

    presetBrowserTab.addAndMakeVisible(firstPage);
    presetBrowserTab.addAndMakeVisible(prevPage);
    presetBrowserTab.addAndMakeVisible(nextPage);
    presetBrowserTab.addAndMakeVisible(lastPage);
    presetBrowserTab.addAndMakeVisible(statusLabel);

    midi::StartupLog::write("CTOR: Opening DB");
    // Open DB
    midi::ConfigState cfgInitial; midi::Config::load(cfgInitial);
    juce::File dbFile = cfgInitial.dbPath.isNotEmpty() ? juce::File(cfgInitial.dbPath)
                                                       : juce::File::getCurrentWorkingDirectory().getChildFile("config/dx_preset_library.sqlite3");
    presetsDb = std::make_unique<storage::PresetsDb>(dbFile);
    presetsDb->open();
    dbPathLabel.setText(dbFile.getFullPathName(), juce::dontSendNotification);

    // Measure average character width
    {
        juce::Font f(13.0f);
        const juce::String sampleChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        const float sampleWidth = f.getStringWidthFloat(sampleChars);
        avgCharWidthPx = (sampleChars.isNotEmpty() ? sampleWidth / (float) sampleChars.length() : 8.0f);
        colIdWidth = juce::jmax(20, (int) juce::roundToInt(colIdWidth - 2 * avgCharWidthPx));
        colNameWidth = juce::jmax(40, (int) juce::roundToInt(colNameWidth - 3 * avgCharWidthPx));
        colCategoryWidth = colNameWidth;
        colCommentsWidth = colCategoryWidth * 2;
        colRatingWidth = 48;
    }

    auto refreshPage = [this]
    {
        juce::String err;
        int total = 0;
        int offset = (currentPage - 1) * pageSize;
        auto rows = presetsDb->queryPresets(filterEdit.getText(), ratingFilterCombo.getText(), pageSize, offset, total, err);
        totalRows = total;
        currentRows = rows;
        presetNames.clear();
        for (int i = 0; i < rows.size(); ++i) presetNames.add("");
        presetList.updateContent();
        presetList.setRowHeight(22);
        presetList.repaint();
        presetHeader.repaint();
        statusLabel.setText("Rows: " + juce::String(totalRows) + "  Page " + juce::String(currentPage), juce::dontSendNotification);
        repaint();
    };

    firstPage.onClick = [this, refreshPage]{ currentPage = 1; refreshPage(); };
    prevPage.onClick  = [this, refreshPage]{ if (currentPage > 1) { --currentPage; refreshPage(); } };
    nextPage.onClick  = [this, refreshPage]{
        int maxPage = juce::jmax(1, (totalRows + pageSize - 1) / pageSize);
        if (currentPage < maxPage) { ++currentPage; refreshPage(); }
    };
    lastPage.onClick  = [this, refreshPage]{
        int maxPage = juce::jmax(1, (totalRows + pageSize - 1) / pageSize);
        currentPage = maxPage; refreshPage();
    };
    filterEdit.onTextChange = [this, refreshPage]{ currentPage = 1; refreshPage(); };
    ratingFilterCombo.onChange = [this, refreshPage]{ currentPage = 1; refreshPage(); };

    {
        ratingFilterCombo.clear();
        ratingFilterCombo.addItem("Any", 1);
        ratingFilterCombo.addItem("Unrated", 2);
        for (int r = 1; r <= 10; ++r) ratingFilterCombo.addItem(juce::String(r), 100 + r);
        ratingFilterCombo.setText("Any", juce::dontSendNotification);
    }

    selectDbButton.onClick = [this, refreshPage]
    {
        dbFileChooser.reset(new juce::FileChooser("Select preset DB file", juce::File(), "*.sqlite3;*.db;*"));
        dbFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [this, refreshPage](const juce::FileChooser& ch)
        {
            auto f = ch.getResult();
            if (f.existsAsFile())
            {
                presetsDb.reset(new storage::PresetsDb(f));
                if (presetsDb->open())
                {
                    dbPathLabel.setText(f.getFullPathName(), juce::dontSendNotification);
                    midi::ConfigState cfg; midi::Config::load(cfg); cfg.dbPath = f.getFullPathName(); midi::Config::save(cfg);
                    currentPage = 1; refreshPage();
                }
                else
                    statusLabel.setText("Failed to open DB", juce::dontSendNotification);
            }
        });
    };

    midi::StartupLog::write("CTOR: Performance Editor tab setup");
    // ── Performance Editor tab ──
    {
        // Header labels
        for (auto* lbl : { &perfHdrTg, &perfHdrOnOff, &perfHdrPrst, &perfHdrChan,
                           &perfHdrLow, &perfHdrHigh, &perfHdrDet, &perfHdrShft,
                           &perfHdrVol, &perfHdrOut, &perfHdrDamp })
        {
            lbl->setFont(juce::Font(12.0f, juce::Font::bold));
            lbl->setJustificationType(juce::Justification::centred);
            performanceEditorTab.addAndMakeVisible(lbl);
        }
        performanceEditorTab.addAndMakeVisible(perfStatus);

        midi::ConfigState perfCfg;
        midi::Config::load(perfCfg);

        for (int i = 0; i < 8; ++i)
        {
            const int tg = i + 1;
            const auto& t = perfCfg.hasPerformanceParams ? perfCfg.tg[i] : midi::TgState();

            perfTgNum[i].setText(juce::String(tg), juce::dontSendNotification);
            perfTgNum[i].setJustificationType(juce::Justification::centred);
            perfTgNum[i].setFont(juce::Font(13.0f, juce::Font::bold));
            performanceEditorTab.addAndMakeVisible(perfTgNum[i]);

            perfTgOnOff[i].addItem("Off", 1);
            perfTgOnOff[i].addItem("On",  2);
            perfTgOnOff[i].setSelectedId(midi::tgOnFromString(t.tgOnOff) ? 2 : 1, juce::dontSendNotification);
            performanceEditorTab.addAndMakeVisible(perfTgOnOff[i]);
            perfTgOnOff[i].onChange = [this, tg, i]{ sendPerfParam(tg, "TG", perfTgOnOff[i].getSelectedId() == 2 ? 1 : 0); };

            for (int p = 1; p <= 32; ++p)
                perfTgPreset[i].addItem("I" + juce::String(p).paddedLeft('0', 2), p);
            perfTgPreset[i].setSelectedId(juce::jlimit(1, 32, midi::presetToVnum(t.preset)), juce::dontSendNotification);
            performanceEditorTab.addAndMakeVisible(perfTgPreset[i]);
            perfTgPreset[i].onChange = [this, tg, i]{ sendPerfParam(tg, "PRESET", perfTgPreset[i].getSelectedId()); };

            for (int c = 1; c <= 16; ++c) perfTgRxCh[i].addItem(juce::String(c), c);
            perfTgRxCh[i].addItem("Omni", 17);
            perfTgRxCh[i].setSelectedId(juce::jlimit(1, 17, midi::rxchFromString(t.rxch)), juce::dontSendNotification);
            performanceEditorTab.addAndMakeVisible(perfTgRxCh[i]);
            perfTgRxCh[i].onChange = [this, tg, i]{ sendPerfParam(tg, "RXCH", perfTgRxCh[i].getSelectedId()); };

            perfTgNoteLow[i].setRange(0, 127, 1);
            perfTgNoteLow[i].setValue(midi::noteNameToMidi(t.noteLow), juce::dontSendNotification);
            perfTgNoteLow[i].setSliderStyle(juce::Slider::IncDecButtons);
            perfTgNoteLow[i].setTextBoxStyle(juce::Slider::TextBoxLeft, false, 36, 20);
            performanceEditorTab.addAndMakeVisible(perfTgNoteLow[i]);
            perfTgNoteLow[i].onValueChange = [this, tg, i]{ sendPerfParam(tg, "NOTELOW", (int) perfTgNoteLow[i].getValue()); };

            perfTgNoteHigh[i].setRange(0, 127, 1);
            perfTgNoteHigh[i].setValue(midi::noteNameToMidi(t.noteHigh), juce::dontSendNotification);
            perfTgNoteHigh[i].setSliderStyle(juce::Slider::IncDecButtons);
            perfTgNoteHigh[i].setTextBoxStyle(juce::Slider::TextBoxLeft, false, 36, 20);
            performanceEditorTab.addAndMakeVisible(perfTgNoteHigh[i]);
            perfTgNoteHigh[i].onValueChange = [this, tg, i]{ sendPerfParam(tg, "NOTEHIGH", (int) perfTgNoteHigh[i].getValue()); };

            perfTgDetune[i].setRange(-7, 7, 1);
            perfTgDetune[i].setValue(t.detune.getIntValue(), juce::dontSendNotification);
            perfTgDetune[i].setSliderStyle(juce::Slider::IncDecButtons);
            perfTgDetune[i].setTextBoxStyle(juce::Slider::TextBoxLeft, false, 30, 20);
            performanceEditorTab.addAndMakeVisible(perfTgDetune[i]);
            perfTgDetune[i].onValueChange = [this, tg, i]{ sendPerfParam(tg, "DETUNE", (int) perfTgDetune[i].getValue()); };

            perfTgShift[i].setRange(-24, 24, 1);
            perfTgShift[i].setValue(t.noteShift.getIntValue(), juce::dontSendNotification);
            perfTgShift[i].setSliderStyle(juce::Slider::IncDecButtons);
            perfTgShift[i].setTextBoxStyle(juce::Slider::TextBoxLeft, false, 30, 20);
            performanceEditorTab.addAndMakeVisible(perfTgShift[i]);
            perfTgShift[i].onValueChange = [this, tg, i]{ sendPerfParam(tg, "NOTESHIFT", (int) perfTgShift[i].getValue()); };

            perfTgVol[i].setRange(0, 99, 1);
            perfTgVol[i].setValue(t.outVol.getIntValue(), juce::dontSendNotification);
            perfTgVol[i].setSliderStyle(juce::Slider::IncDecButtons);
            perfTgVol[i].setTextBoxStyle(juce::Slider::TextBoxLeft, false, 30, 20);
            performanceEditorTab.addAndMakeVisible(perfTgVol[i]);
            perfTgVol[i].onValueChange = [this, tg, i]{ sendPerfParam(tg, "OUTVOL", (int) perfTgVol[i].getValue()); };

            perfTgOut[i].addItem("Off", 1); perfTgOut[i].addItem("Left", 2);
            perfTgOut[i].addItem("Right", 3); perfTgOut[i].addItem("Center", 4);
            perfTgOut[i].setSelectedId(midi::panToOutch(t.pan) + 1, juce::dontSendNotification);
            performanceEditorTab.addAndMakeVisible(perfTgOut[i]);
            perfTgOut[i].onChange = [this, tg, i]{ sendPerfParam(tg, "PAN", perfTgOut[i].getSelectedId() - 1); };

            perfTgDamp[i].addItem("Off", 1); perfTgDamp[i].addItem("On", 2);
            perfTgDamp[i].setSelectedId(midi::fdampFromString(t.fDamp) == 0 ? 1 : 2, juce::dontSendNotification);
            performanceEditorTab.addAndMakeVisible(perfTgDamp[i]);
            perfTgDamp[i].onChange = [this, tg, i]{ sendPerfParam(tg, "FDAMP", perfTgDamp[i].getSelectedId() == 2 ? 1 : 0); };
        }
    }
    refreshPerfPresetDropdowns();

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
    // TODO: TG1-TG8 belong to TX802-Panel-Left.png — add and make visible when implementing left panel
    for (int i = 1; i <= 8; ++i)
        fpTgButtons.add(new juce::TextButton("TG" + juce::String(i)));
    // TODO: RESET — no physical panel button; implement as software action later
    // TODO: lowercase / UPPERCASE — text-entry helpers, no physical buttons; implement later
    // TODO: PRTCT OFF / PRTCT ON / POS1 — software macros; implement later
    // TODO: Play Notes — software test utility; implement later

    midi::StartupLog::write("CTOR: Settings tab setup");
    // ── Settings tab ──
    settingsTab.addAndMakeVisible(midiOutputCombo);
    settingsTab.addAndMakeVisible(midiInputCombo);
    settHdrMidi.setFont(juce::Font(15.0f, juce::Font::bold));
    settHdrDevice.setFont(juce::Font(15.0f, juce::Font::bold));
    settHdrSysex.setFont(juce::Font(15.0f, juce::Font::bold));
    settingsTab.addAndMakeVisible(settHdrMidi);
    settingsTab.addAndMakeVisible(settHdrDevice);
    settingsTab.addAndMakeVisible(settHdrSysex);
    settingsTab.addAndMakeVisible(settLblInput);
    settingsTab.addAndMakeVisible(settLblOutput);
    settingsTab.addAndMakeVisible(refreshMidiButton);
    settingsTab.addAndMakeVisible(forwardingToggle);
    midiStatusBox.setMultiLine(false);
    midiStatusBox.setReadOnly(true);
    midiStatusBox.setCaretVisible(false);
    midiStatusBox.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha(0.3f));
    midiStatusBox.setColour(juce::TextEditor::outlineColourId, juce::Colours::grey);
    midiStatusBox.setColour(juce::TextEditor::textColourId, juce::Colours::lightgreen);
    settingsTab.addAndMakeVisible(midiStatusBox);
    settingsTab.addAndMakeVisible(rebootButton);
    settingsTab.addAndMakeVisible(prepareBtn);
    settingsTab.addAndMakeVisible(playTest);
    settingsTab.addAndMakeVisible(panicBtn);
    settingsTab.addAndMakeVisible(devIdLabel);
    settingsTab.addAndMakeVisible(devIdSlider);
    settingsTab.addAndMakeVisible(chunkLabel);
    settingsTab.addAndMakeVisible(chunkSlider);
    settingsTab.addAndMakeVisible(delayLabel);
    settingsTab.addAndMakeVisible(delaySlider);
    settingsTab.addAndMakeVisible(patchesLabel);
    settingsTab.addAndMakeVisible(patchesCombo);

    midi::StartupLog::write("CTOR: Wiring callbacks");
    // ── Wiring ──
    openBankButton.onClick = [this] { openDx7Bank(); };
    openVoiceButton.onClick = [this] { openSingleVoice(); };
    refreshMidiButton.onClick = [this] { rebuildMidiOutputs(); };
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
    // TODO: wire TG1-TG8 onClick when implementing left panel tab
    for (int i = 0; i < fpTgButtons.size(); ++i)
    {
        fpTgButtons[i]->onClick = [this, sendBtn, i]
        {
            sendBtn("TG" + juce::String(i + 1));
            frontStatus.setText("Sent TG" + juce::String(i + 1), juce::dontSendNotification);
        };
    }
    // TODO: RESET — no physical panel button; wire as software action later
    fpReset.onClick = [sendBtn]{ sendBtn("RESET"); };
    // TODO: lowercase / UPPERCASE — text-entry helpers; wire to physical buttons or menu later
    fpLower.onClick  = [sendBtn]{ sendBtn("LOWERCASE"); };
    fpUpper.onClick  = [sendBtn]{ sendBtn("UPPERCASE"); };
    // TODO: PRTCT OFF / PRTCT ON / POS1 — software macros; expose via menu or toolbar later
    fpPrtctOff.onClick = [this]
    {
        if (midiSender && midiSender->isOpen())
            midi::Tx802HighLevel::sendMacroByName(*midiSender, "PRTCT_OFF", 1);
        frontStatus.setText("Sent PRTCT OFF", juce::dontSendNotification);
    };
    fpPrtctOn.onClick = [this]
    {
        if (midiSender && midiSender->isOpen())
            midi::Tx802HighLevel::sendMacroByName(*midiSender, "PRTCT_ON", 1);
        frontStatus.setText("Sent PRTCT ON", juce::dontSendNotification);
    };
    fpPos1.onClick = [this]
    {
        if (midiSender && midiSender->isOpen())
            midi::Tx802HighLevel::sendMacroByName(*midiSender, "POS1", 1);
        frontStatus.setText("Sent POS1", juce::dontSendNotification);
    };
    // TODO: Play Notes — software test utility; expose via toolbar or menu later
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
    {
        auto inNames = juce::MidiInput::getDevices();
        for (int i = 0; i < inNames.size(); ++i)
            midiInputCombo.addItem(inNames[i], i + 1);
    }
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
                    });
                    startupThread->startThread();
                }
                if (cfg.inputPort.isNotEmpty())
                {
                    midiInputCombo.setText(cfg.inputPort, juce::dontSendNotification);
                    if (cfg.forwardingEnabled && midiSender->isOpen())
                    {
                        forwardingToggle.setToggleState(true, juce::dontSendNotification);
                        midiThru->start(midiInputCombo.getText(), midiSender.get());
                        midiStatusBox.setText("Forwarding ON (" + midiInputCombo.getText() + " \xe2\x86\x92 " + midiOutputCombo.getText() + ")");
                    }
                }
            });
        }
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
                    });
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
            bankList.updateContent();
            bankList.repaint();
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
                    });
            startupThread->startThread();
        }
        else midiStatusBox.setText("Open MIDI Out first");
    };

    midi::StartupLog::write("CTOR: Adding tabs");
    // ── Tabs ──
    tabs.addTab("Preset Browser",       juce::Colours::darkgrey, &presetBrowserTab,       false);
    tabs.addTab("Performance Editor",   juce::Colours::darkgrey, &performanceEditorTab,   false);
    tabs.addTab("Left Panel",           juce::Colours::darkgrey, &leftPanelTab,           false);
    tabs.addTab("Right Panel",          juce::Colours::darkgrey, &frontPanelTab,          false);
    tabs.addTab("Settings",             juce::Colours::darkgrey, &settingsTab,            false);

    midi::StartupLog::write("CTOR: setSize + refreshPage");
    {
        auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        float scale = disp ? (float)disp->scale : 1.0f;
        setSize(juce::roundToInt(PanelLayout::kPanelWidth / scale), 964);
    }
    refreshPage();
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

    // ── Preset Browser layout ──
    auto pb = presetBrowserTab.getLocalBounds().reduced(8);
    auto pbTop = pb.removeFromTop(28);
    openBankButton.setBounds(pbTop.removeFromLeft(180));
    pbTop.removeFromLeft(8);
    openVoiceButton.setBounds(pbTop.removeFromLeft(180));
    pbTop.removeFromLeft(8);
    selectDbButton.setBounds(pbTop.removeFromLeft(120));
    pbTop.removeFromLeft(8);
    dbPathLabel.setBounds(pbTop);
    pb.removeFromTop(4);

    auto pbFilter = pb.removeFromTop(24);
    filterLabel.setBounds(pbFilter.removeFromLeft(60));
    filterEdit.setBounds(pbFilter.removeFromLeft(200));
    pbFilter.removeFromLeft(12);
    ratingLabel.setBounds(pbFilter.removeFromLeft(60));
    ratingFilterCombo.setBounds(pbFilter.removeFromLeft(100));
    pb.removeFromTop(4);

    auto pbDb = pb.removeFromTop(22);
    // Navigation buttons
    {
        auto navRow1 = pbDb.removeFromLeft(200);
        firstPage.setBounds(navRow1.removeFromLeft(48));
        navRow1.removeFromLeft(4);
        prevPage.setBounds(navRow1.removeFromLeft(48));
        navRow1.removeFromLeft(4);
        nextPage.setBounds(navRow1.removeFromLeft(48));
        navRow1.removeFromLeft(4);
        lastPage.setBounds(navRow1.removeFromLeft(48));
    }
    pbDb.removeFromLeft(8);
    statusLabel.setBounds(pbDb);
    pb.removeFromTop(4);

    // Left list (presets) + Right list (bank)
    {
        const int bankWidth = 300;
        auto headerArea = pb.removeFromTop(20);
        auto leftWidth = pb.getWidth() - bankWidth - 12;
        auto headerLeft = headerArea.removeFromLeft(leftWidth);
        presetHeader.setBounds(headerLeft);
        headerArea.removeFromLeft(12);
        auto headerRight = headerArea;
        bankHeader.setBounds(headerRight.removeFromLeft(60));
        headerRight.removeFromLeft(6);
        initBankButton.setBounds(headerRight.removeFromLeft(50).withHeight(18));
        headerRight.removeFromLeft(4);
        randomizeBankButton.setBounds(headerRight.removeFromLeft(65).withHeight(18));
        headerRight.removeFromLeft(4);
        sendBankButton.setBounds(headerRight.removeFromLeft(50).withHeight(18));

        auto leftArea = pb.removeFromLeft(leftWidth);
        presetList.setBounds(leftArea);
        pb.removeFromLeft(12);
        auto bankNoteArea = pb.removeFromBottom(18);
        bankNote.setBounds(bankNoteArea);
        bankList.setBounds(pb);
    }

    // ── Performance Editor layout ──
    {
        auto pe = performanceEditorTab.getLocalBounds().reduced(8);
        const int colW[] = { 30, 50, 120, 60, 70, 70, 60, 60, 60, 70, 55 };
        const int rowH = 28;
        const int hdrH = 22;
        const int gap = 2;

        auto hdrRow = pe.removeFromTop(hdrH);
        juce::Label* hdrs[] = { &perfHdrTg, &perfHdrOnOff, &perfHdrPrst, &perfHdrChan,
                                &perfHdrLow, &perfHdrHigh, &perfHdrDet, &perfHdrShft,
                                &perfHdrVol, &perfHdrOut, &perfHdrDamp };
        for (int c = 0; c < 11; ++c)
        {
            hdrs[c]->setBounds(hdrRow.removeFromLeft(colW[c]));
            hdrRow.removeFromLeft(gap);
        }
        pe.removeFromTop(2);

        for (int i = 0; i < 8; ++i)
        {
            auto row = pe.removeFromTop(rowH);
            perfTgNum[i].setBounds(row.removeFromLeft(colW[0]));      row.removeFromLeft(gap);
            perfTgOnOff[i].setBounds(row.removeFromLeft(colW[1]));    row.removeFromLeft(gap);
            perfTgPreset[i].setBounds(row.removeFromLeft(colW[2]));   row.removeFromLeft(gap);
            perfTgRxCh[i].setBounds(row.removeFromLeft(colW[3]));     row.removeFromLeft(gap);
            perfTgNoteLow[i].setBounds(row.removeFromLeft(colW[4]));  row.removeFromLeft(gap);
            perfTgNoteHigh[i].setBounds(row.removeFromLeft(colW[5])); row.removeFromLeft(gap);
            perfTgDetune[i].setBounds(row.removeFromLeft(colW[6]));   row.removeFromLeft(gap);
            perfTgShift[i].setBounds(row.removeFromLeft(colW[7]));    row.removeFromLeft(gap);
            perfTgVol[i].setBounds(row.removeFromLeft(colW[8]));      row.removeFromLeft(gap);
            perfTgOut[i].setBounds(row.removeFromLeft(colW[9]));      row.removeFromLeft(gap);
            perfTgDamp[i].setBounds(row.removeFromLeft(colW[10]));
            pe.removeFromTop(2);
        }
        perfStatus.setBounds(pe.removeFromTop(24));
    }

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

        // TODO: TG1-TG8 — left panel; position when implementing left panel tab
        // TODO: RESET, lowercase, UPPERCASE, PRTCT OFF/ON, POS1, Play Notes — add to toolbar/menu later
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

    // ── Settings tab layout ──
    {
        auto st = settingsTab.getLocalBounds().reduced(16);
        const int rowH = 28, labelH = 18, gap = 6, sectionGap = 14, comboW = 340, btnW = 150;

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

// ── ListBoxModel (Preset Browser) ──

int MainComponent::getNumRows()
{
    return presetNames.size();
}

void MainComponent::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow(row, currentRows.size())) return;
    auto r = currentRows[(int) row];

    if (selected)
        g.fillAll(juce::Colours::steelblue);
    else
        g.fillAll(row % 2 == 0 ? juce::Colours::transparentBlack : juce::Colour(0x10ffffff));

    g.setFont(juce::Font(13.0f));
    int x = 6;

    // ID
    g.setColour(juce::Colours::lightgrey);
    g.drawText(juce::String(r.id), x, 0, colIdWidth, height, juce::Justification::centredLeft);
    x += colIdWidth;
    g.setColour(juce::Colours::grey.withAlpha(0.3f));
    g.drawVerticalLine(x - 1, 0.0f, (float) height);

    // Patch name
    g.setColour(juce::Colours::white);
    g.drawText(r.presetName, x, 0, colNameWidth, height, juce::Justification::centredLeft);
    x += colNameWidth;
    g.setColour(juce::Colours::grey.withAlpha(0.3f));
    g.drawVerticalLine(x - 1, 0.0f, (float) height);

    // Category
    g.setColour(juce::Colours::lightgrey);
    g.drawText(r.category, x, 0, colCategoryWidth, height, juce::Justification::centredLeft);
    x += colCategoryWidth;
    g.setColour(juce::Colours::grey.withAlpha(0.3f));
    g.drawVerticalLine(x - 1, 0.0f, (float) height);

    // Comments (optional)
    if (showCommentsColumn)
    {
        g.setColour(juce::Colours::lightgrey);
        g.drawText(r.comments, x, 0, colCommentsWidth, height, juce::Justification::centredLeft);
        x += colCommentsWidth;
        g.setColour(juce::Colours::grey.withAlpha(0.3f));
        g.drawVerticalLine(x - 1, 0.0f, (float) height);
    }

    // Rating digit
    g.setColour(juce::Colours::yellow);
    g.drawText(r.rating > 0 ? juce::String(r.rating) : juce::String(""), x, 0, colRatingWidth - 16, height, juce::Justification::centredLeft);

    // Up/down triangles for rating
    {
        const float arrowX = (float)(x + colRatingWidth - 16);
        const float midY = (float) height * 0.5f;
        // Up triangle
        g.setColour(juce::Colours::lightgrey);
        juce::Path up;
        up.addTriangle(arrowX + 4.0f, midY - 2.0f, arrowX + 10.0f, midY - 2.0f, arrowX + 7.0f, midY - 8.0f);
        g.fillPath(up);
        // Down triangle
        juce::Path dn;
        dn.addTriangle(arrowX + 4.0f, midY + 2.0f, arrowX + 10.0f, midY + 2.0f, arrowX + 7.0f, midY + 8.0f);
        g.fillPath(dn);
    }
    x += colRatingWidth;
    g.setColour(juce::Colours::grey.withAlpha(0.3f));
    g.drawVerticalLine(x - 1, 0.0f, (float) height);
}

void MainComponent::selectedRowsChanged(int) {}

juce::Component* MainComponent::refreshComponentForRow(int, bool, juce::Component* existing)
{
    // We use paintListBoxItem + click handling, no per-row components needed
    return existing;
}

void MainComponent::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow(row, currentRows.size())) return;
    auto r = currentRows[(int) row];

    // Check if click is in the rating arrow area
    auto rowBounds = presetList.getRowPosition(row, true);
    int ratingX = colIdWidth + colNameWidth + colCategoryWidth + (showCommentsColumn ? colCommentsWidth : 0);
    int relX = e.getPosition().x;
    if (relX >= ratingX + colRatingWidth - 16 && relX < ratingX + colRatingWidth)
    {
        int relY = e.getPosition().y - rowBounds.getY();
        int midY = rowBounds.getHeight() / 2;
        if (relY < midY)
            changeRatingForRow(row, 1);
        else
            changeRatingForRow(row, -1);
        return;
    }

    // Otherwise send the preset to device
    const int presetId = r.id;
    if (presetId <= 0) { statusLabel.setText("Invalid selection", juce::dontSendNotification); return; }
    if (presetsDb)
    {
        juce::MemoryBlock syx; juce::String err;
        if (! midiSender || ! midiSender->isOpen())
        {
            statusLabel.setText("Select MIDI Output first", juce::dontSendNotification);
            return;
        }
        if (presetsDb->getSysexById(presetId, syx, err))
        {
            midiSender->sendSysex(syx);
            midi::ConfigState cfg; midi::Config::load(cfg);
            midi::Tx802HighLevel::sendButtonByName(*midiSender, "VOICE_SELECT", (juce::uint8) cfg.deviceId);
            statusLabel.setText("Sent preset ID " + juce::String(presetId), juce::dontSendNotification);
        }
        else
            statusLabel.setText("DB fetch failed: " + err, juce::dontSendNotification);
    }
}

// ── Preset Header ──
void MainComponent::PresetHeader::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();
    g.fillAll(juce::Colours::darkgrey.darker(0.3f));
    g.setColour(juce::Colours::white);

    int x = 6;
    const int h = area.getHeight();
    g.drawText("ID",       x, 0, owner.colIdWidth,       h, juce::Justification::centredLeft); x += owner.colIdWidth;
    g.drawText("Patch",    x, 0, owner.colNameWidth,     h, juce::Justification::centredLeft); x += owner.colNameWidth;
    g.drawText("Category", x, 0, owner.colCategoryWidth, h, juce::Justification::centredLeft); x += owner.colCategoryWidth;
    if (owner.showCommentsColumn)
    {
        g.drawText("Comments", x, 0, owner.colCommentsWidth, h, juce::Justification::centredLeft);
        x += owner.colCommentsWidth;
    }
    g.drawText("Rt", x, 0, owner.colRatingWidth, h, juce::Justification::centredLeft);
}

// ── Rating change ──
void MainComponent::changeRatingForRow(int rowIndex, int delta)
{
    if (! juce::isPositiveAndBelow(rowIndex, currentRows.size())) return;
    auto& r = currentRows.getReference(rowIndex);
    int newRating = juce::jlimit(0, 10, r.rating + delta);
    if (newRating == r.rating) return;
    r.rating = newRating;
    if (presetsDb)
    {
        juce::String err;
        presetsDb->updateRating(r.id, newRating, err);
        if (err.isNotEmpty())
            statusLabel.setText("Rating update failed: " + err, juce::dontSendNotification);
    }
    presetList.repaintRow(rowIndex);
}

// ── Bank slot DnD components ──

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
    if (desc.startsWith("preset:"))
    {
        int srcRow = desc.fromFirstOccurrenceOf("preset:", false, false).getIntValue();
        if (juce::isPositiveAndBelow(srcRow, owner.currentRows.size()))
        {
            auto r = owner.currentRows[(int) srcRow];
            owner.setBankSlotFromPreset(index, r.id, r.presetName);
        }
    }
    else if (desc.startsWith("bank:"))
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

// ── Drag from preset list ──
void MainComponent::mouseDown(const juce::MouseEvent& e) { dragStartRow = presetList.getRowContainingPosition(e.getEventRelativeTo(&presetList).getPosition().x, e.getEventRelativeTo(&presetList).getPosition().y); dragging = false; }
void MainComponent::mouseUp(const juce::MouseEvent&) { dragStartRow = -1; dragging = false; }
void MainComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (dragStartRow >= 0 && !dragging && e.getDistanceFromDragStart() > 5)
    {
        dragging = true;
        beginDragFromPresetRow(dragStartRow);
    }
}
void MainComponent::beginDragFromPresetRow(int rowIndex)
{
    if (! juce::isPositiveAndBelow(rowIndex, currentRows.size())) return;
    juce::Image dragImg(juce::Image::ARGB, 160, 20, true);
    { juce::Graphics ig(dragImg); ig.setColour(juce::Colours::white.withAlpha(0.6f)); ig.fillAll(); ig.setColour(juce::Colours::black); ig.drawText(currentRows[rowIndex].presetName, 4, 0, 152, 20, juce::Justification::centredLeft); }
    startDragging(juce::var(juce::String("preset:") + juce::String(rowIndex)), &presetList, dragImg, true);
}

// ── Bank helpers ──
void MainComponent::setBankSlotFromPreset(int slotIndex, int presetId, const juce::String& name)
{
    if (! juce::isPositiveAndBelow(slotIndex, bankSlotIds.size())) return;
    bankSlotIds.set(slotIndex, presetId);
    bankModel.setSlotName(slotIndex, name);
    bankList.updateContent();
    bankList.repaintRow(slotIndex);
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
    bankList.updateContent();
    bankList.repaint();
}

void MainComponent::initBank()
{
    for (int i = 0; i < visibleSlots; ++i)
    {
        bankSlotIds.set(i, 0);
        bankModel.setSlotName(i, "empty");
    }
    bankList.updateContent();
    bankList.repaint();
}

void MainComponent::randomizeBank()
{
    if (! presetsDb) return;
    juce::String err; int total = 0;
    auto all = presetsDb->queryPresets(filterEdit.getText(), ratingFilterCombo.getText(), 99999, 0, total, err);
    if (all.isEmpty()) { statusLabel.setText("No presets to randomize from", juce::dontSendNotification); return; }

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
    bankList.updateContent();
    bankList.repaint();
    statusLabel.setText("Randomized " + juce::String(juce::jmin(visibleSlots, all.size())) + " presets", juce::dontSendNotification);
}

void MainComponent::sendBankToDevice()
{
    if (! midiSender || ! midiSender->isOpen()) { statusLabel.setText("Open a MIDI output first", juce::dontSendNotification); return; }
    if (! presetsDb) { statusLabel.setText("No DB", juce::dontSendNotification); return; }

    const juce::uint8 deviceId = 1;
    midi::Tx802HighLevel::sendMacroByName(*midiSender, "PRTCT_OFF", deviceId);
    juce::Thread::sleep(120);

    midi::ConfigState cfgPts; midi::Config::load(cfgPts);
    const int patchCount = juce::jlimit(1, 32, cfgPts.patchesToSend);
    const bool isPartial = patchCount < 32;

    juce::MemoryBlock packed4096(32 * 128);
    auto* out = static_cast<juce::uint8*>(packed4096.getData());
    const auto initV128 = getInitVoice128();

    for (int i = 0; i < 32; ++i)
    {
        juce::MemoryBlock v128;
        const int id = bankSlotIds[i];
        if (id > 0)
        {
            juce::MemoryBlock syx; juce::String err; juce::String msg; juce::MemoryBlock v155;
            if (! presetsDb->getSysexById(id, syx, err) || ! core::Dx7Utils::verifySingleVoiceSysex(syx, msg, v155))
            { statusLabel.setText("Invalid slot " + juce::String(i+1) + ": " + (err.isNotEmpty()?err:msg), juce::dontSendNotification); return; }
            v128 = core::Dx7Utils::packSingleToBankVoice(v155);
        }
        else
        {
            v128 = initV128;
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
            statusLabel.setText("Bank validate failed: " + msg, juce::dontSendNotification);
    }

    {
        auto cfgFile = midi::Config::getConfigFile();
        auto dir = cfgFile.getParentDirectory();
        auto bankFile = dir.getChildFile("startup_bank.syx");
        const bool wrote = bankFile.replaceWithData(bank.getData(), (int) bank.getSize());
        statusLabel.setText(wrote ? juce::String("Wrote ") + bankFile.getFullPathName()
                                  : juce::String("Failed to write ") + bankFile.getFullPathName(),
                            juce::dontSendNotification);
    }

    if (isPartial)
    {
        // Partial transfer: send header + N voices + 4 bytes into next voice, no checksum/F7
        const int cutoffIndex = 6 + (patchCount * 128) + 4;
        juce::MemoryBlock truncated(bank.getData(), (size_t) cutoffIndex);
        DBG("Partial transfer: sending first " + juce::String(patchCount) + " voices (" + juce::String(cutoffIndex) + " bytes)");
        midi::ConfigState cfgP; midi::Config::load(cfgP);
        if (! midiSender->sendSysexPaced(truncated, cfgP.sysexChunkBytes, cfgP.sysexInterChunkMs))
            midiSender->sendSysex(truncated);
    }
    else
    {
        midi::ConfigState cfgP; midi::Config::load(cfgP);
        if (! midiSender->sendSysexPaced(bank, cfgP.sysexChunkBytes, cfgP.sysexInterChunkMs))
            midiSender->sendSysex(bank);
    }
    juce::Thread::sleep(150);
    midi::Tx802HighLevel::sendButtonByName(*midiSender, "VOICE_SELECT", deviceId);
    midi::Tx802HighLevel::sendButtonByName(*midiSender, "PLUS_ONE", deviceId);
    midi::Tx802HighLevel::sendButtonByName(*midiSender, "MINUS_ONE", deviceId);

    {
        midi::ConfigState cfgS; midi::Config::load(cfgS);
        cfgS.presetBankNames.clear();
        for (int i = 0; i < 32; ++i)
            cfgS.presetBankNames.add(bankModel.getSlotName(i));
        midi::Config::save(cfgS);
        refreshPerfPresetDropdowns();
    }
    statusLabel.setText(isPartial ? "Sent first " + juce::String(patchCount) + " voices (partial transfer)"
                                  : "Sent 32-voice bank (PRTCT_OFF, VOICE_SELECT, +/-)",
                        juce::dontSendNotification);
}

// ── Helpers ──

void MainComponent::deactivateModeButtons()
{
    for (auto* b : fpModeGroup) b->setActive(false);
}

void MainComponent::openDx7Bank()
{
    fileChooser.reset(new juce::FileChooser("Open DX7 Bank", juce::File(), "*.syx"));
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        juce::MemoryBlock data;
        f.loadFileAsData(data);
        juce::String msg;
        if (core::Dx7Utils::isValidDx7Bank(data, msg))
        {
            statusLabel.setText("Valid bank: " + f.getFileName(), juce::dontSendNotification);
        }
        else
            statusLabel.setText("Invalid bank: " + msg, juce::dontSendNotification);
    });
}

void MainComponent::openSingleVoice()
{
    fileChooser.reset(new juce::FileChooser("Open Single Voice", juce::File(), "*.syx"));
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        juce::MemoryBlock data;
        f.loadFileAsData(data);
        juce::String msg; juce::MemoryBlock v155;
        if (core::Dx7Utils::verifySingleVoiceSysex(data, msg, v155))
            statusLabel.setText("Valid single voice", juce::dontSendNotification);
        else
            statusLabel.setText("Invalid: " + msg, juce::dontSendNotification);
    });
}

void MainComponent::setStatus(const juce::String& text)
{
    statusLabel.setText(text, juce::dontSendNotification);
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
}

void MainComponent::refreshPresets() {}

// ── Performance Editor ──

void MainComponent::sendPerfParam(int tg1to8, const juce::String& paramName, int userValue)
{
    if (! midiSender || ! midiSender->isOpen())
    {
        perfStatus.setText("Open MIDI Out first", juce::dontSendNotification);
        return;
    }

    midi::ConfigState cfg;
    midi::Config::load(cfg);
    const juce::uint8 devId = (juce::uint8) cfg.deviceId;
    const int i = tg1to8 - 1;

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

    perfStatus.setText(ok ? (paramName + juce::String(tg1to8) + " = " + juce::String(userValue) + " sent")
                          : "Send failed", juce::dontSendNotification);
}

void MainComponent::updatePerfControlsFromConfig()
{
    midi::ConfigState cfg;
    if (! midi::Config::load(cfg) || ! cfg.hasPerformanceParams) return;

    for (int i = 0; i < 8; ++i)
    {
        const auto& t = cfg.tg[i];
        perfTgOnOff[i].setSelectedId(midi::tgOnFromString(t.tgOnOff) ? 2 : 1, juce::dontSendNotification);
        perfTgPreset[i].setSelectedId(juce::jlimit(1, 32, midi::presetToVnum(t.preset)), juce::dontSendNotification);
        perfTgRxCh[i].setSelectedId(juce::jlimit(1, 17, midi::rxchFromString(t.rxch)), juce::dontSendNotification);
        perfTgNoteLow[i].setValue(midi::noteNameToMidi(t.noteLow), juce::dontSendNotification);
        perfTgNoteHigh[i].setValue(midi::noteNameToMidi(t.noteHigh), juce::dontSendNotification);
        perfTgDetune[i].setValue(t.detune.getIntValue(), juce::dontSendNotification);
        perfTgShift[i].setValue(t.noteShift.getIntValue(), juce::dontSendNotification);
        perfTgVol[i].setValue(t.outVol.getIntValue(), juce::dontSendNotification);
        perfTgOut[i].setSelectedId(midi::panToOutch(t.pan) + 1, juce::dontSendNotification);
        perfTgDamp[i].setSelectedId(midi::fdampFromString(t.fDamp) == 0 ? 1 : 2, juce::dontSendNotification);
    }
    refreshPerfPresetDropdowns();
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

void MainComponent::refreshPerfPresetDropdowns()
{
    midi::ConfigState cfg;
    midi::Config::load(cfg);

    for (int i = 0; i < 8; ++i)
    {
        const int currentId = perfTgPreset[i].getSelectedId();
        perfTgPreset[i].clear(juce::dontSendNotification);
        for (int p = 1; p <= 32; ++p)
        {
            juce::String label = "I" + juce::String(p).paddedLeft('0', 2);
            if (p - 1 < cfg.presetBankNames.size())
            {
                auto name = cfg.presetBankNames[p - 1];
                if (name.isNotEmpty() && name != "empty")
                    label += " " + name;
            }
            perfTgPreset[i].addItem(label, p);
        }
        perfTgPreset[i].setSelectedId(currentId, juce::dontSendNotification);
    }
}
