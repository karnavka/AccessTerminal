#include "AccessPolicyService.h"

#include <chrono>
#include <ctime>
#include <vector>

std::tm toLocalTm(TimePoint tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    return local;
}

AccessDecision deny(LogReason reason) { return {false, reason}; }

bool isInWindow(const TimeWindow& w, int day, int minute) {
    return day == w.dayOfWeek && minute >= w.startMinute && minute < w.endMinute;
}

bool isInAnyWindow(const std::vector<TimeWindow>& windows, int day, int minute) {
    for (const TimeWindow& w : windows) {
        if (isInWindow(w, day, minute)) return true;
    }
    return false;
}

int dayOfWeek(TimePoint tp) {
    return (toLocalTm(tp).tm_wday + 6) % 7;
}

int minuteOfDay(TimePoint tp) {
    const std::tm local = toLocalTm(tp);
    return local.tm_hour * 60 + local.tm_min;
}




AccessPolicyService::AccessPolicyService(CardService& cardService)
    : cardService_(cardService) {}

AccessDecision AccessPolicyService::check(const std::string& cardId, TimePoint now) {
    auto card = cardService_.getCard(cardId);
    if (!card) {
        return deny(LogReason::UnknownCard);
    }

    if (card->status == CardStatus::Revoked) {
        return deny(LogReason::Revoked);
    }

    if (card->status == CardStatus::Blocked) {
        return deny(LogReason::Blocked);
    }

    if (!isInAnyWindow(card->timeWindows, dayOfWeek(now), minuteOfDay(now))) {
        return deny(LogReason::OutsideTimeWindow);
    }

    return {true, std::nullopt};
}
