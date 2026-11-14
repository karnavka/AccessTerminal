#ifndef ACCESSTERMINAL_ACCESSPOLICYSERVICE_H
#define ACCESSTERMINAL_ACCESSPOLICYSERVICE_H

#include <optional>
#include <string>

#include "CardService.h"
#include "Entity/CardEntity.h"
#include "Entity/LogEntity.h"
#include "Entity/TimeUtil.h"

struct AccessDecision {
    bool allowed = false;
    std::optional<LogReason> reason;
};


class AccessPolicyService {
public:
    explicit AccessPolicyService(CardService& cardService);

    bool isAllowed(const std::string& cardId, TimePoint now);

    // як isAllowed але ще повертає причину
    AccessDecision check(const std::string& cardId, TimePoint now);


private:
    CardService& cardService_;
};

#endif //ACCESSTERMINAL_ACCESSPOLICYSERVICE_H
