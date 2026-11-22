#include "UserService.h"
#include <stdexcept>

UserService::UserService(UserRepository& userRepo)
    : _userRepo(userRepo) {}

int UserService::addUser(const std::string& name, UserRole role) {
    validateName(name);
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

void UserService::validateName(const std::string& name) {
    const bool blank = std::all_of(name.begin(), name.end(),
        [](unsigned char c) {
            return std::isspace(c) != 0;
        });
    if (blank) {
        throw std::invalid_argument("Ім'я не може бути порожнім");
    }
    if (name.size() > 100) {
        throw std::invalid_argument("Ім'я задовге (максимум 100 символів)");
    }
}
