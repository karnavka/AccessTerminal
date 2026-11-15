#include <iostream>
#include <sstream>

#include "  Controllers/TerminalController.h"
/*
TerminalController::TerminalController(
    CardService& cards,
    HashService& hash,
    AccessPolicyService& access,
    LogService& logs,
    DoorSimulator& door
): _cards(cards), _hash(hash), _logs(logs), _access(access) , _door(door) {}

void TerminalController::run() {
    std::string input;

    while (true) {
        switch (_state) {

            case TerminalState::Idle: {
                std::cout << "\n===== ACCESS TERMINAL =====\n";
                std::cout << "Tap your card (0 - exit): ";

                if (!std::getline(std::cin, input)) {
                    return;
                }
                if (input == "0") {
                    return;
                }
                //TODO к саме обробити 
                if (input == "forced") {
                    _door.simulateDoorOpened();
                    // onDoorOpened();
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

                onPinEntered(input);
                break;
            }

            case TerminalState::WaitingForEntry: {
                // std::cout << "\nAccess granted. Door unlocked.\n";
                // std::cout << "Simulate door event:\n";
                // std::cout << "o - open door\n";
                // std::cout << "c - close door\n";
                // std::cout << "f - forced open\n";
                // std::cout << "Choice: ";

                // if (!std::getline(std::cin, input)) {
                //     return;
                // }

                // if (input == "o") {
                //     _door.simulateDoorOpened();
                //     onDoorOpened();
                // }
                // else if (input == "c") {
                //     _door.simulateDoorClosed();
                //     onDoorClosed();
                // }
                // else if (input == "f") {
                //     _door.simulateDoorOpened();
                //     onForcedOpened();
                // }
                // else {
                //     std::cout << "Unknown command.\n";
                // }
                break;
            }

            case TerminalState::Finished: {
                std::cout << "\nSession finished.\n";
                reset();
                break;
            }

            // These states should normally be transient:
            // their processing happens inside event handlers.
            case TerminalState::CheckingCard:{
                std::cout << "Опрацювання запиту...\n";
                break;
            }
            case TerminalState::CheckingPin:{
                break;
            }
            case TerminalState::InvalidPin: {
                std::cout << "Processing...\n";
                break;
            }
            default: {
                std::cout << "невалідний стан" << std::endl;
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
    _cards.releaseIfBlockExpired(cardId);
    auto card = _cards.getCard(cardId);

    try {
        AccessDecision decision = _access.check( cardId, std::chrono::system_clock::now());

        if (!decision.allowed) {
            if (decision.reason){
                denyAccess(*decision.reason, cardId);
            }else{
                _logs.log(cardId, LogResult::Denied, std::nullopt);
                std::cout << "У доступі відмовлено: щось не так.\n";
                setState(TerminalState::Finished);
                std::cout << "щось не так.\n";
            }
            return;
        }

        _currentCardId = cardId;
        std::cout << "Картку прийнято. Введіть PIN.\n";
        setState(TerminalState::WaitingForPin);

    } catch (const std::exception& e) {
        std::cerr << "Card validation error: " << e.what() << '\n';
        _logs.log(cardId,LogResult::Denied,LogReason::DbUnavailable);
        setState(TerminalState::Finished);
    }
}


void TerminalController::onPinEntered(const std::string& pin) {
    if (_state != TerminalState::WaitingForPin ||
        !_currentCardId) {
        return;
    }

    std::cout << "inside pin entered: " << pin << std::endl;

    setState(TerminalState::CheckingPin);

    // if (checkPin(pin)) {
    //     _cards.resetFailedAttempt(*_currentCardId);

    //     // TODO: записати LogResult::Success

    //     // grantAccess();
    //     return;
    // }

    // handleFailedAttempt(*_currentCardId);
}


// bool TerminalController::checkPin(const std::string& pin) {
//     auto card = _cards.getCard(*_currentCardId);

//     if (!card) {
//         return false;
//     }

//     return _hash.verify(pin, card->pinHash);
// }


// void TerminalController::handleFailedAttempt(
//     const std::string& cardId
// ) {
//     setState(TerminalState::InvalidPin);

//     // TODO: записати LogResult::Denied

//     FailedAttemptResult result =
//         _cards.registerFailedAttempt(cardId);

//     if (result.blocked) {
//         triggerAlarm("Maximum PIN attempts reached");
//         return;
//     }

//     // Можна повторити введення PIN
//     setState(TerminalState::WaitingForPin);
// }


// void TerminalController::grantAccess() {
//     _door.unlock();

//     setState(TerminalState::WaitingForEntry);

//     // TODO: показати "Access granted"
// }


// void TerminalController::onDoorOpened() {
//     if (_state == TerminalState::WaitingForEntry) {
//         // Двері відчинилися після успішної автентифікації.
//         return;
//     }

//     if (_door.isLocked()) {
//         onForcedOpened();
//     }
// }


// void TerminalController::onDoorClosed() {
//     _door.lock();

//     if (_state == TerminalState::WaitingForEntry) {
//         setState(TerminalState::Finished);
//         reset();
//     }
// }


// void TerminalController::onForcedOpened() {
//     if (!_door.isLocked()) {
//         return;
//     }

//     triggerAlarm("ForcedOpen");
// }


// void TerminalController::triggerAlarm(const std::string& reason) {
//     // TODO: записати LogResult::Alarm та причину ForcedOpen
//     // або іншу відповідну причину.

//     setState(TerminalState::Finished);

//     // TODO: увімкнути сигналізацію / показати повідомлення.
// }

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

// TerminalState TerminalController::getState() const {
//     return _state;
// }



*/