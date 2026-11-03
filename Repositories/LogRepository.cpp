#include "LogRepository.h"
#include "SqliteStatement.h"

namespace {

LogEntity readLog(const Statement& st) {
    LogEntity e;
    e.id        = st.columnInt(0);
    e.timestamp = fromIso(st.columnText(1));
    e.cardId    = st.columnOptText(2);
    e.result    = logResultFromString(st.columnText(3));
    if (auto r = st.columnOptText(4)) e.reason = logReasonFromString(*r);
    return e;
}

}

LogRepository::LogRepository(sqlite3* db) : db_(db) {}

int LogRepository::add(const LogEntity& entry) {
    Statement st(db_,
        "INSERT INTO access_log (timestamp, card_id, result, reason) VALUES (?, ?, ?, ?);");
    st.bind(1, toIso(entry.timestamp));
    st.bind(2, entry.cardId);
    st.bind(3, toString(entry.result));
    if (entry.reason) st.bind(4, toString(*entry.reason));
    else              st.bindNull(4);
    st.step();
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

std::vector<LogEntity> LogRepository::findAll() const {
    return filter(LogFilter{});
}

std::vector<LogEntity> LogRepository::filter(const LogFilter& f) const {
    // ?N IS NULL -> умова вимкнена. Рядки ISO 8601 UTC порівнюються як дати.
    Statement st(db_,
        "SELECT id, timestamp, card_id, result, reason FROM access_log "
        "WHERE (?1 IS NULL OR result = ?1) "
        "  AND (?2 IS NULL OR timestamp >= ?2) "
        "  AND (?3 IS NULL OR timestamp <  ?3) "
        "  AND (?4 IS NULL OR card_id = ?4) "
        "ORDER BY timestamp, id;");

    if (f.result) st.bind(1, toString(*f.result));
    else          st.bindNull(1);
    if (f.from)   st.bind(2, toIso(*f.from));
    else          st.bindNull(2);
    if (f.to)     st.bind(3, toIso(*f.to));
    else          st.bindNull(3);
    st.bind(4, f.cardId);

    std::vector<LogEntity> entries;
    while (st.step()) entries.push_back(readLog(st));
    return entries;
}
