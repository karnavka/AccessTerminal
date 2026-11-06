#pragma once
#include <sqlite3.h>
#include <optional>
#include <stdexcept>
#include <string>


class DbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Statement {
public:
    Statement(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st_, nullptr) != SQLITE_OK)
            throw DbError(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }
    ~Statement() { sqlite3_finalize(st_); }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(int i, int v)                { check(sqlite3_bind_int(st_, i, v)); }
    void bind(int i, const std::string& v) {
        check(sqlite3_bind_text(st_, i, v.c_str(), -1, SQLITE_TRANSIENT));
    }
    void bindNull(int i)                   { check(sqlite3_bind_null(st_, i)); }

    template <class T>
    void bind(int i, const std::optional<T>& v) {
        if (v) bind(i, *v);
        else   bindNull(i);
    }

    bool step() {
        int rc = sqlite3_step(st_);
        if (rc == SQLITE_ROW)  return true;
        if (rc == SQLITE_DONE) return false;
        throw DbError(std::string("step failed: ") + sqlite3_errmsg(db_));
    }

    void reset() {
        sqlite3_reset(st_);
        sqlite3_clear_bindings(st_);
    }

    bool isNull(int c) const { return sqlite3_column_type(st_, c) == SQLITE_NULL; }
    int columnInt(int c) const { return sqlite3_column_int(st_, c); }
    std::string columnText(int c) const {
        const unsigned char* t = sqlite3_column_text(st_, c);
        return t ? reinterpret_cast<const char*>(t) : "";
    }
    std::optional<std::string> columnOptText(int c) const {
        if (isNull(c)) return std::nullopt;
        return columnText(c);
    }

private:
    void check(int rc) {
        if (rc != SQLITE_OK)
            throw DbError(std::string("bind failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3* db_ = nullptr;
    sqlite3_stmt* st_ = nullptr;
};

inline void execSql(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : sqlite3_errmsg(db);
        sqlite3_free(err);
        throw DbError(msg);
    }
}

class Savepoint {
public:
    Savepoint(sqlite3* db, std::string name) : db_(db), name_(std::move(name)) {
        execSql(db_, "SAVEPOINT " + name_ + ";");
    }
    ~Savepoint() {
        if (!done_) {
            std::string sql = "ROLLBACK TO " + name_ + "; RELEASE " + name_ + ";";
            sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr);
        }
    }
    void release() {
        execSql(db_, "RELEASE " + name_ + ";");
        done_ = true;
    }

    Savepoint(const Savepoint&) = delete;
    Savepoint& operator=(const Savepoint&) = delete;

private:
    sqlite3* db_;
    std::string name_;
    bool done_ = false;
};
