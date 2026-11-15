#include "DataBaseInitializer.h"
#include <fstream>
#include <sstream>
#include <cstdio>


bool isUsersEmpty(sqlite3* db) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM users;", -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    bool empty = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        empty = sqlite3_column_int(st, 0) == 0;
    }
    sqlite3_finalize(st);
    return empty;
}



DataBaseInitializer::DataBaseInitializer(const std::string& dbPath,
                                         const std::string& schemaPath,
                                         const std::string& seedPath)
    : dbPath_(dbPath), schemaPath_(schemaPath), seedPath_(seedPath) {}

DataBaseInitializer::~DataBaseInitializer() { close(); }

bool DataBaseInitializer::open() {
    if (sqlite3_open(dbPath_.c_str(), &db_) != SQLITE_OK) {
        std::printf("Cannot open DB: %s\n", sqlite3_errmsg(db_));
        close();
        return false;
    }
    exec("PRAGMA foreign_keys = ON;");
    return true;
}

void DataBaseInitializer::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool DataBaseInitializer::exec(const std::string& sql) {
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::printf("SQL error: %s\n", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool DataBaseInitializer::begin()    { return exec("BEGIN;"); }
bool DataBaseInitializer::commit()   { return exec("COMMIT;"); }
bool DataBaseInitializer::rollback() { return exec("ROLLBACK;"); }

bool DataBaseInitializer::readFile(const std::string& path, std::string& out) {
    std::ifstream file(path);
    if (!file) {
        std::printf("Cannot read file: %s\n", path.c_str());
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

bool DataBaseInitializer::initialize() {
    std::string schema, seed;
    if (!readFile(schemaPath_, schema)) return false;
    if (!readFile(seedPath_, seed))     return false;

    if (!begin()) return false;

    // схему можна виконувати на кожному запуску: там скрізь IF NOT EXISTS
    if (!exec(schema)) {
        rollback();
        return false;
    }

    // seed тільки на першому запуску, коли users ще порожня
    // (інакше UNIQUE constraint failed, бо id користувачів уже є)
    if (isUsersEmpty(db_) && !exec(seed)) {
        rollback();
        return false;
    }
    return commit();
}