#pragma once

#include <juce_core/juce_core.h>

namespace storage {

struct PresetRow {
    int id { 0 };
    juce::String presetName;
    juce::String category;
    juce::String comments;
    int rating { 0 };
};

class PresetsDb {
public:
    explicit PresetsDb(const juce::File& dbFile);
    bool open();
    void close();

    // Fetch paginated rows with optional text and rating filters
    juce::Array<PresetRow> queryPresets(const juce::String& textFilter,
                                        juce::String ratingFilter,
                                        int limit,
                                        int offset,
                                        int& totalRows,
                                        juce::String& errorMessage);

    // Fetch full sysex blob by preset id
    bool getSysexById(int id, juce::MemoryBlock& outSysex, juce::String& errorMessage);

    // Update rating for a preset (0 => unrated/NULL)
    bool updateRating(int id, int rating, juce::String& errorMessage);

private:
    juce::File dbFile;
    void* sqliteHandle { nullptr };
};

} // namespace storage
