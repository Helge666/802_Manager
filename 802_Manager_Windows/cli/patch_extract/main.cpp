/**
 * patch_extract – Extract individual presets from a DX7 bank (.syx) file.
 *
 * Port of Python cli/dx7/patch_extract.py.
 * Optionally writes individual .syx files and/or imports to a SQLite database.
 *
 * Usage:
 *   patch_extract --bankfile bank.syx --folder ./presets
 *   patch_extract --bankfile bank.syx --db presets.sqlite3 --origin "Yamaha Factory"
 */

#include <juce_core/juce_core.h>
#include "core/Dx7Utils.h"
#include "storage/PresetsDb.h"
#include <sqlite3.h>

// Simplified DB insert: we use raw sqlite3 since PresetsDb doesn't expose insert
static bool insertPresetToDb(sqlite3* db, const juce::String& name, const juce::String& bankFileName,
                              int slotIndex, const juce::MemoryBlock& sysex163, const juce::String& origin)
{
    const char* sql = "INSERT INTO presets (presetname, bankfile, slot, sysex, origin) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        // Table might not exist – create it
        const char* create =
            "CREATE TABLE IF NOT EXISTS presets ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  presetname TEXT, category TEXT, comments TEXT, rating INTEGER,"
            "  bankfile TEXT, slot INTEGER, sysex BLOB, origin TEXT,"
            "  hash TEXT"
            ");";
        sqlite3_exec(db, create, nullptr, nullptr, nullptr);
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;
    }
    sqlite3_bind_text(stmt, 1, name.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, bankFileName.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, slotIndex);
    sqlite3_bind_blob(stmt, 4, sysex163.getData(), (int)sysex163.getSize(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, origin.toRawUTF8(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

int main(int argc, char* argv[])
{
    juce::String bankFile, folder, dbPath, origin;

    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);
        if (arg == "--bankfile" && i + 1 < argc) bankFile = juce::String(argv[++i]);
        else if (arg == "--folder" && i + 1 < argc) folder = juce::String(argv[++i]);
        else if (arg == "--db" && i + 1 < argc) dbPath = juce::String(argv[++i]);
        else if (arg == "--origin" && i + 1 < argc) origin = juce::String(argv[++i]);
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: patch_extract --bankfile <file.syx> [options]\n"
                      << "  --folder <dir>     Write individual .syx files to folder\n"
                      << "  --db <path>        Import presets into SQLite database\n"
                      << "  --origin <text>    Origin string stored in database\n";
            return 0;
        }
        else { std::cerr << "Unknown option: " << arg.toStdString() << "\n"; return 1; }
    }

    if (bankFile.isEmpty()) { std::cerr << "Error: --bankfile required\n"; return 1; }

    juce::File f(bankFile);
    if (!f.existsAsFile()) { std::cerr << "File not found: " << bankFile.toStdString() << "\n"; return 1; }

    juce::MemoryBlock data;
    f.loadFileAsData(data);
    std::cout << "Read " << data.getSize() << " bytes from '" << bankFile.toStdString() << "'\n";

    juce::String msg;
    if (!core::Dx7Utils::isValidDx7Bank(data, msg))
    {
        std::cerr << "Invalid bank: " << msg.toStdString() << "\n";
        return 1;
    }
    std::cout << "Bank validation: " << msg.toStdString() << "\n";

    bool writeFiles = folder.isNotEmpty();
    bool writeDb = dbPath.isNotEmpty();
    if (!writeFiles && !writeDb)
    {
        std::cout << "No output specified (--folder or --db). Validation only.\n";
        return 0;
    }

    // Create output folder
    if (writeFiles)
    {
        juce::File outDir(folder);
        outDir.createDirectory();
        std::cout << "Output folder: " << folder.toStdString() << "\n";
    }

    // Open DB
    sqlite3* db = nullptr;
    if (writeDb)
    {
        if (sqlite3_open(dbPath.toRawUTF8(), &db) != SQLITE_OK)
        {
            std::cerr << "Failed to open database: " << dbPath.toStdString() << "\n";
            return 1;
        }
        std::cout << "Database: " << dbPath.toStdString() << "\n";
    }

    const auto* bankBytes = static_cast<const juce::uint8*>(data.getData());
    const juce::String bankFileName = f.getFileName();
    int fileCount = 0, dbCount = 0, errors = 0;

    for (int slot = 0; slot < 32; ++slot)
    {
        const int offset = 6 + slot * 128;
        juce::MemoryBlock packed128(bankBytes + offset, 128);

        // Unpack to 155-byte voice
        juce::MemoryBlock v155 = core::Dx7Utils::unpackBankVoiceToSingle(packed128);
        if ((int)v155.getSize() != 155) { ++errors; continue; }

        // Extract name
        juce::String presetName = core::Dx7Utils::extractPresetNameFromUnpacked(v155);

        // Create full 163-byte SysEx
        juce::MemoryBlock syx163 = core::Dx7Utils::createSinglePresetSysex(packed128);
        if ((int)syx163.getSize() != 163) { ++errors; continue; }

        std::cout << "  [" << (slot + 1) << "] " << presetName.toStdString() << "\n";

        // Write file
        if (writeFiles)
        {
            juce::String fileName = juce::String(slot + 1).paddedLeft('0', 2) + "_" + presetName.trim().replace(" ", "_") + ".syx";
            juce::File outFile = juce::File(folder).getChildFile(fileName);
            if (outFile.replaceWithData(syx163.getData(), syx163.getSize()))
                ++fileCount;
            else
                std::cerr << "    Failed to write " << fileName.toStdString() << "\n";
        }

        // Insert to DB
        if (db)
        {
            if (insertPresetToDb(db, presetName, bankFileName, slot + 1, syx163, origin))
                ++dbCount;
            else
                std::cerr << "    DB insert failed for slot " << (slot + 1) << "\n";
        }
    }

    if (db) sqlite3_close(db);

    std::cout << "\nExtraction complete.\n";
    if (writeFiles) std::cout << "  Files written: " << fileCount << "\n";
    if (writeDb)    std::cout << "  DB inserts: " << dbCount << "\n";
    if (errors > 0) std::cout << "  Errors: " << errors << "\n";

    return errors > 0 ? 1 : 0;
}
