// Tests for TerminalController.
//
// No external framework: a tiny runner is built in (see the section below).
// Every test gets a fresh in-memory SQLite database created from data/schema.sql
// and the REAL services (CardService, SecurityService, AccessPolicyService, LogService),
// so a test checks the whole chain: controller -> services -> DB.
// Time is fake: the entry timeout and the card block duration are controlled by the test.
//
// Two groups of tests:
//   * event tests - call onCardTapped / onPinEntered / onDoorOpened ... directly;
//   * "e2e" tests - feed text lines (CARD, PIN, !open, !close) through run().
//     They need stdin to be NOT a real terminal, so they are skipped in an interactive terminal.
//     (ctest, the CLion Run window, or `./TerminalControllerTests < /dev/null` all run them.)
//
// Usage:
//   TerminalControllerTests            run everything
//   TerminalControllerTests -v         also show what the controller printed in every test
//   TerminalControllerTests forced     run only tests whose name contains "forced"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <optional>
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
}

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

// Replaces std::cin with a prepared script for the duration of a test.
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

// Opens an in-memory DB with the project schema. Separate base class so that the DB
// exists before the repositories in Fixture are constructed.
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
    DoorSimulator door;
    TerminalController terminal;

    explicit Fixture(std::chrono::seconds entryTimeout = 5s)
        : cards(db, cardRepo, hash, config, [this] { return wallNow; }),
          security(db, cards, logs, [this](AlarmReason reason) { alarms.push_back(reason); }),
          terminal(cards, security, access, logs, door, entryTimeout,
                   [this] { return steadyNow; }) {}

    // ---- preparing data ----

    static std::vector<TimeWindow> allWeek() {
        std::vector<TimeWindow> windows;
        for (int day = 0; day < 7; ++day) {
            windows.push_back(TimeWindow{day, 0, 1440});
        }
        return windows;
    }

    // Creates a user + a card with PIN "1234" directly through the repository.
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

    // Card block that expires after `duration` of the fake wall clock
    void blockFor(const std::string& id, std::chrono::minutes duration) {
        CardEntity c = card(id);
        c.status = CardStatus::Blocked;
        c.blockedUntil = wallNow + duration;
        cardRepo.update(c);
    }

    // Tap + correct PIN: the terminal ends up in WaitingForEntry
    void enter(const std::string& id) {
        addCard(id);
        terminal.onCardTapped(id);
        terminal.onPinEntered(kPin);
        CHECK(terminal.state() == State::WaitingForEntry);
    }

    // Simulates a broken database: card tables are gone (the log can stay or go too)
    void breakDatabase(bool alsoLog) {
        execSql(db, "DROP TABLE card_time_windows;");
        execSql(db, "DROP TABLE cards;");
        if (alsoLog) {
            execSql(db, "DROP TABLE access_log;");
        }
    }

    // ---- time ----
    void advance(std::chrono::seconds s)     { steadyNow += s; }
    void advanceWall(std::chrono::minutes m) { wallNow += m; }

    // ---- reading results ----
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

// Runs the text protocol through run(): every line is what a person would type.
// When the lines end, run() sees EOF and returns.
void runScript(Fixture& f, const std::string& script) {
    CinScript input(script);
    f.terminal.run();
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
    CHECK(f.logCount() == 0);   // nothing is logged until the PIN is checked
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

    f.terminal.onPinEntered(kPin);   // the PIN is right, but the card is not valid any more

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

TEST(forced_entry_during_pin_entry_aborts_the_session) {
    Fixture f;
    f.addCard("C1");
    f.terminal.onCardTapped("C1");

    f.terminal.onDoorOpened();
    f.terminal.onPinEntered(kPin);   // too late: the session is gone

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

    f.terminal.onDoorOpened();   // the lock is closed again, so this is a break-in

    CHECK(f.onlyAlarm(AlarmReason::ForcedOpen));
    CHECK(f.terminal.state() == State::Idle);
}

TEST(valid_card_works_even_if_the_door_was_left_open_after_a_break_in) {
    Fixture f;
    f.terminal.onDoorOpened();   // forced entry, the door stays open
    f.enter("C1");

    CHECK(f.door.isOpen());
    CHECK(!f.door.isLocked());

    f.terminal.onDoorClosed();   // closing it ends the session and locks

    CHECK(f.terminal.state() == State::Idle);
    CHECK(f.door.isLocked());
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
// Runner
// =====================================================================

int main(int argc, char** argv) {
    bool verbose = true;
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
    }

    std::streambuf* const realOut = std::cout.rdbuf();
    std::streambuf* const realErr = std::cerr.rdbuf();

    int passed = 0;
    int failed = 0;
    int skipped = 0;

    for (const TestCase& test : registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) {
            continue;
        }

        g_output.str("");
        g_output.clear();
        std::cout.rdbuf(g_output.rdbuf());
        std::cerr.rdbuf(g_output.rdbuf());

        std::string problem;
        bool skip = false;
        try {
            test.fn();
        } catch (const TestFailure& e) {
            problem = e.message;
        } catch (const Skip& s) {
            skip = true;
            problem = s.why;
        } catch (const std::exception& e) {
            problem = std::string("unexpected exception: ") + e.what();
        } catch (...) {
            problem = "unexpected unknown exception";
        }

        std::cout.rdbuf(realOut);
        std::cerr.rdbuf(realErr);

        const bool ok = problem.empty() && !skip;
        if (skip) {
            ++skipped;
            std::cout << "[ SKIP ] " << test.name << "\n         " << problem << '\n';
        } else if (ok) {
            ++passed;
            std::cout << "[  OK  ] " << test.name << '\n';
        } else {
            ++failed;
            std::cout << "[ FAIL ] " << test.name << "\n         " << problem << '\n';
        }

        if (verbose || (!ok && !skip)) {
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

