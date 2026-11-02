#pragma once
#include <sqlite3.h>
#include <string>

class DataBaseInitializer {
public:
    explicit DataBaseInitializer(const std::string& dbPath,
                                 const std::string& schemaPath,
                                 const std::string& seedPath);
    ~DataBaseInitializer();

    DataBaseInitializer(const DataBaseInitializer&) = delete;
    DataBaseInitializer& operator=(const DataBaseInitializer&) = delete;

    bool open();
    bool initialize();
    void close();

    sqlite3* getConnection() const { return db_; }
    bool begin();
    bool commit();
    bool rollback();

private:
    bool exec(const std::string& sql);
    bool readFile(const std::string& path, std::string&  out);

    sqlite3* db_ = nullptr;
    std::string dbPath_;
    std::string schemaPath_;
    std::string seedPath_;
};