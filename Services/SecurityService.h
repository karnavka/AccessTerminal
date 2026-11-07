//
// Created by Віра Вознюк on 06.11.2026.
//

#ifndef ACCESSTERMINAL_SECURITYSERVICE_H
#define ACCESSTERMINAL_SECURITYSERVICE_H
#include <chrono>
#include <functional>
#include "sqlite3.h"
#include "Entity/TimeUtil.h"
#include "Repositories/CardRepository.h"
#include "Repositories/LogRepository.h"

struct SecurityConfig {
    int maxFailedAttempts = 3;
    std::chrono::minutes blockDuration{15};

};
enum AlarmReason {TooManyAttempts, ForcedOpen};


struct WrongPinOut {
    bool cardBlocked = false;
    int attempts = 0;
};
class SecurityService {
public:
    using Clock = std::function<TimePoint()>;
    using AlarmSink = std::function<void(AlarmReason)>;

    SecurityService(sqlite3 *db, CardRepository &card, LogRepository &log,
                    SecurityConfig config, AlarmSink alarmSink, Clock clock = [] {
                        return std::chrono::system_clock::now();
                    }): db_(db), card_(card), log_(log), config_(config), alarmSink_(std::move(alarmSink)), clock_(std::move(clock)) {}

    WrongPinOut wrongPinOut(const std::string& cardId) const;
    void onSuccessfulPin(const std::string& cardId) const;
    void triggerAlarm(const std::string& cardId, AlarmReason reason) const;
    void triggerAlarm(AlarmReason reason) const;
private:
    void writeLog(const std::string& cardId, LogResult result, LogReason reason) const;
    void writeSuccess(const std::string& cardId) const;
    void writeLogNoCard(LogResult result, LogReason reason) const;

    sqlite3 *db_;
    CardRepository &card_;
    LogRepository &log_;
    SecurityConfig config_;
    AlarmSink alarmSink_;
    Clock clock_;

};

#endif //ACCESSTERMINAL_SECURITYSERVICE_H