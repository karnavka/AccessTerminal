#pragma once
#include <sqlite3.h>
#include <optional>
#include <string>
#include <vector>
#include "../Entity/CardEntity.h"


class CardRepository {
public:
    explicit CardRepository(sqlite3* db);


    void add(const CardEntity& card);

    std::optional<CardEntity> findById(const std::string& cardId) const;
    std::vector<CardEntity> findByUserId(int userId) const;
    std::vector<CardEntity> findAll() const;


    bool update(const CardEntity& card);

    void replaceTimeWindows(const std::string& cardId, const std::vector<TimeWindow>& windows);

private:
    std::vector<TimeWindow> loadWindows(const std::string& cardId) const;
    void insertWindows(const std::string& cardId, const std::vector<TimeWindow>& windows);
    void attachWindows(std::vector<CardEntity>& cards) const;

    sqlite3* db_;
};
