#pragma once

#include <juce_core/juce_core.h>
#include "core/Tx802Utils.h"   // TgState + all conversion helpers now live in core

namespace midi {

// ─── Re-export core helpers into midi namespace for backward compatibility ─────
// These thin aliases let existing callers keep using midi::tgOnFromString() etc.
// New code should prefer core:: directly.

using core::midiToNoteName;
using core::noteNameToMidi;
using core::panToOutch;
using core::outchToPan;
using core::fdampFromString;
using core::fdampToString;
using core::tgOnFromString;
using core::tgOnToString;
using core::presetToVnum;
using core::vnumToPreset;
using core::rxchFromString;
using core::rxchToString;

// Re-export TgState so midi::TgState keeps working
using core::TgState;

// ─── ConfigState ─────────────────────────────────────────────────────────

struct ConfigState {
    juce::String inputPort;
    juce::String outputPort;
    bool forwardingEnabled { false };
    juce::String dbPath;
    int deviceId { 1 };             // 1..16
    int sysexChunkBytes { 256 };    // pacing chunk size
    int sysexInterChunkMs { 20 };   // pacing delay between chunks
    int patchesToSend { 8 };        // how many voices to send (1,8,16,32)
    int lastSelectedTg { 0 };       // 0-based; default TG1

    // Performance state for TG1-TG8  (Python-compatible format)
    core::TgState tg[8];            // tg[0] = TG1 .. tg[7] = TG8
    bool hasPerformanceParams { false };

    // Names of the patches in the most recently sent bank (position = bank slot index)
    juce::StringArray presetBankNames; // up to 32 entries; empty string = not set
};

// ─── Config file I/O ─────────────────────────────────────────────────────

class Config {
public:
    static juce::File getConfigFile()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                     .getChildFile("802_Manager");
        dir.createDirectory();
        return dir.getChildFile("802_manager_settings.json");
    }

    static bool load(ConfigState& state)
    {
        auto file = getConfigFile();
        if (! file.existsAsFile()) return false;
        juce::var json = juce::JSON::parse(file);
        if (json.isVoid() || ! json.isObject()) return false;
        auto* obj = json.getDynamicObject();

        state.inputPort = obj->getProperty("input_port").toString();
        state.outputPort = obj->getProperty("output_port").toString();
        state.forwardingEnabled = (bool) obj->getProperty("midi_forwarding");
        state.dbPath = obj->getProperty("db_path").toString();
        if (obj->hasProperty("device_id"))
            state.deviceId = (int) obj->getProperty("device_id");
        if (obj->hasProperty("sysex_chunk_bytes"))
            state.sysexChunkBytes = (int) obj->getProperty("sysex_chunk_bytes");
        if (obj->hasProperty("sysex_interchunk_ms"))
            state.sysexInterChunkMs = (int) obj->getProperty("sysex_interchunk_ms");
        if (obj->hasProperty("patches_to_send"))
            state.patchesToSend = (int) obj->getProperty("patches_to_send");
        if (obj->hasProperty("last_selected_tg"))
            state.lastSelectedTg = juce::jlimit(0, 7, (int) obj->getProperty("last_selected_tg"));

        // Load performance params – Python-compatible format:
        // "performance_params": { "1": { "TG": "Off", "PRESET": "I01", ... }, ... }
        if (obj->hasProperty("performance_params"))
        {
            auto* perfObj = obj->getProperty("performance_params").getDynamicObject();
            if (perfObj)
            {
                state.hasPerformanceParams = true;
                for (int tg = 1; tg <= 8; ++tg)
                {
                    auto key = juce::String(tg);
                    if (perfObj->hasProperty(key))
                    {
                        auto* tgObj = perfObj->getProperty(key).getDynamicObject();
                        if (tgObj)
                        {
                            auto& t = state.tg[tg - 1];
                            if (tgObj->hasProperty("TG"))        t.tgOnOff   = tgObj->getProperty("TG").toString();
                            if (tgObj->hasProperty("PRESET"))    t.preset    = tgObj->getProperty("PRESET").toString();
                            if (tgObj->hasProperty("RXCH"))      t.rxch      = tgObj->getProperty("RXCH").toString();
                            if (tgObj->hasProperty("NOTELOW"))   t.noteLow   = tgObj->getProperty("NOTELOW").toString();
                            if (tgObj->hasProperty("NOTEHIGH"))  t.noteHigh  = tgObj->getProperty("NOTEHIGH").toString();
                            if (tgObj->hasProperty("DETUNE"))    t.detune    = tgObj->getProperty("DETUNE").toString();
                            if (tgObj->hasProperty("NOTESHIFT")) t.noteShift = tgObj->getProperty("NOTESHIFT").toString();
                            if (tgObj->hasProperty("OUTVOL"))    t.outVol    = tgObj->getProperty("OUTVOL").toString();
                            if (tgObj->hasProperty("PAN"))       t.pan       = tgObj->getProperty("PAN").toString();
                            if (tgObj->hasProperty("FDAMP"))     t.fDamp     = tgObj->getProperty("FDAMP").toString();
                        }
                    }
                }
            }
        }

        // Load preset bank names (up to 32 entries)
        if (obj->hasProperty("preset_bank"))
        {
            auto* arr = obj->getProperty("preset_bank").getArray();
            if (arr)
            {
                state.presetBankNames.clear();
                for (int j = 0; j < arr->size() && j < 32; ++j)
                    state.presetBankNames.add((*arr)[j].toString());
            }
        }

        return true;
    }

    static bool save(const ConfigState& state)
    {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("input_port", state.inputPort);
        obj->setProperty("output_port", state.outputPort);
        obj->setProperty("midi_forwarding", state.forwardingEnabled);
        if (state.dbPath.isNotEmpty()) obj->setProperty("db_path", state.dbPath);
        obj->setProperty("device_id", state.deviceId);
        obj->setProperty("sysex_chunk_bytes", state.sysexChunkBytes);
        obj->setProperty("sysex_interchunk_ms", state.sysexInterChunkMs);
        obj->setProperty("patches_to_send", state.patchesToSend);
        obj->setProperty("last_selected_tg", state.lastSelectedTg);

        // Save performance params – Python-compatible format
        if (state.hasPerformanceParams)
        {
            juce::DynamicObject::Ptr perfObj = new juce::DynamicObject();
            for (int tg = 1; tg <= 8; ++tg)
            {
                const auto& t = state.tg[tg - 1];
                juce::DynamicObject::Ptr tgObj = new juce::DynamicObject();
                tgObj->setProperty("TG",        t.tgOnOff);
                tgObj->setProperty("PRESET",    t.preset);
                tgObj->setProperty("RXCH",      t.rxch);
                tgObj->setProperty("NOTELOW",   t.noteLow);
                tgObj->setProperty("NOTEHIGH",  t.noteHigh);
                tgObj->setProperty("DETUNE",    t.detune);
                tgObj->setProperty("NOTESHIFT", t.noteShift);
                tgObj->setProperty("OUTVOL",    t.outVol);
                tgObj->setProperty("PAN",       t.pan);
                tgObj->setProperty("FDAMP",     t.fDamp);
                perfObj->setProperty(juce::String(tg), juce::var(tgObj.get()));
            }
            obj->setProperty("performance_params", juce::var(perfObj.get()));
        }

        // Save preset bank names + IDs
        if (state.presetBankNames.size() > 0)
        {
            juce::Array<juce::var> names;
            for (const auto& n : state.presetBankNames)
                names.add(n);
            obj->setProperty("preset_bank", names);
        }

        juce::var json(obj.get());
        auto file = getConfigFile();
        return file.replaceWithText(juce::JSON::toString(json, false));
    }
};

} // namespace midi
