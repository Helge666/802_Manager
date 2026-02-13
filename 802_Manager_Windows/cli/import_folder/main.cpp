/**
 * import_folder – Recursively import DX7 bank (.syx) files from a folder into a database.
 *
 * Port of Python cli/dx7/import_folder.py.
 * Walks subfolders, uses subfolder name as origin.
 *
 * Usage:
 *   import_folder --folder /path/to/banks --db presets.sqlite3
 *   import_folder --folder /path/to/banks --db presets.sqlite3 --dry-run
 */

#include <juce_core/juce_core.h>
#include "core/Dx7Utils.h"
#include <sqlite3.h>

static bool ensureTable(sqlite3* db)
{
    const char* sql =
        "CREATE TABLE IF NOT EXISTS presets ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  presetname TEXT, category TEXT, comments TEXT, rating INTEGER,"
        "  bankfile TEXT, slot INTEGER, sysex BLOB, origin TEXT,"
        "  hash TEXT"
        ");";
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

static bool insertPreset(sqlite3* db, const juce::String& name, const juce::String& bankFileName,
                          int slot, const juce::MemoryBlock& syx163, const juce::String& origin)
{
    const char* sql = "INSERT INTO presets (presetname, bankfile, slot, sysex, origin) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, bankFileName.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, slot);
    sqlite3_bind_blob(stmt, 4, syx163.getData(), (int)syx163.getSize(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, origin.toRawUTF8(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

static int processBank(const juce::File& syxFile, sqlite3* db, const juce::String& origin, bool dryRun)
{
    juce::MemoryBlock data;
    syxFile.loadFileAsData(data);

    juce::String msg;
    if (!core::Dx7Utils::isValidDx7Bank(data, msg))
    {
        std::cerr << "  Invalid bank: " << msg.toStdString() << "\n";
        return 0;
    }

    const auto* bytes = static_cast<const juce::uint8*>(data.getData());
    const juce::String bankFileName = syxFile.getFileName();
    int inserted = 0;

    for (int slot = 0; slot < 32; ++slot)
    {
        juce::MemoryBlock packed128(bytes + 6 + slot * 128, 128);
        juce::MemoryBlock v155 = core::Dx7Utils::unpackBankVoiceToSingle(packed128);
        if ((int)v155.getSize() != 155) continue;

        juce::String name = core::Dx7Utils::extractPresetNameFromUnpacked(v155);
        juce::MemoryBlock syx163 = core::Dx7Utils::createSinglePresetSysex(packed128);
        if ((int)syx163.getSize() != 163) continue;

        if (dryRun)
        {
            std::cout << "    [DRY RUN] Would import slot " << (slot + 1) << ": " << name.toStdString() << "\n";
            ++inserted;
        }
        else
        {
            if (insertPreset(db, name, bankFileName, slot + 1, syx163, origin))
                ++inserted;
        }
    }
    return inserted;
}

int main(int argc, char* argv[])
{
    juce::String folder, dbPath;
    bool dryRun = false;

    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);
        if (arg == "--folder" && i + 1 < argc) folder = juce::String(argv[++i]);
        else if (arg == "--db" && i + 1 < argc) dbPath = juce::String(argv[++i]);
        else if (arg == "--dry-run") dryRun = true;
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: import_folder --folder <dir> --db <path> [--dry-run]\n"
                      << "  Recursively scans subfolders for .syx files.\n"
                      << "  Uses subfolder name as origin.\n"
                      << "  --dry-run  Print what would be imported without writing to DB.\n";
            return 0;
        }
        else { std::cerr << "Unknown option: " << arg.toStdString() << "\n"; return 1; }
    }

    if (folder.isEmpty() || dbPath.isEmpty()) { std::cerr << "Error: --folder and --db required\n"; return 1; }

    juce::File baseDir(folder);
    if (!baseDir.isDirectory()) { std::cerr << "Folder not found: " << folder.toStdString() << "\n"; return 1; }

    sqlite3* db = nullptr;
    if (!dryRun)
    {
        if (sqlite3_open(dbPath.toRawUTF8(), &db) != SQLITE_OK)
        {
            std::cerr << "Failed to open database: " << dbPath.toStdString() << "\n";
            return 1;
        }
        ensureTable(db);
    }

    std::cout << "Starting import from " << folder.toStdString() << "\n";
    std::cout << "Database: " << dbPath.toStdString() << "\n";
    if (dryRun) std::cout << "DRY RUN MODE\n";

    int totalFiles = 0, successful = 0;

    // Walk subdirectories
    for (const auto& entry : juce::RangedDirectoryIterator(baseDir, true, "*.syx", juce::File::findFiles))
    {
        juce::File syxFile = entry.getFile();
        // Use parent folder name as origin (skip if directly in base)
        juce::String origin = syxFile.getParentDirectory().getFileName();
        if (syxFile.getParentDirectory() == baseDir)
            origin = "root";

        ++totalFiles;
        std::cout << "Processing: " << syxFile.getFullPathName().toStdString() << " (origin: " << origin.toStdString() << ")\n";

        int inserted = processBank(syxFile, db, origin, dryRun);
        if (inserted > 0)
        {
            ++successful;
            std::cout << "  Successfully imported " << inserted << " presets\n";
        }
        else
        {
            std::cout << "  Skipped (invalid or empty)\n";
        }
    }

    if (db) sqlite3_close(db);

    std::cout << "\nImport Summary:\n";
    std::cout << "  Total .syx files found: " << totalFiles << "\n";
    std::cout << "  Successfully processed: " << successful << "\n";
    std::cout << "  Failed: " << (totalFiles - successful) << "\n";

    return successful > 0 ? 0 : 1;
}
