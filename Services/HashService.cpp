#include <functional>
#include "Services/HashService.h"

std::string HashService::hash(const std::string& pin) const{
    return std::to_string(std::hash<std::string>{}(pin));
}

bool HashService::verify(const std::string& pin, const std::string& stored) const{
    return hash(pin) == stored;
}