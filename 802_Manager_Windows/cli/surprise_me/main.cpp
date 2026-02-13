/**
 * surprise_me – Create a DX7 bank with randomly selected presets from a database.
 *
 * Port of Python cli/dx7/surprise_me.py.
 *
 * Usage:
 *   surprise_me --db presets.sqlite3 --bankfile random_bank.syx
 *   surprise_me --db presets.sqlite3 --bankfile random_bank.syx --count 16
 */

#include <juce_core/juce_core.h>
#include "core/Dx7Utils.h"
#include "storage/PresetsDb.h"

int main(int argc, char* argv[])
{
    juce::String dbPath, bankFile;
    int count = 32;

    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);
        if (arg == "--db" && i + 1 < argc) dbPath = juce::String(argv[++i]);
        else if (arg == "--bankfile" && i + 1 < argc) bankFile = juce::String(argv[++i]);
        else if (arg == "--count" && i + 1 < argc) count = juce::jlimit(1, 32, juce::String(argv[++i]).getIntValue());
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: surprise_me --db <path> --bankfile <out.syx> [--count <N>]\n"
                      << "  --count <1-32>  Number of presets to include (default: 32)\n";
            return 0;
        }
        else { std::cerr << "Unknown option: " << arg.toStdString() << "\n"; return 1; }
    }

    if (dbPath.isEmpty() || bankFile.isEmpty()) { std::cerr << "Error: --db and --bankfile required\n"; return 1; }

    juce::File dbFile(dbPath);
    if (!dbFile.existsAsFile()) { std::cerr << "Database not found: " << dbPath.toStdString() << "\n"; return 1; }

    storage::PresetsDb db(dbFile);
    if (!db.open()) { std::cerr << "Failed to open database\n"; return 1; }

    // Query all presets (just to get IDs and names)
    juce::String err;
    int total = 0;
    auto rows = db.queryPresets("", "Any", 99999, 0, total, err);
    if (rows.isEmpty()) { std::cerr << "No presets in database\n"; return 1; }

    int numPresets = juce::jmin(count, rows.size());
    std::cout << "Database has " << rows.size() << " presets, selecting " << numPresets << " randomly.\n";

    // Fisher-Yates shuffle to pick numPresets
    juce::Array<int> indices;
    for (int i = 0; i < rows.size(); ++i) indices.add(i);
    juce::Random rng;
    for (int i = indices.size() - 1; i > 0; --i)
    {
        int j = rng.nextInt(i + 1);
        indices.swap(i, j);
    }

    // Collect packed voices
    juce::Array<juce::MemoryBlock> packedVoices;
    for (int s = 0; s < numPresets; ++s)
    {
        int id = rows[indices[s]].id;
        juce::MemoryBlock syx;
        juce::String fetchErr;
        if (!db.getSysexById(id, syx, fetchErr)) { std::cerr << "  Failed to fetch ID " << id << "\n"; continue; }

        juce::String msg;
        juce::MemoryBlock v155;
        if (!core::Dx7Utils::verifySingleVoiceSysex(syx, msg, v155)) { std::cerr << "  Invalid ID " << id << "\n"; continue; }

        auto v128 = core::Dx7Utils::packSingleToBankVoice(v155);
        packedVoices.add(v128);
    }

    std::cout << "Selected " << packedVoices.size() << " valid presets.\n";

    // Pad to 32
    juce::MemoryBlock initVoice(128, true);
    while (packedVoices.size() < 32)
        packedVoices.add(initVoice);

    // Build bank
    juce::MemoryBlock bank(4104);
    auto* b = static_cast<juce::uint8*>(bank.getData());
    b[0] = 0xF0; b[1] = 0x43; b[2] = 0x00; b[3] = 0x09; b[4] = 0x20; b[5] = 0x00;
    for (int i = 0; i < 32; ++i)
        std::memcpy(b + 6 + i * 128, packedVoices[i].getData(), 128);

    juce::MemoryBlock chkData(b + 6, 4096);
    b[4102] = core::Dx7Utils::calculateChecksum(chkData);
    b[4103] = 0xF7;

    // Write
    juce::File outFile(bankFile);
    if (!outFile.replaceWithData(bank.getData(), bank.getSize()))
    {
        std::cerr << "Failed to write: " << bankFile.toStdString() << "\n";
        return 1;
    }

    std::cout << "Surprise bank created: " << bankFile.toStdString() << " (" << bank.getSize() << " bytes)\n";
    return 0;
}
