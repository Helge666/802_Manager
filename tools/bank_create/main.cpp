/**
 * bank_create – Create a DX7 32-voice bank (.syx) from individual files and/or DB presets.
 *
 * Port of Python cli/dx7/bank_create.py.
 *
 * Usage:
 *   bank_create --bankfile out.syx --presetfiles "a.syx,b.syx,c.syx"
 *   bank_create --bankfile out.syx --db presets.sqlite3 --presetids "1,5,42,100"
 *   bank_create --bankfile out.syx --presetfiles "a.syx" --db presets.sqlite3 --presetids "10,20"
 */

#include <juce_core/juce_core.h>
#include "core/Dx7Utils.h"
#include "storage/PresetsDb.h"

int main(int argc, char* argv[])
{
    juce::String bankFile, presetFiles, dbPath, presetIds;

    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);
        if (arg == "--bankfile" && i + 1 < argc) bankFile = juce::String(argv[++i]);
        else if (arg == "--presetfiles" && i + 1 < argc) presetFiles = juce::String(argv[++i]);
        else if (arg == "--db" && i + 1 < argc) dbPath = juce::String(argv[++i]);
        else if (arg == "--presetids" && i + 1 < argc) presetIds = juce::String(argv[++i]);
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: bank_create --bankfile <out.syx> [options]\n"
                      << "  --presetfiles <list>  Comma-separated .syx single voice files\n"
                      << "  --db <path>           SQLite database path\n"
                      << "  --presetids <list>    Comma-separated preset IDs from database\n";
            return 0;
        }
        else { std::cerr << "Unknown option: " << arg.toStdString() << "\n"; return 1; }
    }

    if (bankFile.isEmpty()) { std::cerr << "Error: --bankfile required\n"; return 1; }
    if (presetFiles.isEmpty() && (dbPath.isEmpty() || presetIds.isEmpty()))
    {
        std::cerr << "Error: Provide --presetfiles and/or --db with --presetids\n";
        return 1;
    }

    std::cout << "Creating bank file: " << bankFile.toStdString() << "\n";

    // Collect packed 128-byte voices
    juce::Array<juce::MemoryBlock> packedVoices;

    // 1) From individual .syx files
    if (presetFiles.isNotEmpty())
    {
        juce::StringArray files;
        files.addTokens(presetFiles, ",", "");
        std::cout << "Processing " << files.size() << " preset file(s)...\n";

        for (const auto& fname : files)
        {
            juce::File f(fname.trim());
            if (!f.existsAsFile()) { std::cerr << "  File not found: " << fname.toStdString() << "\n"; continue; }
            juce::MemoryBlock data;
            f.loadFileAsData(data);

            juce::String msg;
            juce::MemoryBlock v155;
            if (!core::Dx7Utils::verifySingleVoiceSysex(data, msg, v155))
            {
                std::cerr << "  Invalid: " << msg.toStdString() << " (" << fname.toStdString() << ")\n";
                continue;
            }
            juce::String name = core::Dx7Utils::extractPresetNameFromSysex(data);
            auto v128 = core::Dx7Utils::packSingleToBankVoice(v155);
            packedVoices.add(v128);
            std::cout << "  Added '" << name.toStdString() << "' from file\n";
        }
    }

    // 2) From database
    if (dbPath.isNotEmpty() && presetIds.isNotEmpty())
    {
        juce::File dbFile(dbPath);
        if (!dbFile.existsAsFile()) { std::cerr << "Database not found: " << dbPath.toStdString() << "\n"; return 1; }
        storage::PresetsDb db(dbFile);
        if (!db.open()) { std::cerr << "Failed to open database\n"; return 1; }

        juce::StringArray ids;
        ids.addTokens(presetIds, ",", "");
        std::cout << "Retrieving " << ids.size() << " preset(s) from database...\n";

        for (const auto& idStr : ids)
        {
            int id = idStr.trim().getIntValue();
            if (id <= 0) { std::cerr << "  Invalid ID: " << idStr.toStdString() << "\n"; continue; }

            juce::MemoryBlock syx;
            juce::String err;
            if (!db.getSysexById(id, syx, err))
            {
                std::cerr << "  ID " << id << " not found: " << err.toStdString() << "\n";
                continue;
            }
            juce::String msg;
            juce::MemoryBlock v155;
            if (!core::Dx7Utils::verifySingleVoiceSysex(syx, msg, v155))
            {
                std::cerr << "  ID " << id << " invalid: " << msg.toStdString() << "\n";
                continue;
            }
            juce::String name = core::Dx7Utils::extractPresetNameFromSysex(syx);
            auto v128 = core::Dx7Utils::packSingleToBankVoice(v155);
            packedVoices.add(v128);
            std::cout << "  Added '" << name.toStdString() << "' (ID: " << id << ") from database\n";
        }
    }

    if (packedVoices.isEmpty())
    {
        std::cerr << "No valid presets collected. Aborting.\n";
        return 1;
    }

    // Pad to 32 with init voices if needed
    std::cout << "Collected " << packedVoices.size() << " preset(s)";
    if (packedVoices.size() < 32)
        std::cout << ", padding remaining " << (32 - packedVoices.size()) << " slot(s) with init voice";
    std::cout << "\n";

    juce::MemoryBlock initVoice(128, true); // All zeros = init
    while (packedVoices.size() < 32)
        packedVoices.add(initVoice);

    // Build bank (4104 bytes)
    juce::MemoryBlock bank(4104);
    auto* b = static_cast<juce::uint8*>(bank.getData());
    b[0] = 0xF0; b[1] = 0x43; b[2] = 0x00; b[3] = 0x09; b[4] = 0x20; b[5] = 0x00;
    for (int i = 0; i < 32; ++i)
        std::memcpy(b + 6 + i * 128, packedVoices[i].getData(), 128);

    juce::MemoryBlock chkData(b + 6, 4096);
    b[4102] = core::Dx7Utils::calculateChecksum(chkData);
    b[4103] = 0xF7;

    // Validate
    juce::String valMsg;
    if (!core::Dx7Utils::isValidDx7Bank(bank, valMsg))
    {
        std::cerr << "Bank validation failed: " << valMsg.toStdString() << "\n";
        return 1;
    }

    // Write
    juce::File outFile(bankFile);
    if (!outFile.replaceWithData(bank.getData(), bank.getSize()))
    {
        std::cerr << "Failed to write: " << bankFile.toStdString() << "\n";
        return 1;
    }

    std::cout << "Bank created: " << bankFile.toStdString() << " (" << bank.getSize() << " bytes, "
              << juce::jmin(32, packedVoices.size()) << " presets)\n";
    return 0;
}
