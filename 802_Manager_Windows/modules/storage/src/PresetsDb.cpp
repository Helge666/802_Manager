#include "storage/PresetsDb.h"
#include <sqlite3.h>

using namespace storage;

namespace {
struct SqliteDeleter {
    void operator()(sqlite3* db) const noexcept { if (db) sqlite3_close(db); }
};
}

PresetsDb::PresetsDb(const juce::File& file) : dbFile(file) {}

bool PresetsDb::open()
{
    close();
    sqlite3* raw = nullptr;
    if (sqlite3_open(dbFile.getFullPathName().toRawUTF8(), &raw) != SQLITE_OK)
        return false;
    sqliteHandle = raw;
    return true;
}

void PresetsDb::close()
{
    if (sqliteHandle)
    {
        sqlite3_close((sqlite3*) sqliteHandle);
        sqliteHandle = nullptr;
    }
}

juce::Array<PresetRow> PresetsDb::queryPresets(const juce::String& textFilter,
                                               juce::String ratingFilter,
                                               int limit,
                                               int offset,
                                               int& totalRows,
                                               juce::String& errorMessage)
{
    juce::Array<PresetRow> rows;
    totalRows = 0;
    if (! sqliteHandle && ! open()) { errorMessage = "DB open failed"; return rows; }

    auto* db = (sqlite3*) sqliteHandle;

    // Build WHERE clause
    juce::String where;
    juce::Array<juce::String> params;
    if (textFilter.isNotEmpty())
    {
        juce::StringArray cols { "presetname", "category", "bankfile", "comments", "origin" };
        juce::StringArray likeClauses;
        for (auto& c : cols) likeClauses.add(c + " LIKE ?");
        where = " WHERE (" + likeClauses.joinIntoString(" OR ") + ")";
        for (int i = 0; i < cols.size(); ++i) params.add("%" + textFilter + "%");
    }
    if (ratingFilter.isNotEmpty() && ratingFilter != "Any")
    {
        if (where.isNotEmpty()) where += " AND "; else where = " WHERE ";
        if (ratingFilter == "Unrated") where += "rating IS NULL";
        else { where += "rating = ?"; params.add(ratingFilter); }
    }

    juce::String countSql = "SELECT COUNT(*) FROM presets" + where + ";";
    juce::String listSql =
        "SELECT id, presetname, category, comments, COALESCE(rating,0), "
        "COALESCE(bankfile,''), COALESCE(origin,'') "
        "FROM presets" + where + " LIMIT ? OFFSET ?;";

    // Prepare and run count
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, countSql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        int bindIndex = 1;
        for (auto& p : params) sqlite3_bind_text(stmt, bindIndex++, p.toRawUTF8(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) totalRows = sqlite3_column_int(stmt, 0);
    }
    else { errorMessage = "count query prepare failed"; }
    sqlite3_finalize(stmt);

    // Prepare and run list
    if (sqlite3_prepare_v2(db, listSql.toRawUTF8(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        int bindIndex = 1;
        for (auto& p : params) sqlite3_bind_text(stmt, bindIndex++, p.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, bindIndex++, limit);
        sqlite3_bind_int(stmt, bindIndex++, offset);

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            PresetRow row;
            row.id = sqlite3_column_int(stmt, 0);
            row.presetName = (const char*) sqlite3_column_text(stmt, 1);
            row.category = (const char*) sqlite3_column_text(stmt, 2);
            row.comments = (const char*) sqlite3_column_text(stmt, 3);
            row.rating   = sqlite3_column_int(stmt, 4);
            row.bankfile = (const char*) sqlite3_column_text(stmt, 5);
            row.origin   = (const char*) sqlite3_column_text(stmt, 6);
            rows.add(row);
        }
    }
    else { errorMessage = "list query prepare failed"; }
    sqlite3_finalize(stmt);

    return rows;
}

bool PresetsDb::getSysexById(int id, juce::MemoryBlock& outSysex, juce::String& errorMessage)
{
    if (! sqliteHandle && ! open()) { errorMessage = "DB open failed"; return false; }
    auto* db = (sqlite3*) sqliteHandle;

    const char* sql = "SELECT sysex FROM presets WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) { errorMessage = "prepare failed"; return false; }
    sqlite3_bind_int(stmt, 1, id);

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const void* blob = sqlite3_column_blob(stmt, 0);
        int bytes = sqlite3_column_bytes(stmt, 0);
        outSysex.setSize(bytes);
        outSysex.copyFrom(blob, 0, bytes);
        ok = true;
    }
    else errorMessage = "id not found";

    sqlite3_finalize(stmt);
    return ok;
}

bool PresetsDb::updateRating(int id, int rating, juce::String& errorMessage)
{
    if (! sqliteHandle && ! open()) { errorMessage = "DB open failed"; return false; }
    auto* db = (sqlite3*) sqliteHandle;
    const char* sql = rating > 0 ?
        "UPDATE presets SET rating = ? WHERE id = ?;" :
        "UPDATE presets SET rating = NULL WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) { errorMessage = "prepare failed"; return false; }
    int bindIndex = 1;
    if (rating > 0) sqlite3_bind_int(stmt, bindIndex++, rating);
    sqlite3_bind_int(stmt, bindIndex++, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (! ok) errorMessage = "update failed";
    sqlite3_finalize(stmt);
    return ok;
}

bool PresetsDb::updateComments(int id, const juce::String& text, juce::String& errorMessage)
{
    if (! sqliteHandle && ! open()) { errorMessage = "DB open failed"; return false; }
    auto* db = (sqlite3*) sqliteHandle;
    const char* sql = "UPDATE presets SET comments = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) { errorMessage = "prepare failed"; return false; }
    sqlite3_bind_text(stmt, 1, text.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (! ok) errorMessage = "update failed";
    sqlite3_finalize(stmt);
    return ok;
}
