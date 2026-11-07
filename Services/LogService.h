//
// Created by Христина Кузь on 07.11.2026.
//

#pragma once

#include "../Repositories/LogRepository.h"

class LogService {
public:
    explicit LogService(LogRepository& logRepo);

    // логування події
    int log(std::optional<std::string> cardId, LogResult result, std::optional<LogReason> reason = std::nullopt);
    // отримати всі записи
    std::vector<LogEntity> getAll() const;
    // фільтрація
    std::vector<LogEntity> filter(const LogFilter& f) const;

private:
    LogRepository& _logRepo;
};