//
// Created by Віра Вознюк on 07.11.2026.
//

#ifndef ACCESSTERMINAL_SECURITYCONFIG_H
#define ACCESSTERMINAL_SECURITYCONFIG_H
#include <chrono>

struct SecurityConfig {
    int maxFailedAttempts = 3;
    std::chrono::minutes blockDuration{15};
    std::chrono::seconds doorOpenTimeout{5};
};

#endif //ACCESSTERMINAL_SECURITYCONFIG_H