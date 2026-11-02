#pragma once
#include <stdexcept>
#include <string>

enum class UserRole { User, Admin };

struct UserEntity{
    int id;
    std::string name;
    UserRole role;
};

// inline std::string toString(UserRole r) {
//     switch (r) {
//         case UserRole::User:  return "User";
//         case UserRole::Admin: return "Admin";
//     }
//     throw std::invalid_argument("Unknown UserRole");
// }
 
// inline UserRole userRoleFromString(const std::string& s) {
//     if (s == "User")  return UserRole::User;
//     if (s == "Admin") return UserRole::Admin;
//     throw std::invalid_argument("Unknown UserRole: " + s);
// }