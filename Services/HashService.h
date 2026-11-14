#pragma once

#include <string>

class HashService {
public:
    std::string hash(const std::string& pin) const;

    bool verify(
        const std::string& pin,
        const std::string& storedHash
    ) const;
};