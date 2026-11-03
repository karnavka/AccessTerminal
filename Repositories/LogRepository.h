#pragma once
#include <sqlite3.h>
#include <optional>
#include <string>
#include <vector>
#include "../Entity/LogEntity.h"


struct LogFilter {
    std::optional<LogResult> result;
    std::optional<TimePoint> from;
    std::optional<TimePoint> to;
    std::optional<std::string> cardId;
};

class LogRepository {
public:
    explicit LogRepository(sqlite3* db);

    int add(const LogEntity& entry);
    std::vector<LogEntity> findAll() const;
    std::vector<LogEntity> filter(const LogFilter& f) const;

private:
    sqlite3* db_;
};
