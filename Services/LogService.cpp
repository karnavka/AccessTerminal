//
// Created by Христина Кузь on 07.11.2026.
//

#include "LogService.h"

LogService::LogService(LogRepository& logRepo): _logRepo(logRepo) {}

int LogService::log(std::optional<std::string> cardId, LogResult result, std::optional<LogReason> reason) {
    LogEntity entry;
    entry.cardId = cardId;
    entry.result = result;
    entry.reason = reason;
    return _logRepo.add(entry);
}

std::vector<LogEntity> LogService::getAll() const {
    return _logRepo.findAll();
}

std::vector<LogEntity> LogService::filter(const LogFilter& f) const {
    return _logRepo.filter(f);
}
