// Tests for Controllers
//
// using in-memory SQLite database
// Time is fake: the entry timeout and the card block duration are controlled by the test.
//
// Usage: check README.md


#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "Entity/DoorSimulator.h"
#include "Entity/SecurityConfig.h"
#include "Repositories/CardRepository.h"
#include "Repositories/LogRepository.h"
#include "Repositories/SqliteStatement.h"
#include "Repositories/UserRepository.h"
#include "Services/AccessPolicyService.h"
#include "Services/CardService.h"
#include "Services/HashService.h"
#include "Services/LogService.h"
#include "Services/SecurityService.h"
#include "Services/UserService.h"

#include "ConsoleInput.h"
#include "AdminController.h"
#include "TerminalController.h"

namespace {

using namespace std::chrono_literals;
using State = TerminalController::State;

// =====================================================================
// Mini test framework
// =====================================================================

struct TestFailure { std::string message; };
struct Skip        { std::string why; };

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            throw TestFailure{std::string(__FILE__) + ":" + std::to_string(__LINE__) +  \
                              ": CHECK(" #cond ") failed"};                             \
        }                                                                               \
    } while (0)

#define CHECK_NOTHROW(expr)                                                             \
    do {                                                                                \
        try {                                                                           \
            expr;                                                                       \
        } catch (...) {                                                                 \
            throw TestFailure{std::string(__FILE__) + ":" + std::to_string(__LINE__) +  \
                              ": exception escaped from " #expr};                       \
        }                                                                               \
    } while (0)

struct TestCase {
    std::string name;
    void (*fn)();
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

#define TEST(name)                                          \
    void name();                                            \
    const Registrar registrar_##name(#name, &name);         \
    void name()

// Everything the controller prints (cout + cerr) during the current test.
std::ostringstream g_output;

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// Joins lines the way a person types them (every line ends with Enter).
std::string lines(std::initializer_list<std::string> items) {
    std::string text;
    for (const std::string& item : items) {
        text += item + '\n';
    }
    return text;
}

int countOf(const std::string& text, const std::string& part) {
    int n = 0;
    for (std::size_t pos = text.find(part); pos != std::string::npos;
         pos = text.find(part, pos + part.size())) {
        ++n;
    }
    return n;
}

std::string after(const std::string& text, const std::string& marker) {
    const std::size_t pos = text.find(marker);
    return pos == std::string::npos ? std::string() : text.substr(pos);
}

void requireNonInteractive() {
    if (console::isInteractive()) {
        throw Skip{"stdin/stdout is a real terminal; run with `< /dev/null` or from ctest/CLion"};
    }
}

//instead og cin
class CinScript {
public:
    explicit CinScript(const std::string& text) : _in(text), _old(std::cin.rdbuf(_in.rdbuf())) {
        std::cin.clear();
    }
    ~CinScript() {
        std::cin.rdbuf(_old);
        std::cin.clear();
    }
    CinScript(const CinScript&) = delete;
    CinScript& operator=(const CinScript&) = delete;

private:
    std::istringstream _in;
    std::streambuf* _old;
};

// =====================================================================
// Test fixture: DB + services + terminal with fake clocks
// =====================================================================

const std::string kPin = "1234";
const std::string kWrongPin = "0000";

const std::string& sharedPinHash() {
    static const std::string hash = HashService().hash(kPin);
    return hash;
}

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot read " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// 0 = Monday ... 6 = Sunday, the same numbering as in the DB. Local time, like AccessPolicyService.
int todayIndex() {
    const std::time_t t = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    return (local.tm_wday + 6) % 7;
}

// Today's local date as YYYY-MM-DD (the format the admin types in the log filter).
std::string todayDate() {
    const std::time_t t = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[11];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &local);
    return buf;
}

// Opens an in-memory DB
struct DbHandle {
    sqlite3* db = nullptr;

    DbHandle() {
        if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
            throw std::runtime_error("cannot open in-memory database");
        }
        execSql(db, "PRAGMA foreign_keys = ON;");
        execSql(db, readFile(std::string(PROJECT_DIR) + "/data/schema.sql"));
    }
    ~DbHandle() { sqlite3_close(db); }

    DbHandle(const DbHandle&) = delete;
    DbHandle& operator=(const DbHandle&) = delete;
};

struct Fixture : DbHandle {
    TimePoint wallNow = std::chrono::system_clock::now();//block duration
    std::chrono::steady_clock::time_point steadyNow = std::chrono::steady_clock::now();//entry timeout

    UserRepository userRepo{db};
    CardRepository cardRepo{db};
    LogRepository logRepo{db};
    HashService hash;
    SecurityConfig config{};          // 3 attempts, 15 min block
    UserService users{userRepo};
    CardService cards;
    AccessPolicyService access{cards};
    LogService logs{logRepo};
    std::vector<AlarmReason> alarms;  // everything the "siren" received
    SecurityService security;
    AdminController admin;
    DoorSimulator door;
    TerminalController terminal;

    explicit Fixture(std::chrono::seconds entryTimeout = 5s)
        : cards(db, cardRepo, hash, config, [this] { return wallNow; }),
        security(db, cards, logs, [this](AlarmReason reason) { alarms.push_back(reason); }),
        admin(users, cards, logs, hash, security),
        terminal(cards, security, access, logs, door, entryTimeout,
                [this] { return steadyNow; }) {}


    static std::vector<TimeWindow> allWeek() {
        std::vector<TimeWindow> windows;
        for (int day = 0; day < 7; ++day) {
            windows.push_back(TimeWindow{day, 0, 1440});
        }
        return windows;
    }

    void addCard(const std::string& id,
                 std::vector<TimeWindow> windows = allWeek(),
                 CardStatus status = CardStatus::Active) {
        CardEntity card;
        card.cardId = id;
        card.userId = users.addUser("Test " + id);
        card.pinHash = sharedPinHash();
        card.status = status;
        card.timeWindows = std::move(windows);
        cardRepo.add(card);
    }

    // Admin user + card with PIN "1234" - all week
    void addAdminCard(const std::string& id = "ADM") {
        CardEntity card;
        card.cardId = id;
        card.userId = users.addUser("Admin " + id, UserRole::Admin);
        card.pinHash = sharedPinHash();
        card.timeWindows = allWeek();
        cardRepo.add(card);
    }

    void blockFor(const std::string& id, std::chrono::minutes duration) {
        CardEntity c = card(id);
        c.status = CardStatus::Blocked;
        c.blockedUntil = wallNow + duration;
        cardRepo.update(c);
    }

    void enter(const std::string& id) {
        addCard(id);
        terminal.onCardTapped(id);
        terminal.onPinEntered(kPin);
        CHECK(terminal.state() == State::WaitingForEntry);
    }

    void breakDatabase(bool alsoLog) {
        execSql(db, "DROP TABLE card_time_windows;");
        execSql(db, "DROP TABLE cards;");
        if (alsoLog) {
            execSql(db, "DROP TABLE access_log;");
        }
    }

    void advance(std::chrono::seconds s)     { steadyNow += s; }
    void advanceWall(std::chrono::minutes m) { wallNow += m; }

    CardEntity card(const std::string& id) const { return cards.getCard(id).value(); }

    int logCount() const { return static_cast<int>(logs.getAll().size()); }

    int countLog(LogResult result) const {
        int n = 0;
        for (const LogEntity& e : logs.getAll()) {
            if (e.result == result) ++n;
        }
        return n;
    }

    int countLog(LogResult result, LogReason reason) const {
        int n = 0;
        for (const LogEntity& e : logs.getAll()) {
            if (e.result == result && e.reason && *e.reason == reason) ++n;
        }
        return n;
    }

    LogEntity firstLog(LogResult result) const {
        for (const LogEntity& e : logs.getAll()) {
            if (e.result == result) return e;
        }
        throw std::runtime_error("no such log entry");
    }

    bool onlyAlarm(AlarmReason reason) const { return alarms.size() == 1 && alarms[0] == reason; }
};

void runScript(Fixture& f, const std::string& script) {
    requireNonInteractive();
    CinScript input(script);
    f.terminal.run();
}

void runAdmin(Fixture& f, const std::string& script) {
    requireNonInteractive();
    CinScript input(script);
    f.admin.run();
}

// =====================================================================
// 1. Tapping a card (Idle)
// =====================================================================

TEST(tap_valid_card_asks_for_pin) {
    Fixture f;
    f.addCard("C1");

    f.terminal.onCardTapped("C1");

    CHECK(f.terminal.state() == State::WaitingForPin);
    CHECK(f.door.isLocked());
    CHECK(f.logCount() == 0);
}

TEST(tap_unknown_card_is_denied_and_logged) {
    Fixture f;

    f.terminal.onCardTapped("NOPE");

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
    CHECK(f.logCount() == 1);
    CHECK(f.countLog(LogResult::Denied, LogReason::UnknownCard) == 1);
    CHECK(f.firstLog(LogResult::Denied).cardId == std::string("NOPE"));
}

TEST(tap_revoked_card_is_denied) {
    Fixture f;
    f.addCard("C1", Fixture::allWeek(), CardStatus::Revoked);

    f.terminal.onCardTapped("C1");

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.countLog(LogResult::Denied, LogReason::Revoked) == 1);
}

TEST(tap_card_blocked_by_admin_is_denied) {
    Fixture f;
    f.addCard("C1", Fixture::allWeek(), CardStatus::Blocked);   // no expiry date

    f.terminal.onCardTapped("C1");

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.countLog(LogResult::Denied, LogReason::Blocked) == 1);
}

TEST(tap_card_with_unexpired_block_is_denied) {
    Fixture f;
    f.addCard("C1");
    f.blockFor("C1", 15min);
    f.advanceWall(14min);

    f.terminal.onCardTapped("C1");

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.countLog(LogResult::Denied, LogReason::Blocked) == 1);
}

TEST(tap_card_with_expired_block_is_released_and_accepted) {
    Fixture f;
    f.addCard("C1");
    f.blockFor("C1", 15min);
    f.advanceWall(16min);

    f.terminal.onCardTapped("C1");

    CHECK(f.terminal.state() == State::WaitingForPin);
    CHECK(f.card("C1").status == CardStatus::Active);
    CHECK(f.card("C1").failedAttempts == 0);
}

TEST(tap_card_without_time_windows_is_denied) {
    Fixture f;
    f.addCard("C1", {});

    f.terminal.onCardTapped("C1");

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.countLog(LogResult::Denied, LogReason::OutsideTimeWindow) == 1);
}

TEST(tap_card_with_window_on_another_weekday_is_denied) {
    Fixture f;
    const int otherDay = (todayIndex() + 1) % 7;
    f.addCard("C1", {TimeWindow{otherDay, 0, 1440}});

    f.terminal.onCardTapped("C1");

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.countLog(LogResult::Denied, LogReason::OutsideTimeWindow) == 1);
}

TEST(tap_card_with_window_covering_today_is_accepted) {
    Fixture f;
    // (a test run exactly at midnight could flip the weekday between these two lines)
    f.addCard("C1", {TimeWindow{todayIndex(), 0, 1440}});

    f.terminal.onCardTapped("C1");

    CHECK(f.terminal.state() == State::WaitingForPin);
}

TEST(tap_empty_card_id_is_ignored) {
    Fixture f;

    f.terminal.onCardTapped("");

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.logCount() == 0);
}

TEST(tap_while_waiting_for_pin_is_ignored) {
    Fixture f;
    f.addCard("C1");
    f.addCard("C2");
    f.terminal.onCardTapped("C1");

    f.terminal.onCardTapped("C2");   // must not replace the card that is being processed
    f.terminal.onPinEntered(kPin);

    CHECK(f.terminal.state() == State::WaitingForEntry);
    CHECK(f.logCount() == 1);
    CHECK(f.firstLog(LogResult::Success).cardId == std::string("C1"));
}

TEST(tap_with_sql_injection_like_id_is_just_unknown_card) {
    Fixture f;
    f.addCard("C1");

    f.terminal.onCardTapped("x' OR '1'='1");

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.countLog(LogResult::Denied, LogReason::UnknownCard) == 1);
    CHECK(f.cards.getAllCards().size() == 1);   // tables are intact
}

TEST(denial_message_does_not_reveal_the_reason) {
    Fixture f;
    f.addCard("REV", Fixture::allWeek(), CardStatus::Revoked);
    f.addCard("BLK", Fixture::allWeek(), CardStatus::Blocked);
    f.addCard("LATE", {});

    f.terminal.onCardTapped("NOPE");
    f.terminal.onCardTapped("REV");
    f.terminal.onCardTapped("BLK");
    f.terminal.onCardTapped("LATE");

    CHECK(f.countLog(LogResult::Denied) == 4);   // the log has the details...
    const std::string shown = g_output.str();    // ...the person at the door does not
    CHECK(!contains(shown, "UnknownCard"));
    CHECK(!contains(shown, "Revoked"));
    CHECK(!contains(shown, "Blocked"));
    CHECK(!contains(shown, "OutsideTimeWindow"));
}

// =====================================================================
// 2. Entering the PIN (WaitingForPin)
// =====================================================================

TEST(correct_pin_grants_access) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");

    f.terminal.onPinEntered(kPin);

    CHECK(f.terminal.state() == State::WaitingForEntry);
    CHECK(!f.door.isLocked());
    CHECK(!f.door.isOpen());
    CHECK(f.logCount() == 1);
    const LogEntity entry = f.firstLog(LogResult::Success);
    CHECK(entry.cardId == std::string("C1"));
    CHECK(!entry.reason.has_value());
    CHECK(f.card("C1").failedAttempts == 0);
    CHECK(f.alarms.empty());
}

TEST(wrong_pin_counts_an_attempt_and_keeps_waiting) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");

    f.terminal.onPinEntered(kWrongPin);

    CHECK(f.terminal.state() == State::WaitingForPin);
    CHECK(f.door.isLocked());
    CHECK(f.card("C1").failedAttempts == 1);
    CHECK(f.countLog(LogResult::Denied, LogReason::WrongPin) == 1);
    CHECK(f.alarms.empty());
}

TEST(correct_pin_after_wrong_ones_resets_the_counter) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");
    f.terminal.onPinEntered(kWrongPin);
    f.terminal.onPinEntered(kWrongPin);
    CHECK(f.card("C1").failedAttempts == 2);

    f.terminal.onPinEntered(kPin);

    CHECK(f.terminal.state() == State::WaitingForEntry);
    CHECK(f.card("C1").failedAttempts == 0);
    CHECK(f.countLog(LogResult::Denied, LogReason::WrongPin) == 2);
    CHECK(f.countLog(LogResult::Success) == 1);
}

TEST(three_wrong_pins_block_the_card_and_raise_the_alarm) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");

    f.terminal.onPinEntered(kWrongPin);
    f.terminal.onPinEntered(kWrongPin);
    f.terminal.onPinEntered(kWrongPin);

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
    CHECK(f.card("C1").status == CardStatus::Blocked);
    CHECK(f.card("C1").blockedUntil.has_value());
    CHECK(f.countLog(LogResult::Denied, LogReason::WrongPin) == 3);
    CHECK(f.countLog(LogResult::Alarm, LogReason::TooManyAttempts) == 1);
    CHECK(f.onlyAlarm(AlarmReason::TooManyAttempts));
}

TEST(blocked_card_is_denied_on_the_next_tap) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");
    f.terminal.onPinEntered(kWrongPin);
    f.terminal.onPinEntered(kWrongPin);
    f.terminal.onPinEntered(kWrongPin);

    f.terminal.onCardTapped("C1");

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.countLog(LogResult::Denied, LogReason::Blocked) == 1);
}

TEST(card_blocked_by_wrong_pins_works_again_after_the_block_expires) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");
    f.terminal.onPinEntered(kWrongPin);
    f.terminal.onPinEntered(kWrongPin);
    f.terminal.onPinEntered(kWrongPin);
    f.advanceWall(16min);   // block duration is 15 min

    f.terminal.onCardTapped("C1");
    f.terminal.onPinEntered(kPin);

    CHECK(f.terminal.state() == State::WaitingForEntry);
    CHECK(f.card("C1").status == CardStatus::Active);
    CHECK(f.card("C1").failedAttempts == 0);
}

TEST(manual_unblock_before_the_block_expires_restores_access) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");
    f.terminal.onPinEntered(kWrongPin);
    f.terminal.onPinEntered(kWrongPin);
    f.terminal.onPinEntered(kWrongPin);
    CHECK(f.card("C1").status == CardStatus::Blocked);
    f.advanceWall(1min);

    f.cards.unblockCard("C1");
    f.terminal.onCardTapped("C1");
    f.terminal.onPinEntered(kPin);

    CHECK(f.terminal.state() == State::WaitingForEntry);
    CHECK(f.card("C1").status == CardStatus::Active);
    CHECK(f.card("C1").failedAttempts == 0);
    CHECK(!f.card("C1").blockedUntil.has_value());
}

TEST(cancel_returns_to_idle_and_forgets_the_card) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");

    f.terminal.onCancel();
    f.terminal.onPinEntered(kPin);   // nobody is waiting for a PIN any more

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
    CHECK(f.logCount() == 0);
}

TEST(pin_without_a_card_is_ignored) {
    Fixture f;
    f.addCard("C1");

    f.terminal.onPinEntered(kPin);

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
    CHECK(f.logCount() == 0);
}

TEST(empty_pin_is_not_an_attempt) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");

    f.terminal.onPinEntered("");

    CHECK(f.terminal.state() == State::WaitingForPin);
    CHECK(f.card("C1").failedAttempts == 0);
    CHECK(f.logCount() == 0);
}

TEST(card_revoked_between_tap_and_pin_is_denied) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");
    f.cards.revokeCard("C1");

    f.terminal.onPinEntered(kPin); 

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
    CHECK(f.countLog(LogResult::Success) == 0);
    CHECK(f.countLog(LogResult::Denied, LogReason::Revoked) == 1);
}

TEST(card_blocked_between_tap_and_pin_is_denied) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");
    f.cards.blockCard("C1");

    f.terminal.onPinEntered(kPin);

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
    CHECK(f.countLog(LogResult::Success) == 0);
    CHECK(f.countLog(LogResult::Denied, LogReason::Blocked) == 1);
}

// =====================================================================
// 3. Door after a successful PIN (WaitingForEntry)
// =====================================================================

TEST(open_then_close_ends_the_session_and_locks) {
    Fixture f;
    f.enter("C1");

    f.terminal.onDoorOpened();
    CHECK(f.door.isOpen());
    CHECK(f.terminal.state() == State::WaitingForEntry);
    CHECK(!f.door.isLocked());

    f.terminal.onDoorClosed();
    CHECK(!f.door.isOpen());
    CHECK(f.door.isLocked());
    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.alarms.empty());
    CHECK(f.countLog(LogResult::Alarm) == 0);
}

TEST(nobody_enters_so_the_door_locks_after_the_timeout) {
    Fixture f;   // entry timeout = 5 s
    f.enter("C1");

    f.advance(4s);
    CHECK(!f.terminal.checkEntryTimeout());
    CHECK(f.terminal.state() == State::WaitingForEntry);
    CHECK(!f.door.isLocked());

    f.advance(1s);
    CHECK(f.terminal.checkEntryTimeout());
    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
    CHECK(f.alarms.empty());
}

TEST(timeout_does_not_fire_while_the_door_is_open) {
    Fixture f;
    f.enter("C1");
    f.terminal.onDoorOpened();

    f.advance(60s);

    CHECK(!f.terminal.checkEntryTimeout());
    CHECK(f.terminal.state() == State::WaitingForEntry);
    CHECK(!f.door.isLocked());

    f.terminal.onDoorClosed();
    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
}

TEST(another_card_is_ignored_while_the_door_is_unlocked) {
    Fixture f;
    f.enter("C1");
    f.addCard("C2");

    f.terminal.onCardTapped("C2");

    CHECK(f.terminal.state() == State::WaitingForEntry);
    CHECK(!f.door.isLocked());
    CHECK(f.logCount() == 1);   // only the first Success
}

TEST(next_visitor_can_tap_after_the_session_ended) {
    Fixture f;
    f.enter("C1");
    f.terminal.onDoorOpened();
    f.terminal.onDoorClosed();
    f.addCard("C2");

    f.terminal.onCardTapped("C2");

    CHECK(f.terminal.state() == State::WaitingForPin);
}

TEST(closing_a_closed_door_changes_nothing) {
    Fixture f;

    f.terminal.onDoorClosed();

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
    CHECK(f.alarms.empty());
}

// =====================================================================
// 4. Forced entry (door opened while the lock is closed)
// =====================================================================

TEST(door_opened_in_idle_is_a_forced_entry) {
    Fixture f;

    f.terminal.onDoorOpened();

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
    CHECK(f.door.isOpen());   // physically it is still open
    CHECK(f.onlyAlarm(AlarmReason::ForcedOpen));
    CHECK(f.countLog(LogResult::Alarm, LogReason::ForcedOpen) == 1);
    CHECK(!f.firstLog(LogResult::Alarm).cardId.has_value());
}

TEST(opening_an_already_open_door_is_not_a_second_alarm) {
    Fixture f;
    f.terminal.onDoorOpened();

    f.terminal.onDoorOpened();

    CHECK(f.alarms.size() == 1);
    CHECK(f.countLog(LogResult::Alarm) == 1);
}

TEST(every_new_forced_opening_raises_an_alarm) {
    Fixture f;

    f.terminal.onDoorOpened();
    f.terminal.onDoorClosed();
    f.terminal.onDoorOpened();

    CHECK(f.alarms.size() == 2);
    CHECK(f.countLog(LogResult::Alarm, LogReason::ForcedOpen) == 2);
}

TEST(forced_entry_does_not_block_cards_or_change_counters) {
    Fixture f;
    f.addCard("C1");
    f.cards.registerFailedAttempt("C1");  

    f.terminal.onDoorOpened();

    CHECK(f.onlyAlarm(AlarmReason::ForcedOpen));
    CHECK(f.card("C1").status == CardStatus::Active);
    CHECK(f.card("C1").failedAttempts == 1);
    CHECK(!f.card("C1").blockedUntil.has_value());
    CHECK(f.logCount() == 1);
    CHECK(!f.firstLog(LogResult::Alarm).cardId.has_value());
}

TEST(forced_entry_during_pin_entry_keeps_the_card_unchanged) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");
    f.terminal.onPinEntered(kWrongPin);

    f.terminal.onDoorOpened();

    CHECK(f.card("C1").status == CardStatus::Active);
    CHECK(f.card("C1").failedAttempts == 1);
    CHECK(f.countLog(LogResult::Alarm, LogReason::TooManyAttempts) == 0);
}

TEST(forced_entry_during_pin_entry_aborts_the_session) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");

    f.terminal.onDoorOpened();
    f.terminal.onPinEntered(kPin);

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
    CHECK(f.onlyAlarm(AlarmReason::ForcedOpen));
    CHECK(f.countLog(LogResult::Success) == 0);
}

TEST(opening_after_the_entry_timeout_is_a_forced_entry) {
    Fixture f;
    f.enter("C1");
    f.advance(6s);
    CHECK(f.terminal.checkEntryTimeout());

    f.terminal.onDoorOpened();

    CHECK(f.onlyAlarm(AlarmReason::ForcedOpen));
    CHECK(f.terminal.state() == State::Idle);
}

TEST(valid_card_works_even_if_the_door_was_left_open_after_a_break_in) {
    Fixture f;
    f.terminal.onDoorOpened();
    f.enter("C1");

    CHECK(f.door.isOpen());
    CHECK(!f.door.isLocked());

    f.terminal.onDoorClosed(); 

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
}

// =====================================================================
// 4b. Leaving the terminal (every exit resets the state, closes and locks the door)
// =====================================================================

TEST(leaving_the_terminal_with_the_door_open_closes_and_locks_it) {
    Fixture f;
    f.addCard("C1");

    // card, PIN, the person opens the door, then the input ends (like Ctrl+D)
    runScript(f, lines({"C1", kPin, "!open"}));

    CHECK(!f.door.isOpen());
    CHECK(f.door.isLocked());
    CHECK(f.terminal.state() == State::Idle);
}

TEST(leaving_the_terminal_after_a_break_in_closes_the_door) {
    Fixture f;

    runScript(f, lines({"!open", "0"}));

    CHECK(f.onlyAlarm(AlarmReason::ForcedOpen));
    CHECK(!f.door.isOpen());
    CHECK(f.door.isLocked());
    CHECK(f.terminal.state() == State::Idle);
}

TEST(leaving_the_terminal_while_waiting_for_pin_forgets_the_card) {
    Fixture f;
    f.addCard("C1");

    runScript(f, lines({"C1"}));   // the input ends while the PIN is expected

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
    f.terminal.onPinEntered(kPin);   // nobody is waiting for it
    CHECK(f.countLog(LogResult::Success) == 0);
}

// =====================================================================
// 5. Database failures (the terminal must fail closed and never crash)
// =====================================================================

TEST(db_failure_on_tap_denies_and_keeps_the_door_locked) {
    Fixture f;
    f.addCard("C1");
    f.breakDatabase(false);

    CHECK_NOTHROW(f.terminal.onCardTapped("C1"));

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
    CHECK(f.countLog(LogResult::Denied, LogReason::DbUnavailable) == 1);
}

TEST(db_failure_on_pin_denies_and_keeps_the_door_locked) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");
    f.breakDatabase(false);

    CHECK_NOTHROW(f.terminal.onPinEntered(kPin));

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
    CHECK(f.countLog(LogResult::Success) == 0);
    CHECK(f.countLog(LogResult::Denied, LogReason::DbUnavailable) == 1);
}

TEST(db_and_log_both_down_does_not_crash_on_tap) {
    Fixture f;
    f.addCard("C1");
    f.breakDatabase(true);

    CHECK_NOTHROW(f.terminal.onCardTapped("C1"));

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
}

TEST(db_and_log_both_down_does_not_crash_on_forced_entry) {
    Fixture f;
    f.breakDatabase(true);

    CHECK_NOTHROW(f.terminal.onDoorOpened());

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
}

// =====================================================================
// 6. Admin: login
// =====================================================================

TEST(admin_login_with_correct_pin_is_logged) {
    Fixture f;
    f.addAdminCard();

    runAdmin(f, lines({"ADM", kPin, "0"}));

    CHECK(contains(g_output.str(), "Вхід успішний"));
    CHECK(f.countLog(LogResult::Success) == 1);
    CHECK(f.firstLog(LogResult::Success).cardId == std::string("ADM"));
}

TEST(admin_login_with_wrong_pin_is_logged_and_the_card_is_not_blocked) {
    Fixture f;
    f.addAdminCard();

    runAdmin(f, lines({"ADM", kWrongPin, kPin, "0"}));

    const std::string shown = g_output.str();
    CHECK(contains(shown, "Неправильний PIN"));
    CHECK(contains(shown, "Вхід успішний"));
    CHECK(f.countLog(LogResult::Denied, LogReason::WrongPin) == 1);
    CHECK(f.card("ADM").status == CardStatus::Active);
}

TEST(admin_login_is_refused_for_a_regular_user_card) {
    Fixture f;
    f.addCard("C1");

    runAdmin(f, lines({"C1", kPin, "0"}));

    const std::string shown = g_output.str();
    CHECK(contains(shown, "Ви не адміністратор"));
    CHECK(!contains(shown, "МЕНЮ адміністратора"));
    CHECK(f.countLog(LogResult::Success) == 0);
}

TEST(admin_login_with_unknown_card_is_logged) {
    Fixture f;

    runAdmin(f, lines({"NOPE", "0"}));

    CHECK(contains(g_output.str(), "Картку не знайдено"));
    CHECK(f.countLog(LogResult::Denied, LogReason::UnknownCard) == 1);
}

TEST(admin_login_with_revoked_card_is_refused) {
    Fixture f;
    f.addAdminCard();
    f.cards.revokeCard("ADM");

    runAdmin(f, lines({"ADM", kPin, "0"}));

    CHECK(contains(g_output.str(), "Картку відкликано"));
    CHECK(f.countLog(LogResult::Denied, LogReason::Revoked) == 1);
    CHECK(f.countLog(LogResult::Success) == 0);
}

// =====================================================================
// 7. Admin: adding users and issuing cards
// =====================================================================

TEST(admin_issues_a_card_to_a_user_without_an_active_card) {
    Fixture f;
    f.addAdminCard();
    const int uid = f.users.addUser("Newcomer");

    // menu 3, user id, PIN, 1 window: Monday 08:00-18:00
    runAdmin(f, lines({"ADM", kPin, "3", std::to_string(uid), "4321", "1", "1", "08:00", "18:00", "0"}));

    const std::vector<CardEntity> issued = f.cards.getCardsByUser(uid);
    CHECK(issued.size() == 1);
    CHECK(issued[0].status == CardStatus::Active);
    CHECK(issued[0].failedAttempts == 0);
    CHECK(issued[0].timeWindows.size() == 1);
    CHECK(issued[0].timeWindows[0].dayOfWeek == 0);
    CHECK(issued[0].timeWindows[0].startMinute == 480);
    CHECK(issued[0].timeWindows[0].endMinute == 1080);
    CHECK(f.cards.verifyPin(issued[0].cardId, "4321"));
    CHECK(issued[0].pinHash != "4321");   // the PIN itself is never stored
}

TEST(issued_card_ids_are_unique_and_never_reuse_an_existing_id) {
    Fixture f;
    f.addCard("C1");
    std::set<std::string> ids{"C1"};

    for (int i = 0; i < 5; ++i) {
        const int uid = f.users.addUser("User " + std::to_string(i));
        const std::string id = f.cards.issueCard(uid, kPin, Fixture::allWeek());
        CHECK(ids.insert(id).second);
    }
}

TEST(card_with_an_existing_id_is_rejected_by_the_database) {
    Fixture f;
    f.addCard("C1");
    CardEntity duplicate = f.card("C1");
    duplicate.userId = f.users.addUser("Other");

    bool rejected = false;
    try {
        f.cardRepo.add(duplicate);
    } catch (const DbError&) {
        rejected = true;
    }

    CHECK(rejected);
    CHECK(f.cards.getAllCards().size() == 1);
    CHECK(f.card("C1").userId != duplicate.userId);   // the original is untouched
}

TEST(admin_cannot_issue_a_second_card_to_a_user_with_an_active_card) {
    Fixture f;
    f.addAdminCard();
    f.addCard("C1");
    const int uid = f.card("C1").userId;

    runAdmin(f, lines({"ADM", kPin, "3", std::to_string(uid), "0", "0"}));

    CHECK(contains(g_output.str(), "вже є активна картка"));
    CHECK(f.cards.getCardsByUser(uid).size() == 1);

    bool rejected = false;   // the service refuses it too, not only the console
    try {
        f.cards.issueCard(uid, kPin, Fixture::allWeek());
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(f.cards.getCardsByUser(uid).size() == 1);
}

TEST(admin_cannot_issue_a_card_to_a_user_whose_card_is_only_blocked) {
    Fixture f;
    f.addAdminCard();
    f.addCard("C1", Fixture::allWeek(), CardStatus::Blocked);
    const int uid = f.card("C1").userId;

    runAdmin(f, lines({"ADM", kPin, "3", std::to_string(uid), "4321", "1", "1", "08:00", "18:00", "0"}));

    CHECK(contains(g_output.str(), "Не вдалося додати картку"));
    CHECK(f.cards.getCardsByUser(uid).size() == 1);
}

TEST(admin_issue_card_rejects_bad_user_id_and_bad_pin) {
    Fixture f;
    f.addAdminCard();
    const int uid = f.users.addUser("Newcomer");

    // bad ids: text, negative, unknown; then bad PINs: too short, letters, too long; then cancel
    runAdmin(f, lines({"ADM", kPin, "3", "abc", "-5", "999", std::to_string(uid),
                       "12", "abcd", "123456789", "0", "0"}));

    const std::string shown = g_output.str();
    CHECK(contains(shown, "Некоректний ID користувача"));
    CHECK(contains(shown, "не знайдено"));
    CHECK(contains(shown, "4-8 цифр"));
    CHECK(f.cards.getCardsByUser(uid).empty());
}

TEST(admin_issue_card_rejects_bad_time_windows) {
    Fixture f;
    f.addAdminCard();
    const int uid = f.users.addUser("Newcomer");

    // day 8 is wrong, 25:00 is wrong, then end before start; then cancel at the day prompt
    runAdmin(f, lines({"ADM", kPin, "3", std::to_string(uid), "1234", "1",
                       "8", "1", "25:00", "12:00", "10:00", "0", "0"}));

    const std::string shown = g_output.str();
    CHECK(contains(shown, "Некоректний день тижня"));
    CHECK(contains(shown, "Некоректний формат часу"));
    CHECK(contains(shown, "Час початку має бути раніше"));
    CHECK(f.cards.getCardsByUser(uid).empty());
}

TEST(admin_adds_users_with_both_roles) {
    Fixture f;
    f.addAdminCard();

    runAdmin(f, lines({"ADM", kPin, "2", "Марія", "1", "2", "Олег", "2", "0"}));

    bool maria = false;
    bool oleh = false;
    for (const UserEntity& u : f.users.getAllUsers()) {
        if (u.name == "Марія" && u.role == UserRole::User) maria = true;
        if (u.name == "Олег" && u.role == UserRole::Admin) oleh = true;
    }
    CHECK(maria);
    CHECK(oleh);
}

TEST(admin_cannot_add_a_user_with_an_empty_name) {
    Fixture f;
    f.addAdminCard();
    const std::size_t before = f.users.getAllUsers().size();

    runAdmin(f, lines({"ADM", kPin, "2", "", "   ", "0", "0"}));

    CHECK(contains(g_output.str(), "Ім'я не може бути порожнім"));
    CHECK(f.users.getAllUsers().size() == before);

    bool rejected = false;   // and the service refuses it too
    try {
        f.users.addUser("   ");
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(f.users.getAllUsers().size() == before);
}

// =====================================================================
// 8. Admin: editing a card (PIN, time windows, block / unblock)
// =====================================================================

TEST(admin_changes_the_pin) {
    Fixture f;
    f.addAdminCard();
    f.addCard("C1");

    // menu 4, card id, 1 = change PIN, new PIN, back, exit
    runAdmin(f, lines({"ADM", kPin, "4", "C1", "1", "5678", "0", "0"}));

    CHECK(f.cards.verifyPin("C1", "5678"));
    CHECK(!f.cards.verifyPin("C1", kPin));
}

TEST(admin_invalid_pin_keeps_the_old_one) {
    Fixture f;
    f.addAdminCard();
    f.addCard("C1");
    const std::string hashBefore = f.card("C1").pinHash;

    runAdmin(f, lines({"ADM", kPin, "4", "C1", "1", "12", "abcd", "123456789", "0", "0", "0"}));

    CHECK(contains(g_output.str(), "4-8 цифр"));
    CHECK(f.card("C1").pinHash == hashBefore);
    CHECK(f.cards.verifyPin("C1", kPin));
}

TEST(admin_adds_a_time_window) {
    Fixture f;
    f.addAdminCard();
    f.addCard("C1", {TimeWindow{0, 480, 1200}});

    // 4 = edit windows, 1 = add: Tuesday 09:00-10:00
    runAdmin(f, lines({"ADM", kPin, "4", "C1", "4", "1", "2", "09:00", "10:00", "0", "0", "0"}));

    const std::vector<TimeWindow> w = f.card("C1").timeWindows;
    CHECK(w.size() == 2);
    CHECK(w[1].dayOfWeek == 1 && w[1].startMinute == 540 && w[1].endMinute == 600);
}

TEST(admin_overlapping_time_window_is_rejected_and_nothing_changes) {
    Fixture f;
    f.addAdminCard();
    f.addCard("C1", {TimeWindow{0, 480, 1200}});

    // Monday 10:00-12:00 overlaps Monday 08:00-20:00; then cancel
    runAdmin(f, lines({"ADM", kPin, "4", "C1", "4", "1", "1", "10:00", "12:00", "0", "0", "0", "0"}));

    CHECK(contains(g_output.str(), "перетинається"));
    const std::vector<TimeWindow> w = f.card("C1").timeWindows;
    CHECK(w.size() == 1);
    CHECK(w[0].dayOfWeek == 0 && w[0].startMinute == 480 && w[0].endMinute == 1200);
}

TEST(admin_edits_a_time_window) {
    Fixture f;
    f.addAdminCard();
    f.addCard("C1", {TimeWindow{0, 480, 1200}});

    // 3 = edit window, window #1 becomes Wednesday 10:00-11:00
    runAdmin(f, lines({"ADM", kPin, "4", "C1", "4", "3", "1", "3", "10:00", "11:00", "0", "0", "0"}));

    const std::vector<TimeWindow> w = f.card("C1").timeWindows;
    CHECK(w.size() == 1);
    CHECK(w[0].dayOfWeek == 2 && w[0].startMinute == 600 && w[0].endMinute == 660);
}

TEST(admin_removes_a_time_window) {
    Fixture f;
    f.addAdminCard();
    f.addCard("C1", {TimeWindow{0, 480, 1200}, TimeWindow{1, 480, 1200}});

    // 2 = remove window, window #1
    runAdmin(f, lines({"ADM", kPin, "4", "C1", "4", "2", "1", "0", "0", "0"}));

    const std::vector<TimeWindow> w = f.card("C1").timeWindows;
    CHECK(w.size() == 1);
    CHECK(w[0].dayOfWeek == 1);
}

TEST(admin_bad_window_number_changes_nothing) {
    Fixture f;
    f.addAdminCard();
    f.addCard("C1", {TimeWindow{0, 480, 1200}});

    // remove: window #7 does not exist, then cancel
    runAdmin(f, lines({"ADM", kPin, "4", "C1", "4", "2", "7", "abc", "0", "0", "0", "0"}));

    CHECK(contains(g_output.str(), "Некоректний номер часового вікна"));
    CHECK(f.card("C1").timeWindows.size() == 1);
}

TEST(admin_edit_of_an_unknown_card_is_rejected) {
    Fixture f;
    f.addAdminCard();

    runAdmin(f, lines({"ADM", kPin, "4", "NOPE", "0", "0"}));

    CHECK(contains(g_output.str(), "Картку не знайдено"));
}

TEST(admin_cannot_edit_a_revoked_card) {
    Fixture f;
    f.addAdminCard();
    f.addCard("C1", Fixture::allWeek(), CardStatus::Revoked);

    runAdmin(f, lines({"ADM", kPin, "4", "C1", "0"}));

    CHECK(contains(g_output.str(), "змінювати її вже не можна"));
    CHECK(f.card("C1").status == CardStatus::Revoked);
}

TEST(admin_blocks_a_card) {
    Fixture f;
    f.addAdminCard();
    f.addCard("C1");

    // 5 = block (the menu is shown again afterwards, then back, exit)
    runAdmin(f, lines({"ADM", kPin, "4", "C1", "5", "0", "0"}));

    CHECK(f.card("C1").status == CardStatus::Blocked);
    f.terminal.onCardTapped("C1");
    CHECK(f.countLog(LogResult::Denied, LogReason::Blocked) == 1);
}

TEST(admin_unblocks_a_card_blocked_by_wrong_pins) {
    Fixture f;
    f.addAdminCard();
    f.addCard("C1");
    f.terminal.onCardTapped("C1");
    f.terminal.onPinEntered(kWrongPin);
    f.terminal.onPinEntered(kWrongPin);
    f.terminal.onPinEntered(kWrongPin);
    CHECK(f.card("C1").status == CardStatus::Blocked);

    // 3 = unblock (only offered for blocked cards)
    runAdmin(f, lines({"ADM", kPin, "4", "C1", "3", "0", "0"}));

    CHECK(f.card("C1").status == CardStatus::Active);
    CHECK(f.card("C1").failedAttempts == 0);
    CHECK(!f.card("C1").blockedUntil.has_value());
    f.terminal.onCardTapped("C1");
    CHECK(f.terminal.state() == State::WaitingForPin);
}

// =====================================================================
// 9. Admin: revoking a card
// =====================================================================

TEST(admin_revokes_a_card) {
    Fixture f;
    f.addAdminCard();
    f.addCard("C1");

    // 2 = revoke, confirm; a revoked card is not editable, so the edit menu closes by itself
    runAdmin(f, lines({"ADM", kPin, "4", "C1", "2", "так", "0"}));

    CHECK(f.card("C1").status == CardStatus::Revoked);
    CHECK(!f.card("C1").blockedUntil.has_value());
    f.terminal.onCardTapped("C1");
    CHECK(f.countLog(LogResult::Denied, LogReason::Revoked) == 1);
}

TEST(admin_declining_the_confirmation_keeps_the_card) {
    Fixture f;
    f.addAdminCard();
    f.addCard("C1");

    runAdmin(f, lines({"ADM", kPin, "4", "C1", "2", "ні", "0", "0"}));

    CHECK(contains(g_output.str(), "Відкликання скасовано"));
    CHECK(f.card("C1").status == CardStatus::Active);
}

TEST(admin_cannot_revoke_the_card_they_are_logged_in_with) {
    Fixture f;
    f.addAdminCard();

    runAdmin(f, lines({"ADM", kPin, "4", "ADM", "2", "0", "0"}));

    CHECK(contains(g_output.str(), "Неможливо відкликати картку адміна"));
    CHECK(f.card("ADM").status == CardStatus::Active);
}

// =====================================================================
// 10. Admin: viewing and filtering the log
// =====================================================================

// Three entries of different kinds. The admin's own login adds a fourth (Success, ADM).
void addSampleLog(Fixture& f) {
    f.logs.log(std::string("L1"), LogResult::Denied, LogReason::WrongPin);
    f.logs.log(std::string("L2"), LogResult::Success);
    f.logs.log(std::nullopt, LogResult::Alarm, LogReason::ForcedOpen);
}

TEST(admin_sees_all_log_entries) {
    Fixture f;
    f.addAdminCard();
    addSampleLog(f);

    // 1 = information, 3 = log, 0 = no filtering, back, exit
    runAdmin(f, lines({"ADM", kPin, "1", "3", "0", "0", "0"}));

    const std::string shown = g_output.str();
    CHECK(f.logCount() == 4);
    CHECK(countOf(shown, "ID запису:") == 4);
    CHECK(contains(shown, "L1"));
    CHECK(contains(shown, "L2"));
    CHECK(contains(shown, "WrongPin"));
    CHECK(contains(shown, "ForcedOpen"));
}

TEST(admin_filters_the_log_by_result) {
    Fixture f;
    f.addAdminCard();
    addSampleLog(f);

    // 1 = by result, 2 = Denied
    runAdmin(f, lines({"ADM", kPin, "1", "3", "1", "2", "0", "0"}));

    const std::string filtered = after(g_output.str(), "Результат фільтрації");
    CHECK(countOf(filtered, "ID запису:") == 1);
    CHECK(contains(filtered, "L1"));
    CHECK(!contains(filtered, "L2"));
}

TEST(admin_filters_the_log_by_date) {
    Fixture f;
    f.addAdminCard();
    addSampleLog(f);

    // (a test run exactly at midnight could flip the date)
    runAdmin(f, lines({"ADM", kPin, "1", "3", "2", todayDate(), "0", "0"}));

    const std::string filtered = after(g_output.str(), "Результат фільтрації");
    CHECK(countOf(filtered, "ID запису:") == 4);
}

TEST(admin_log_filter_by_another_date_finds_nothing) {
    Fixture f;
    f.addAdminCard();
    addSampleLog(f);

    runAdmin(f, lines({"ADM", kPin, "1", "3", "2", "2000-01-01", "0", "0"}));

    const std::string filtered = after(g_output.str(), "Результат фільтрації");
    CHECK(contains(filtered, "Записів за заданим фільтром не знайдено"));
    CHECK(countOf(filtered, "ID запису:") == 0);
}

TEST(admin_log_filter_rejects_an_invalid_date) {
    Fixture f;
    f.addAdminCard();
    addSampleLog(f);

    runAdmin(f, lines({"ADM", kPin, "1", "3", "2", "2026-13-45", "yesterday", "0", "0", "0"}));

    const std::string shown = g_output.str();
    CHECK(contains(shown, "Некоректна дата"));
    CHECK(!contains(shown, "Результат фільтрації"));
}

TEST(admin_filters_the_log_by_card_id) {
    Fixture f;
    f.addAdminCard();
    addSampleLog(f);

    runAdmin(f, lines({"ADM", kPin, "1", "3", "3", "L1", "0", "0"}));

    const std::string filtered = after(g_output.str(), "Результат фільтрації");
    CHECK(countOf(filtered, "ID запису:") == 1);
    CHECK(contains(filtered, "L1"));
    CHECK(!contains(filtered, "L2"));
}

TEST(admin_log_filter_by_unknown_card_id_finds_nothing) {
    Fixture f;
    f.addAdminCard();
    addSampleLog(f);

    runAdmin(f, lines({"ADM", kPin, "1", "3", "3", "NONE", "0", "0"}));

    const std::string filtered = after(g_output.str(), "Результат фільтрації");
    CHECK(contains(filtered, "Записів за заданим фільтром не знайдено"));
}

} // namespace

// =====================================================================
// Runner
// =====================================================================

int main(int argc, char** argv) {
    bool verbose = true;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
    }

    std::streambuf* const realOut = std::cout.rdbuf();
    std::streambuf* const realErr = std::cerr.rdbuf();

    int passed = 0;
    int failed = 0;
    int skipped = 0;

    for (const TestCase& test : registry()) {

        g_output.str("");
        g_output.clear();
        std::cout.rdbuf(g_output.rdbuf());
        std::cerr.rdbuf(g_output.rdbuf());

        std::string problem;
        // bool skip = false;
        try {
            test.fn();
        } catch (const TestFailure& e) {
            problem = e.message;
        } catch (const Skip& s) {
            // skip = true;
            // problem = s.why;
        } catch (const std::exception& e) {
            problem = std::string("unexpected exception: ") + e.what();
        } catch (...) {
            problem = "unexpected unknown exception";
        }

        std::cout.rdbuf(realOut);
        std::cerr.rdbuf(realErr);

        const bool ok = problem.empty() /* && !skip */;
        // if (skip) {
        //     ++skipped;
        //     std::cout << "[ SKIP ] " << test.name << "\n         " << problem << '\n';
        // } else
        if (ok) {
            ++passed;
            std::cout << "[  OK  ] " << test.name << '\n';
        } else {
            ++failed;
            std::cout << "[ FAIL ] " << test.name << "\n         " << problem << '\n';
        }

        if (verbose || (!ok /*&& !skip*/)) {
            std::istringstream lines(g_output.str());
            std::string line;
            while (std::getline(lines, line)) {
                std::cout << "           | " << line << '\n';
            }
        }
    }

    std::cout << "\n" << passed << " passed, " << failed << " failed, " << skipped << " skipped\n";
    return failed == 0 ? 0 : 1;
}