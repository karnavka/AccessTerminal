//
// Created by Віра Вознюк on 06.11.2026.
//

#ifndef ACCESSTERMINAL_SECURITYSERVICE_H
#define ACCESSTERMINAL_SECURITYSERVICE_H
#include <chrono>
#include <functional>

#include "CardService.h"
#include "sqlite3.h"

struct SecurityConfig {
    int maxFailedAttempts = 3;
    std::chrono::minutes blockDuration{15};
};

enum AlarmReason { TooManyAttempts, ForcedOpen };

struct WrongPinOut {
    bool cardBlok = false;
    int attempts = 0;
};

class SecurityService {
public:
    using AlarmSink = std::function<void(AlarmReason)>;

    SecurityService(sqlite3 *db, CardService &cards, /*LogService& logs,*/ AlarmSink alarmSink)
        : db_(db), cards_(cards), /*logs_(logs),*/ alarmSink_(std::move(alarmSink)) {
    }

    WrongPinOut wrongPinOut(const std::string &cardId);

    void onSuccessfulPin(const std::string &cardId);

    void triggerAlarm(const std::string &cardId, AlarmReason reason);

    void triggerAlarm(AlarmReason reason);

private:
    sqlite3 *db_;
    CardService &cards_;
    //LogService&  logs_;
    AlarmSink alarmSink_;
};
#endif //ACCESSTERMINAL_SECURITYSERVICE_H
