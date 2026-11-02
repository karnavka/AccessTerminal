#pragma once
#include <optional>
#include <stdexcept>
#include <string>
#include "TimeUtil.h"

enum class LogResult { Success, Denied, Alarm };

enum class LogReason {
    WrongPin, UnknownCard, Revoked, Blocked,
    OutsideTimeWindow, DbUnavailable,
    TooManyAttempts, ForcedOpen
};

struct LogEntity {
    int id;
    TimePoint timestamp = now();
    std::optional<std::string> cardId; 
    LogResult result;
    std::optional<LogReason> reason;
};


// inline std::string toString(LogResult r) {
//     switch (r) {
//         case LogResult::Success: return "Success";
//         case LogResult::Denied:  return "Denied";
//         case LogResult::Alarm:   return "Alarm";
//     }
//     throw std::invalid_argument("Unknown LogResult");
// }

// inline LogResult logResultFromString(const std::string& s) {
//     if (s == "Success") return LogResult::Success;
//     if (s == "Denied")  return LogResult::Denied;
//     if (s == "Alarm")   return LogResult::Alarm;
//     throw std::invalid_argument("Unknown LogResult: " + s);
// }

// inline std::string toString(LogReason r) {
//     switch (r) {
//         case LogReason::WrongPin:          return "WrongPin";
//         case LogReason::UnknownCard:       return "UnknownCard";
//         case LogReason::Revoked:           return "Revoked";
//         case LogReason::Blocked:           return "Blocked";
//         case LogReason::OutsideTimeWindow: return "OutsideTimeWindow";
//         case LogReason::DbUnavailable:     return "DbUnavailable";
//         case LogReason::TooManyAttempts:   return "TooManyAttempts";
//         case LogReason::ForcedOpen:        return "ForcedOpen";
//     }
//     throw std::invalid_argument("Unknown LogReason");
// }

// inline LogReason logReasonFromString(const std::string& s) {
//     if (s == "WrongPin")          return LogReason::WrongPin;
//     if (s == "UnknownCard")       return LogReason::UnknownCard;
//     if (s == "Revoked")           return LogReason::Revoked;
//     if (s == "Blocked")           return LogReason::Blocked;
//     if (s == "OutsideTimeWindow") return LogReason::OutsideTimeWindow;
//     if (s == "DbUnavailable")     return LogReason::DbUnavailable;
//     if (s == "TooManyAttempts")   return LogReason::TooManyAttempts;
//     if (s == "ForcedOpen")        return LogReason::ForcedOpen;
//     throw std::invalid_argument("Unknown LogReason: " + s);
// }