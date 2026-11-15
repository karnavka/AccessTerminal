#include <iostream>
#include <sstream>

#include "  Controllers/TerminalController.h"

TerminalController::TerminalController(
    CardService& cards,
    SecurityService& security,
    AccessPolicyService& access,
    LogService& logs,
    DoorSimulator& door
): _cards(cards), _security(security), _access(access), _logs(logs), _door(door) {}

void TerminalController::run() {
    std::string input;

    while (true) {
        switch (_state) {

            case TerminalState::Idle: {
                std::cout << "\n===== ACCESS TERMINAL =====\n";
                std::cout << "Tap your card (0 - exit, !open / !close - door sensor): ";

                if (!std::getline(std::cin, input)) {
                    return;
                }
                if (input == "0") {
                    return;
                }
                if (handleDoorCommand(input)) {
                    break;
                }

                onCardTapped(input);
                break;
            }

            case TerminalState::WaitingForPin: {
                std::cout << "Enter PIN (0 - cancel): ";

                if (!std::getline(std::cin, input)) {
                    return;
                }
                if (input == "0") {
                    reset();
                    break;
                }
                if (handleDoorCommand(input)) {
                    break;
                }

                onPinEntered(input);
                break;
            }

            case TerminalState::WaitingForEntry: {
                std::cout << "Двері розблоковано (!open / !close): ";

                if (!std::getline(std::cin, input)) {
                    return;
                }
                if (!handleDoorCommand(input)) {
                    std::cout << "Невідома команда.\n";
                }
                break;
            }

            case TerminalState::Finished: {
                std::cout << "\nSession finished.\n";
                reset();
                break;
            }
            // під час цих станів термінал не має бути доступним
            case TerminalState::CheckingCard:
            case TerminalState::CheckingPin:
            case TerminalState::InvalidPin:
            default: {
                std::cout << "невалідний стан\n";
                reset();
                break;
            }
        }
    }
}

void TerminalController::onCardTapped(const std::string& cardId) {
    if (_state != TerminalState::Idle) {
        return;
    }
    setState(TerminalState::CheckingCard);

    if (cardId.empty()) {
        denyAccess(LogReason::UnknownCard);
        return;
    }

    try {
        AccessDecision decision = _access.check(cardId, std::chrono::system_clock::now());

        if (!decision.allowed) {
            // log CHECK requires a reason for Denied, so always provide one
            denyAccess(decision.reason.value_or(LogReason::UnknownCard), cardId);
            return;
        }

        _currentCardId = cardId;
        std::cout << "Картку прийнято. Введіть PIN.\n";
        setState(TerminalState::WaitingForPin);

    } catch (const std::exception& e) {
        std::cerr << "Card validation error: " << e.what() << '\n';
        _logs.log(cardId, LogResult::Denied, LogReason::DbUnavailable);
        setState(TerminalState::Finished);
    }
}

void TerminalController::onPinEntered(const std::string& pin) {
    if (_state != TerminalState::WaitingForPin || !_currentCardId) {
        return;
    }
    setState(TerminalState::CheckingPin);

    const std::string cardId = *_currentCardId;

    try {
        if (_cards.verifyPin(cardId, pin)) {
            _security.onSuccessfulPin(cardId); // reset attempts + log Success
            grantAccess();
        } else {
            handleFailedAttempt(cardId);
        }
    } catch (const std::exception& e) {
        std::cerr << "PIN validation error: " << e.what() << '\n';
        _logs.log(cardId, LogResult::Denied, LogReason::DbUnavailable);
        setState(TerminalState::Finished);
    }
}

void TerminalController::handleFailedAttempt(const std::string& cardId) {
    setState(TerminalState::InvalidPin);

    // registers the attempt, logs WrongPin, blocks the card + logs/raises Alarm if needed
    const WrongPinOut out = _security.wrongPinOut(cardId);

    if (out.cardBlok) {
        std::cout << "Забагато спроб. Картку заблоковано.\n";
        setState(TerminalState::Finished);
        return;
    }

    // card disappeared / got blocked or revoked mid-session: nothing left to retry
    if (out.attempts <= 0) {
        std::cout << "Картка більше недоступна.\n";
        setState(TerminalState::Finished);
        return;
    }

    std::cout << "Невірний PIN. Залишилось спроб: " << out.attempts << '\n';
    setState(TerminalState::WaitingForPin);
}

void TerminalController::grantAccess() {
    _door.unlock();
    std::cout << "Доступ дозволено. Двері розблоковано.\n";
    setState(TerminalState::WaitingForEntry);
}

bool TerminalController::handleDoorCommand(const std::string& input) {
    if (input == "!open") {
        onDoorOpened();
        return true;
    }
    if (input == "!close") {
        onDoorClosed();
        return true;
    }
    return false;
}

void TerminalController::onDoorOpened() {
    if (_door.isOpen()) {
        std::cout << "Двері вже відчинені.\n";
        return;
    }

    _door.simulateDoorOpened();

    // forced open = door opened while it is locked (independent of terminal state)
    if (_door.isLocked()) {
        onForcedOpened();
        return;
    }
    std::cout << "Двері відчинено.\n";
}

void TerminalController::onDoorClosed() {
    if (!_door.isOpen()) {
        std::cout << "Двері вже зачинені.\n";
        return;
    }

    _door.simulateDoorClosed();
    std::cout << "Двері зачинено.\n";

    if (_state == TerminalState::WaitingForEntry) {
        _door.lock();
        setState(TerminalState::Finished);
    }
}

void TerminalController::onForcedOpened() {
    std::cout << "!!! ВЗЛОМ: двері відчинено без автентифікації !!!\n";
    _security.triggerAlarm(AlarmReason::ForcedOpen); // logs Alarm/ForcedOpen + siren sink
    setState(TerminalState::Finished);               // aborts any in-progress session
}

void TerminalController::denyAccess(LogReason reason, std::optional<std::string> cardId) {
    _logs.log(cardId, LogResult::Denied, reason);
    std::cout << "У доступі відмовлено: " << toString(reason) << '\n';
    setState(TerminalState::Finished);
}

void TerminalController::reset() {
    _currentCardId.reset();
    _door.lock();
    setState(TerminalState::Idle);
}

void TerminalController::setState(TerminalState state) {
    _state = state;
}