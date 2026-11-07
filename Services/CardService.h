//
// Created by Віра Вознюк on 07.11.2026.
//

#ifndef ACCESSTERMINAL_CARDSERVICE_H
#define ACCESSTERMINAL_CARDSERVICE_H


#pragma once
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "sqlite3.h"
#include "Entity/TimeUtil.h"
#include "Repositories/CardRepository.h"
#include "Entity/SecurityConfig.h"

//#include "SecurityConfig.h"

struct FailedAttemptResult {
    bool blocked = false;
    int attemptsLeft = 0;
};

class CardService {
public:
    using Clock = std::function<TimePoint()>;

    CardService(sqlite3 *db, CardRepository &cards, /*const HashService& hash,*/
                SecurityConfig config,
                Clock clock = [] { return std::chrono::system_clock::now(); });

    void issueCard(int userId, const std::string &cardId, const std::string &pin,
                   const std::vector<TimeWindow> &windows);

    void revokeCard(const std::string &cardId);

    void blockCard(const std::string &cardId);

    void unblockCard(const std::string &cardId);

   // void changePin(const std::string &cardId, const std::string &newPin);

   // void updateTimeWindows(const std::string &cardId, const std::vector<TimeWindow> &windows);

    FailedAttemptResult registerFailedAttempt(const std::string &cardId);

    void resetFailedAttempt(const std::string &cardId);

    bool releaseIfBlockExpired(const std::string &cardId);

    std::optional<CardEntity> getCard(const std::string &cardId) const;

    std::vector<CardEntity> getCardsByUser(int userId) const;

    std::vector<CardEntity> getAllCards() const;

private:
    CardEntity loadOrThrow(const std::string &cardId) const;

    void save(const CardEntity &card);

    static void validatePin(const std::string &pin);

    sqlite3 *db_;
    CardRepository &cards_;
    //  const HashService& hash_;
    SecurityConfig config_;
    Clock clock_;
};
#endif //ACCESSTERMINAL_CARDSERVICE_H
