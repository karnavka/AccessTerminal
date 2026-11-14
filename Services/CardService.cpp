//
// Created by Віра Вознюк on 07.11.2026.
//

#include "CardService.h"
#include <algorithm>
#include <stdexcept>
#include "Repositories/SqliteStatement.h"
#include "Entity/SecurityConfig.h"

//Закоментовані логи бо тре реалізувати і то шо або в аксес полісі або я хз чи його сюди
CardService::CardService(sqlite3 *db, CardRepository &cards, /*const HashService& hash,*/
                         SecurityConfig config, Clock clock)
    : db_(db), cards_(cards), /* hash_(hash),*/ config_(config), clock_(std::move(clock)) {
}
//кількст
void CardService::validatePin(const std::string &pin) {
    const bool digitsOnly = std::all_of(pin.begin(), pin.end(),
                                        [](unsigned char c) { return std::isdigit(c) != 0; });
    if (pin.size() < 4 || pin.size() > 8 || !digitsOnly) {
        throw std::invalid_argument("PIN має складатися з 4-8 цифр");
    }
}


CardEntity CardService::loadOrThrow(const std::string &cardId) const {
    auto card = cards_.findById(cardId);
    if (!card) throw std::invalid_argument("Картку не знайдено: " + cardId);
    return *card;
}

void CardService::save(const CardEntity &card) {
    if (!cards_.update(card)) {
        throw std::runtime_error("Не вдалося оновити картку: " + card.cardId);
    }
}

void CardService::issueCard(int userId, const std::string &cardId, const std::string &pin,
                            const std::vector<TimeWindow> &windows) {
    if (cardId.empty()) throw std::invalid_argument("Порожній ідентифікатор картки");
    validatePin(pin);

    validateWindows(windows);

    Savepoint sp(db_, "issue_card");

    if (cards_.findById(cardId)) {
        throw std::invalid_argument("Картка з таким ідентифікатором уже існує");
    }

    for (const CardEntity &c: cards_.findByUserId(userId))
        if (c.status != CardStatus::Revoked) {
            throw std::invalid_argument("У користувача вже є активна картка");
        }

    CardEntity card;
    card.cardId = cardId;
    card.userId = userId;
    // card.pinHash        = hash_.hash(pin);
    card.status = CardStatus::Active;
    card.failedAttempts = 0;
    card.timeWindows = windows;
    cards_.add(card);

    sp.release();
}

void CardService::revokeCard(const std::string &cardId) {
    Savepoint sp(db_, "revoke_card");
    CardEntity card = loadOrThrow(cardId);
    if (card.status == CardStatus::Revoked) {
        throw std::invalid_argument("Картка вже відкликана");
    }
    if (card.status != CardStatus::Revoked) {
        card.status = CardStatus::Revoked;
        card.blockedUntil.reset();
        save(card);
    }
    sp.release();
}

void CardService::blockCard(const std::string &cardId) {
    Savepoint sp(db_, "block_card");
    CardEntity card = loadOrThrow(cardId);
    if (card.status == CardStatus::Revoked) {
        throw std::invalid_argument("Відкликану картку блокувати не можна");
    }
    if (card.status != CardStatus::Blocked) {
        card.status = CardStatus::Blocked;
        card.blockedUntil.reset();
        save(card);
    }
    sp.release();
}

void CardService::unblockCard(const std::string &cardId) {
    Savepoint sp(db_, "unblock_card");
    CardEntity card = loadOrThrow(cardId);
    if (card.status != CardStatus::Blocked) {
        throw std::invalid_argument("Розблокувати можна лише заблоковану картку");
    }
    card.status = CardStatus::Active;
    card.failedAttempts = 0;
    card.blockedUntil.reset();
    save(card);
    sp.release();
}
/*
void CardService::changePin(const std::string &cardId, const std::string &newPin) {
    validatePin(newPin);
    Savepoint sp(db_, "change_pin");
    CardEntity card = loadOrThrow(cardId);
    if (card.status == CardStatus::Revoked) {
        throw std::invalid_argument("Картка відкликана");
    }
    // card.pinHash = hash_.hash(newPin);
    save(card);
    sp.release();
}*/

void CardService::validateTimeWindow(const TimeWindow& window) {
    if (window.dayOfWeek < 1 || window.dayOfWeek > 7) {
        throw std::invalid_argument("День тижня має бути від 1 до 7");
    }
    if (window.startMinute < 0 || window.startMinute >= 1440 || window.endMinute <= 0 || window.endMinute >= 1440) {
        throw std::invalid_argument("Некоректний час");
    }
    if (window.startMinute >= window.endMinute) {
        throw std::invalid_argument("Час початку має бути раніше часу завершення");
    }
}

// + щоб не перетиналися
void CardService::validateWindows(const std::vector<TimeWindow>& windows) {
    for (const TimeWindow& window : windows) {
        validateTimeWindow(window);
    }
    for (size_t i = 0; i < windows.size(); i++) {
        for (size_t j = i + 1; j < windows.size(); j++) {
            if (windows[i].dayOfWeek == windows[j].dayOfWeek && windows[i].startMinute < windows[j].endMinute &&
                windows[j].startMinute < windows[i].endMinute) {
                throw std::invalid_argument("Часові вікна не повинні перетинатися");
            }
        }
    }
}

void CardService::addTimeWindow(const std::string& cardId, const TimeWindow& window) {
    Savepoint sp(db_, "add_time_window");
    CardEntity card = loadOrThrow(cardId);
    if (card.status == CardStatus::Revoked) {
        throw std::invalid_argument("Картка відкликана");
    }
    card.timeWindows.push_back(window);
    validateWindows(card.timeWindows);
    cards_.replaceTimeWindows(cardId, card.timeWindows);
    sp.release();
}

void CardService::removeTimeWindow(const std::string& cardId, int windowIndex) {
    Savepoint sp(db_, "remove_time_window");
    CardEntity card = loadOrThrow(cardId);
    if (card.status == CardStatus::Revoked) {
        throw std::invalid_argument("Картка відкликана");
    }
    if (windowIndex < 0 || windowIndex >= static_cast<int>(card.timeWindows.size())) {
        throw std::invalid_argument("Неправильний номер часового вікна");
    }
    card.timeWindows.erase(card.timeWindows.begin() + windowIndex);
    cards_.replaceTimeWindows(cardId, card.timeWindows);
    sp.release();
}

void CardService::editTimeWindow(const std::string& cardId, int windowIndex, const TimeWindow& newWindow) {
    Savepoint sp(db_, "edit_time_window");
    CardEntity card = loadOrThrow(cardId);
    if (card.status == CardStatus::Revoked) {
        throw std::invalid_argument("Картка відкликана");
    }
    if (windowIndex < 0 || windowIndex >= static_cast<int>(card.timeWindows.size())) {
        throw std::invalid_argument("Неправильний номер часового вікна");
    }
    card.timeWindows[windowIndex] = newWindow;
    validateWindows(card.timeWindows);
    cards_.replaceTimeWindows(cardId, card.timeWindows);
    sp.release();
}

FailedAttemptResult CardService::registerFailedAttempt(const std::string &cardId) {
    Savepoint sp(db_, "failed_attempt");
    CardEntity card = loadOrThrow(cardId);
    FailedAttemptResult r;

    if (card.status != CardStatus::Active) {
        sp.release();
        return r;
    }

    card.failedAttempts += 1;
    if (card.failedAttempts >= config_.maxFailedAttempts) {
        card.status = CardStatus::Blocked;
        card.blockedUntil = clock_() + config_.blockDuration;
        r.blocked = true;
    } else {
        r.attemptsLeft = config_.maxFailedAttempts - card.failedAttempts;
    }
    save(card);
    sp.release();
    return r;
}

void CardService::resetFailedAttempt(const std::string &cardId) {
    Savepoint sp(db_, "reset_attempts");
    CardEntity card = loadOrThrow(cardId);
    if (card.failedAttempts != 0) {
        card.failedAttempts = 0;
        save(card);
    }
    sp.release();
}

bool CardService::releaseIfBlockExpired(const std::string &cardId) {
    Savepoint sp(db_, "auto_unblock");
    auto card = cards_.findById(cardId);
    if (!card || card->status != CardStatus::Blocked || !card->blockedUntil
        || clock_() < *card->blockedUntil) {
        sp.release();
        return false;
    }
    card->status = CardStatus::Active;
    card->failedAttempts = 0;
    card->blockedUntil.reset();
    save(*card);
    sp.release();
    return true;
}


std::optional<CardEntity> CardService::getCard(const std::string &cardId) const {
    return cards_.findById(cardId);
}

std::vector<CardEntity> CardService::getCardsByUser(int userId) const {
    return cards_.findByUserId(userId);
}

std::vector<CardEntity> CardService::getAllCards() const {
    return cards_.findAll();
}
