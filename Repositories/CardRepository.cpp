#include "CardRepository.h"
#include "SqliteStatement.h"

namespace {

const std::string kSelectCard =
    "SELECT card_id, user_id, pin_hash, status, failed_attempts, blocked_until FROM cards ";

CardEntity readCard(const Statement& st) {
    CardEntity c;
    c.cardId         = st.columnText(0);
    c.userId         = st.columnInt(1);
    c.pinHash        = st.columnText(2);
    c.status         = cardStatusFromString(st.columnText(3));
    c.failedAttempts = st.columnInt(4);
    if (auto s = st.columnOptText(5)) c.blockedUntil = fromIso(*s);
    return c;
}

std::optional<std::string> toIsoOpt(const std::optional<TimePoint>& tp) {
    if (tp) return toIso(*tp);
    return std::nullopt;
}

}

CardRepository::CardRepository(sqlite3* db) : db_(db) {}


void CardRepository::add(const CardEntity& card) {
    Savepoint sp(db_, "card_add");
    {
        Statement st(db_,
            "INSERT INTO cards (card_id, user_id, pin_hash, status, failed_attempts, blocked_until) "
            "VALUES (?, ?, ?, ?, ?, ?);");
        st.bind(1, card.cardId);
        st.bind(2, card.userId);
        st.bind(3, card.pinHash);
        st.bind(4, toString(card.status));
        st.bind(5, card.failedAttempts);
        st.bind(6, toIsoOpt(card.blockedUntil));
        st.step();
    }
    insertWindows(card.cardId, card.timeWindows);
    sp.release();
}

bool CardRepository::update(const CardEntity& card) {
    Statement st(db_,
        "UPDATE cards SET pin_hash = ?, status = ?, failed_attempts = ?, blocked_until = ? "
        "WHERE card_id = ?;");
    st.bind(1, card.pinHash);
    st.bind(2, toString(card.status));
    st.bind(3, card.failedAttempts);
    st.bind(4, toIsoOpt(card.blockedUntil));
    st.bind(5, card.cardId);
    st.step();
    return sqlite3_changes(db_) > 0;
}

void CardRepository::replaceTimeWindows(const std::string& cardId,
                                        const std::vector<TimeWindow>& windows) {
    Savepoint sp(db_, "card_windows");
    {
        Statement del(db_, "DELETE FROM card_time_windows WHERE card_id = ?;");
        del.bind(1, cardId);
        del.step();
    }
    insertWindows(cardId, windows);
    sp.release();
}

void CardRepository::insertWindows(const std::string& cardId,
                                   const std::vector<TimeWindow>& windows) {
    Statement st(db_,
        "INSERT INTO card_time_windows (card_id, day_of_week, start_minute, end_minute) "
        "VALUES (?, ?, ?, ?);");
    for (const TimeWindow& w : windows) {
        st.reset();
        st.bind(1, cardId);
        st.bind(2, w.dayOfWeek);
        st.bind(3, w.startMinute);
        st.bind(4, w.endMinute);
        st.step();
    }
}


std::optional<CardEntity> CardRepository::findById(const std::string& cardId) const {
    std::optional<CardEntity> card;
    {
        Statement st(db_, kSelectCard + "WHERE card_id = ?;");
        st.bind(1, cardId);
        if (!st.step()) return std::nullopt;
        card = readCard(st);
    }
    card->timeWindows = loadWindows(card->cardId);
    return card;
}

std::vector<CardEntity> CardRepository::findByUserId(int userId) const {
    std::vector<CardEntity> cards;
    {
        Statement st(db_, kSelectCard + "WHERE user_id = ? ORDER BY card_id;");
        st.bind(1, userId);
        while (st.step()) cards.push_back(readCard(st));
    }
    attachWindows(cards);
    return cards;
}

std::vector<CardEntity> CardRepository::findAll() const {
    std::vector<CardEntity> cards;
    {
        Statement st(db_, kSelectCard + "ORDER BY card_id;");
        while (st.step()) cards.push_back(readCard(st));
    }
    attachWindows(cards);
    return cards;
}

std::vector<TimeWindow> CardRepository::loadWindows(const std::string& cardId) const {
    Statement st(db_,
        "SELECT day_of_week, start_minute, end_minute FROM card_time_windows "
        "WHERE card_id = ? ORDER BY day_of_week, start_minute;");
    st.bind(1, cardId);
    std::vector<TimeWindow> windows;
    while (st.step())
        windows.push_back(TimeWindow{st.columnInt(0), st.columnInt(1), st.columnInt(2)});
    return windows;
}

void CardRepository::attachWindows(std::vector<CardEntity>& cards) const {
    for (CardEntity& c : cards) c.timeWindows = loadWindows(c.cardId);
}
