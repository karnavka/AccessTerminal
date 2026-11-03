#pragma once
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include "TimeUtil.h"

enum class CardStatus { Active, Blocked, Revoked };


struct TimeWindow {
    int dayOfWeek;
    int startMinute;
    int endMinute;
};

struct CardEntity {
    std::string cardId;                         // PRIMARY KEY, вводиться вручну / скануються
    int userId;
    std::string pinHash;                        // ніколи не сам PIN
    CardStatus status = CardStatus::Active;
    int failedAttempts = 0;
    std::optional<TimePoint> blockedUntil;      // NULL у БД = nullopt
    std::vector<TimeWindow> timeWindows;        // окрема таблиця, підвантажується CardRepository
};

inline std::string toString(CardStatus s) {
    switch (s) {
        case CardStatus::Active:  return "Active";
        case CardStatus::Blocked: return "Blocked";
        case CardStatus::Revoked: return "Revoked";
    }
    throw std::invalid_argument("Unknown CardStatus");
}

inline CardStatus cardStatusFromString(const std::string& s) {
    if (s == "Active")  return CardStatus::Active;
    if (s == "Blocked") return CardStatus::Blocked;
    if (s == "Revoked") return CardStatus::Revoked;
    throw std::invalid_argument("Unknown CardStatus: " + s);
}