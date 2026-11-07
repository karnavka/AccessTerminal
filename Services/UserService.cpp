#include "UserService.h"
#include <stdexcept>

UserService::UserService(UserRepository& userRepo)
    : _userRepo(userRepo) {}

int UserService::addUser(const std::string& name, UserRole role) {
    bool isEmpty = true;

    for (char ch : name) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            isEmpty = false;
            break;
        }
    }
    if (isEmpty) {
        throw std::invalid_argument("Name cannot be empty");
    }

    return _userRepo.add(name, role);
}

std::optional<UserEntity> UserService::getUser(int userId) const {
    return _userRepo.findById(userId);
}

std::vector<UserEntity> UserService::getAllUsers() const {
    return _userRepo.findAll();
}

bool UserService::isAdmin(int userId) const {
    auto user = getUser(userId);
    if (!user) {
        return false;
    }
    return user->role == UserRole::Admin;
}

bool UserService::hasActiveCard(int userId) const {
    return _userRepo.hasActiveCard(userId);
}