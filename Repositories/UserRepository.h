#pragma once
#include <sqlite3.h>
#include <optional>
#include <string>
#include <vector>
#include "../Entity/UserEntity.h"


class UserRepository {
public:
    explicit UserRepository(sqlite3* db);

    //додати нового
    int add(const std::string& name, UserRole role);
    //знайти за айді
    std::optional<UserEntity> findById(int id) const;
    //вивести всіх
    std::vector<UserEntity> findAll() const;
    // чи має вже активну карточку
    bool hasActiveCard(int userId) const;

private:
    sqlite3* db_;
};
