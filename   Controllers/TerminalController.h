#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include "../Services/UserService.h"
#include "../Services/CardService.h"
#include "../Services/LogService.h"
#include "../Services/SecurityService.h"
#include "../Services/AccessPolicyService.h"

#include "../Entity/DoorSimulator.h"


class TerminalController {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    enum class State {
        Idle,               //S0
        WaitingForPin,      //S1
        WaitingForEntry     //S5
    };

    TerminalController(CardService& cards,
                       SecurityService& security,
                       AccessPolicyService& access,
                       LogService& logs,
                       DoorSimulator& door,
                       std::chrono::seconds entryTimeout,
                       Clock clock = [] { return std::chrono::steady_clock::now(); });

    ~TerminalController();

    void run();

    // ---- події ----
    void onCardTapped(const std::string& cardId);
    void onPinEntered(const std::string& pin);
    void onCancel();
    void onDoorOpened();
    void onDoorClosed(); 
    bool checkEntryTimeout();
    State state() const { return _state; }

private:
    enum class InputKind { Line, Interrupted, Eof };
    struct Input {
        InputKind kind;
        std::string text;
    };

    enum class InputMode { Plain, Masked, KeysOnly };

    // ---- ввід ----
    Input readInput();
    Input readKeys(const std::string& prompt, InputMode mode);
    Input readLineBlocking(); //for tests
    std::string promptFor(bool tty) const;
    InputMode inputMode() const;

    bool handleLine(const std::string& text);

    bool handleDoorCommand(const std::string& text);
    void toggleDoor();

    // ---- логіка ----
    void handleFailedAttempt(const std::string& cardId);
    void grantAccess();
    void onForcedOpened();
    bool entryExpired() const;
    void denyAccess(LogReason reason, const std::string& cardId);
    void abortOnError(const std::string& cardId, const std::exception& e);
    void endSession();
    void shutdown();

    CardService& _cards;
    SecurityService& _security;
    AccessPolicyService& _access;
    LogService& _logs;
    DoorSimulator& _door;

    std::chrono::seconds _entryTimeout;
    Clock _clock;
    std::chrono::steady_clock::time_point _entryDeadline;

    State _state = State::Idle;
    std::optional<std::string> _currentCardId;
};