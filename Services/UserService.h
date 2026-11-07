#pragma once

#include "../Repositories/UserRepository.h"

class UserService {
public:
    explicit UserService(UserRepository& userRepo);

    int addUser(const std::string& name, UserRole role = UserRole::User);

    std::optional<UserEntity> getUser(int userId) const;

    std::vector<UserEntity> getAllUsers() const;

    bool isAdmin(int userId) const;

    bool hasActiveCard(int userId) const;

private:
    UserRepository& _userRepo;
};