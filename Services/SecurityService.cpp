//
// Created by Віра Вознюк on 07.11.2026.
//
#include"SecurityService.h"
#include "Entity/LogEntity.h"
#include "Repositories/SqliteStatement.h"
#include "Entity/SecurityConfig.h"

namespace {
    LogReason toLogReason(AlarmReason r) {
        switch (r) {
            case TooManyAttempts: return LogReason::TooManyAttempts;
            case ForcedOpen: return LogReason::ForcedOpen;
        }
        return LogReason::TooManyAttempts;
    }
}

WrongPinOut SecurityService::wrongPinOut(const std::string &cardId) {
    WrongPinOut o;
    bool alarm = false;
    {
        Savepoint sp(db_, "wrong_pin");

        auto card = cards_.getCard(cardId);
        if (!card) {
            logs_.log(cardId, LogResult::Denied, LogReason::UnknownCard);
            sp.release();
            return o;
        }
        switch (card->status) {
            case CardStatus::Active:
                break;
            case CardStatus::Blocked:
                logs_.log(cardId, LogResult::Denied, LogReason::Blocked);
                sp.release();
                return o;
            case CardStatus::Revoked:
                logs_.log(cardId, LogResult::Denied, LogReason::Revoked);
                sp.release();
                return o;
        }

        const FailedAttemptResult r = cards_.registerFailedAttempt(cardId);
        logs_.log(cardId, LogResult::Denied, LogReason::WrongPin);

        if (r.blocked) {
            logs_.log(cardId, LogResult::Alarm, LogReason::TooManyAttempts);
            o.cardBlok = true;
            alarm = true;
        } else {
            o.attempts = r.attemptsLeft;
        }
        sp.release();
    }
    if (alarm) {
        alarmSink_(TooManyAttempts);
    }
    return o;
}

void SecurityService::onSuccessfulPin(const std::string &cardId) {
    Savepoint sp(db_, "ok_pin");
    cards_.resetFailedAttempt(cardId);
    logs_.log(cardId, LogResult::Success);
    sp.release();
}

void SecurityService::triggerAlarm(const std::string &cardId, AlarmReason reason) {
    {
        Savepoint sp(db_, "alarm");
        logs_.log(cardId, LogResult::Alarm, toLogReason(reason));
        sp.release();
    }
    alarmSink_(reason);
}

void SecurityService::triggerAlarm(AlarmReason reason) {
    {
        Savepoint sp(db_, "alarm");
        logs_.log(std::nullopt, LogResult::Alarm, toLogReason(reason));
        sp.release();
    }
    alarmSink_(reason);
}
