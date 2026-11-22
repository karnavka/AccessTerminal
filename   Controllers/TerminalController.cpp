#include <iostream>

#include "  Controllers/TerminalController.h"
#include "  Controllers/ConsoleInput.h"

TerminalController::TerminalController(
    CardService& cards,
    SecurityService& security,
    AccessPolicyService& access,
    LogService& logs,
    DoorSimulator& door,
    std::chrono::seconds entryTimeout
): _cards(cards), _security(security), _access(access), _logs(logs), _door(door),
   _entryTimeout(entryTimeout) {}

void TerminalController::run() {
    console::RawModeGuard rawMode;

    std::cout << "\n===== ACCESS TERMINAL =====\n";

    while (true) {
        switch (_state) {

            case TerminalState::Idle: {
                const Input in = readInput("Tap your card (0 - exit, Tab - door sensor): ", false, true);
                if (in.kind == InputKind::Eof) {
                    return;
                }
                if (in.kind == InputKind::Interrupted) {
                    break;
                }
                if (in.text == "0") {
                    return;
                }
                if (handleDoorCommand(in.text)) {
                    break;
                }

                onCardTapped(in.text);
                break;
            }

            case TerminalState::WaitingForPin: {
                const Input in = readInput("Enter PIN (0 - cancel, Tab - door sensor): ", true, true);
                if (in.kind == InputKind::Eof) {
                    return;
                }
                if (in.kind == InputKind::Interrupted) {
                    break;
                }
                if (in.text == "0") {
                    reset();
                    break;
                }
                if (handleDoorCommand(in.text)) {
                    break;
                }

                onPinEntered(in.text);
                break;
            }

            case TerminalState::WaitingForEntry: {
                const Input in = readInput("Двері розблоковано (Tab - відчинити/зачинити): ", false, true);
                if (in.kind == InputKind::Eof) {
                    return;
                }
                if (in.kind == InputKind::Interrupted) {
                    break;
                }

                // сюди текст приходить лише в режимі без терміналу
                checkEntryTimeout();
                if (!handleDoorCommand(in.text)) {
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

TerminalController::Input TerminalController::readInput(const std::string& prompt, bool mask, bool acceptText) {
    std::cout << prompt << std::flush;

    // не справжній термінал (Run у CLion, перенаправлений ввід): блокуючий getline,
    // двері - командами !open / !close, таймаут перевіряється лише після вводу
    if (!console::isInteractive()) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            return {InputKind::Eof, ""};
        }
        return {InputKind::Line, line};
    }

    const TerminalState startState = _state;
    std::string buffer;

    while (true) {
        const console::KeyPress key = console::pollKey(100);

        switch (key.kind) {
            case console::KeyKind::None:
                checkEntryTimeout();
                if (_state != startState) {
                    return {InputKind::Interrupted, ""};
                }
                break;

            case console::KeyKind::Char:
                if (acceptText && buffer.size() < 64) {
                    buffer.push_back(key.ch);
                    std::cout << (mask ? '*' : key.ch) << std::flush;
                }
                break;

            case console::KeyKind::Backspace:
                if (!buffer.empty()) {
                    buffer.pop_back();
                    std::cout << "\b \b" << std::flush;
                }
                break;

            case console::KeyKind::Enter:
                if (!acceptText) {
                    break;
                }
                std::cout << '\n';
                return {InputKind::Line, buffer};

            case console::KeyKind::Tab:
                std::cout << '\n';
                toggleDoor();
                if (_state != startState) {
                    return {InputKind::Interrupted, ""};
                }
                // перемальовуємо рядок вводу разом з тим, що вже набрано
                std::cout << prompt << (mask ? std::string(buffer.size(), '*') : buffer) << std::flush;
                break;

            case console::KeyKind::Eof:
                std::cout << '\n';
                return {InputKind::Eof, ""};
        }
    }
}

void TerminalController::toggleDoor() {
    if (_door.isOpen()) {
        onDoorClosed();
    } else {
        onDoorOpened();
    }
}

void TerminalController::onCardTapped(const std::string& cardId) {
    if (_state != TerminalState::Idle) {
        return;
    }
    setState(TerminalState::CheckingCard);

    try {
        if (cardId.empty()) {
            denyAccess(LogReason::UnknownCard);
            return;
        }

        AccessDecision decision = _access.check(cardId, std::chrono::system_clock::now());

        if (!decision.allowed) {
            // CHECK в access_log вимагає причину для Denied
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
            _security.onSuccessfulPin(cardId); // скидання спроб + лог Success
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

    const WrongPinOut out = _security.wrongPinOut(cardId);

    if (out.cardBlok) {
        std::cout << "Забагато спроб. Картку заблоковано.\n";
        setState(TerminalState::Finished);
        return;
    }
    
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
    _entryDeadline = std::chrono::steady_clock::now() + _entryTimeout;
    std::cout << "Доступ дозволено. Двері розблоковано ("
              << _entryTimeout.count() << " с на вхід).\n";
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

    // злам = двері відчинено при замкненому замку (незалежно від стану терміналу)
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
    _security.triggerAlarm(AlarmReason::ForcedOpen);
    setState(TerminalState::Finished);
}

void TerminalController::checkEntryTimeout() {
    if (_state == TerminalState::WaitingForEntry && !_door.isOpen() &&
        std::chrono::steady_clock::now() >= _entryDeadline) {
        std::cout << "Час на вхід вичерпано. Двері зачинено.\n";
        _door.lock();
        setState(TerminalState::Finished);
    }
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