#include "UserRepository.h"
#include "SqliteStatement.h"

namespace {

UserEntity readUser(const Statement& st) {
    UserEntity u;
    u.id   = st.columnInt(0);
    u.name = st.columnText(1);
    u.role = userRoleFromString(st.columnText(2));
    return u;
}

}

UserRepository::UserRepository(sqlite3* db) : db_(db) {}

int UserRepository::add(const std::string& name, UserRole role) {
    Statement st(db_, "INSERT INTO users (name, role) VALUES (?, ?);");
    st.bind(1, name);
    st.bind(2, toString(role));
    st.step();
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

std::optional<UserEntity> UserRepository::findById(int id) const {
    Statement st(db_, "SELECT id, name, role FROM users WHERE id = ?;");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return readUser(st);
}

std::vector<UserEntity> UserRepository::findAll() const {
    Statement st(db_, "SELECT id, name, role FROM users ORDER BY id;");
    std::vector<UserEntity> users;
    while (st.step()) users.push_back(readUser(st));
    return users;
}
