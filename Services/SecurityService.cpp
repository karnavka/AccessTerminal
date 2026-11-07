//
// Created by Віра Вознюк on 07.11.2026.
//
#include"SecurityService.h"
#include "Repositories/SqliteStatement.h"

namespace {
    LogReason toLogReason(AlarmReason r) {
        switch (r) {
            case TooManyAttempts: return LogReason::TooManyAttempts;
            case ForcedOpen: return LogReason::ForcedOpen;
        }
        return LogReason::TooManyAttempts;
    }
}

void SecurityService::writeLog(const std::string &cardId, LogResult result, LogReason reason) const {
    LogEntity e;
    e.cardId = cardId;
    e.result = result;
    e.reason = reason;
    e.timestamp = clock_();
    log_.add(e);
}

void SecurityService::writeSuccess(const std::string &cardId) const {
    LogEntity e;
    e.cardId = cardId;
    e.timestamp = clock_();
    e.result = LogResult::Success;
    log_.add(e);
}

void SecurityService::triggerAlarm(const std::string &cardId, AlarmReason reason) const {
    {
        Savepoint sp(db_, "alarm");
        writeLog(cardId, LogResult::Alarm, toLogReason(reason));
        sp.release();
    }
    alarmSink_(reason);
}

WrongPinOut SecurityService::wrongPinOut(const std::string &cardId) const {
    WrongPinOut o;
    bool alarm = false;

    {
        Savepoint sp(db_, "wrong_pin");
        auto card = card_.findById(cardId);
        if (!card) {
            writeLog(cardId, LogResult::Denied, LogReason::UnknownCard);
            sp.release();
            return o;
        }
        switch (card->status) {
            case CardStatus::Active:
                break;
            case CardStatus::Blocked:
                writeLog(cardId, LogResult::Denied, LogReason::Blocked);
                sp.release();
                o.cardBlocked = true;
                return o;
            case CardStatus::Revoked:
                writeLog(cardId, LogResult::Denied, LogReason::Revoked);
                sp.release();
                return o;
        }

        card->failedAttempts += 1;
        writeLog(cardId, LogResult::Denied, LogReason::WrongPin);

        if (card->failedAttempts >= config_.maxFailedAttempts) {
            card->status = CardStatus::Blocked;
            card->blockedUntil = clock_() + config_.blockDuration;
            writeLog(cardId, LogResult::Alarm, LogReason::TooManyAttempts);
            alarm = true;
            o.cardBlocked = true;
        } else {
            o.attempts = config_.maxFailedAttempts - card->failedAttempts;
        }
        card_.update(*card);
        sp.release();
    }
    if (alarm) alarmSink_(TooManyAttempts);
    return o;
}

void SecurityService::onSuccessfulPin(const std::string &cardId) const {
    Savepoint sp(db_, "ok_pin");
    if (auto card = card_.findById(cardId)) {
        if (card->failedAttempts != 0) {
            card->failedAttempts = 0;
            card_.update(*card);
        }
    }
    writeSuccess(cardId);
    sp.release();
}

void SecurityService::writeLogNoCard(const LogResult result, LogReason reason) const {
    LogEntity e;
    e.timestamp = clock_();
    e.result = result;
    e.reason = reason;
    log_.add(e);
}

void SecurityService::triggerAlarm(AlarmReason reason) const {
    {
        Savepoint sp(db_, "alarm");
        writeLogNoCard(LogResult::Alarm, toLogReason(reason));
        sp.release();
    }
    alarmSink_(reason);
}
