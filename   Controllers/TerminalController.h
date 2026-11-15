#pragma once

#ifndef ACCESSTERMINAL_TERMINALCONTROLLER_H
#define ACCESSTERMINAL_TERMINALCONTROLLER_H

#include <optional>

#include "../Services/UserService.h"
#include "../Services/CardService.h"
#include "../Services/LogService.h"
#include "../Services/SecurityService.h"
#include "../Services/AccessPolicyService.h"

#include "../Entity/DoorSimulator.h"

class TerminalController{
public:
    TerminalController(CardService&, SecurityService&, AccessPolicyService&, LogService&, DoorSimulator&);

    void run();

private:
    enum class TerminalState {
        Idle,
        CheckingCard,   // S1
        WaitingForPin,  // S2
        CheckingPin,    // S3
        InvalidPin,     // S4
        WaitingForEntry,// S5
        Finished
    };
    TerminalState _state = TerminalState::Idle;
    void setState(TerminalState state);

    // card / pin flow
    void onCardTapped(const std::string&);
    void onPinEntered(const std::string&);
    void handleFailedAttempt(const std::string& cardId);
    void grantAccess();

    // door events
    bool handleDoorCommand(const std::string& input); // true if input was a door command
    void onDoorOpened();
    void onDoorClosed();
    void onForcedOpened();

    void reset();
    void denyAccess(LogReason, std::optional<std::string> = std::nullopt);

    CardService& _cards;
    SecurityService& _security;
    AccessPolicyService& _access;
    LogService& _logs;
    DoorSimulator& _door;

    // картка яка обробляється
    std::optional<std::string> _currentCardId;
};

#endif //ACCESSTERMINAL_TERMINALCONTROLLER_H