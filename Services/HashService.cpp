#include "Services/HashService.h"

#include <argon2.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <stdexcept>

namespace {
    constexpr std::uint32_t TIME_COST   = 3;
    constexpr std::uint32_t MEMORY_KIB  = 1 << 15;  // 32 MiB
    constexpr std::uint32_t PARALLELISM = 1;
    constexpr std::size_t   SALT_BYTES  = 16;
    constexpr std::size_t   HASH_BYTES  = 32;

    std::array<unsigned char, SALT_BYTES> makeSalt() {
        std::array<unsigned char, SALT_BYTES> salt{};
        std::random_device rd;
        for (unsigned char& b : salt) {
            b = static_cast<unsigned char>(rd());
        }
        return salt;
    }
}

std::string HashService::hash(const std::string& pin) const {
    const auto salt = makeSalt();

    const std::size_t size = argon2_encodedlen(
        TIME_COST, MEMORY_KIB, PARALLELISM, SALT_BYTES, HASH_BYTES, Argon2_id);
    std::string encoded(size, '\0');

    const int rc = argon2id_hash_encoded(
        TIME_COST, MEMORY_KIB, PARALLELISM,
        pin.data(), pin.size(),
        salt.data(), salt.size(),
        HASH_BYTES,
        &encoded[0], encoded.size());

    if (rc != ARGON2_OK) {
        throw std::runtime_error(std::string("Argon2 hashing failed: ") + argon2_error_message(rc));
    }

    encoded.resize(std::strlen(encoded.c_str()));
    return encoded;
}

bool HashService::verify(const std::string& pin, const std::string& storedHash) const {
    return argon2id_verify(storedHash.c_str(), pin.data(), pin.size()) == ARGON2_OK;
}
