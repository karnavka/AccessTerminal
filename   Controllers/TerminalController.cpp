#include "TerminalController.h"
#include "ConsoleInput.h"

#include <iostream>
#include <utility>

namespace {

    constexpr std::size_t kMaxInputLength = 32;

    std::string trim(const std::string& text) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return "";
        }
        const auto last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

}

TerminalController::TerminalController(CardService& cards,
                                       SecurityService& security,
                                       AccessPolicyService& access,
                                       LogService& logs,
                                       DoorSimulator& door,
                                       std::chrono::seconds entryTimeout,
                                       Clock clock)
    : _cards(cards), _security(security), _access(access), _logs(logs), _door(door),
      _entryTimeout(entryTimeout), _clock(std::move(clock)) {}

TerminalController::~TerminalController() {
    shutdown();
}

// =====================================================================
// Цикл і ввід
// =====================================================================

void TerminalController::run() {
    console::RawModeGuard rawMode;

    std::cout << "\n===== ТЕРМІНАЛ КОНТРОЛЮ ДОСТУПУ =====\n";

    while (true) {
        const Input in = readInput();
        if (in.kind == InputKind::Eof) {
            break;
        }
        if (in.kind == InputKind::Interrupted) {
            continue;
        }
        if (!handleLine(in.text)) {
            break;
        }
    }

    const bool wasOpenOrUnlocked = _door.isOpen() || !_door.isLocked();
    shutdown();
    if (wasOpenOrUnlocked) {
        std::cout << "Вихід: двері зачинено й заблоковано.\n";
    }
}

bool TerminalController::handleLine(const std::string& text) {
    // У режимі без терміналу таймаут можна перевірити лише тут, після кожного вводу.
    // Це ж правильний порядок: спізнілий "!open" після таймауту - це вже злам.
    checkEntryTimeout();

    if (text.empty()) {
        return true;
    }
    if (handleDoorCommand(text)) {
        return true;
    }

    switch (_state) {
        case State::Idle:
            if (text == "0") {
                return false;
            }
            onCardTapped(text);
            break;

        case State::WaitingForPin:
            if (text == "0") {
                onCancel();
            } else {
                onPinEntered(text);
            }
            break;

        case State::WaitingForEntry:
            std::cout << "Невідома команда.\n";
            break;
    }
    return true;
}

bool TerminalController::handleDoorCommand(const std::string& text) {
    if (text == "!open") {
        onDoorOpened();
        return true;
    }
    if (text == "!close") {
        onDoorClosed();
        return true;
    }
    return false;
}

std::string TerminalController::promptFor(bool tty) const {
    const std::string doors = tty ? "Tab - двері" : "!open / !close - двері";

    switch (_state) {
        case State::Idle:
            return "Прикладіть картку (0 - вихід, " + doors + "): ";
        case State::WaitingForPin:
            return "Введіть PIN (0 - скасувати, " + doors + "): ";
        case State::WaitingForEntry:
            return "Двері розблоковано (" + doors + "): ";
    }
    return "";
}

TerminalController::InputMode TerminalController::inputMode() const {
    switch (_state) {
        case State::Idle:            return InputMode::Plain;
        case State::WaitingForPin:   return InputMode::Masked;
        case State::WaitingForEntry: return InputMode::KeysOnly;
    }
    return InputMode::Plain;
}

TerminalController::Input TerminalController::readInput() {
    const bool tty = console::isInteractive();
    const std::string prompt = promptFor(tty);

    std::cout << prompt << std::flush;

    return tty ? readKeys(prompt, inputMode()) : readLineBlocking();
}

TerminalController::Input TerminalController::readLineBlocking() {
    std::string line;
    if (!std::getline(std::cin, line)) {
        return {InputKind::Eof, ""};
    }
    return {InputKind::Line, trim(line)};
}

TerminalController::Input TerminalController::readKeys(const std::string& prompt, InputMode mode) {
    const State startState = _state;
    std::string buffer;

    while (true) {
        const console::KeyPress key = console::pollKey(100);

        switch (key.kind) {
            case console::KeyKind::None:
                if (entryExpired()) {
                    std::cout << '\n';
                    checkEntryTimeout();
                    return {InputKind::Interrupted, ""};
                }
                break;

            case console::KeyKind::Char:
            // std::cerr << "[key=" << static_cast<int>(key.ch) << "]"; 
                if (mode != InputMode::KeysOnly && buffer.size() < kMaxInputLength) {
                    buffer.push_back(key.ch);
                    std::cout << (mode == InputMode::Masked ? '*' : key.ch) << std::flush;
                }
                break;

            case console::KeyKind::Backspace:
                if (!buffer.empty()) {
                    buffer.pop_back();
                    std::cout << "\b \b" << std::flush;
                }
                break;

            case console::KeyKind::Enter:
                if (mode == InputMode::KeysOnly) {
                    break;
                }
                std::cout << '\n';
                return {InputKind::Line, buffer};

            case console::KeyKind::Tab:
                std::cout << '\n';
                toggleDoor();
                if (_state != startState) {
                    return {InputKind::Interrupted, ""};   // набраний текст відкидаємо
                }
                // стан той самий - перемальовуємо рядок разом із тим, що вже набрано
                std::cout << prompt
                          << (mode == InputMode::Masked ? std::string(buffer.size(), '*') : buffer)
                          << std::flush;
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

// =====================================================================
// Картка і PIN
// =====================================================================

void TerminalController::onCardTapped(const std::string& cardId) {
    if (_state != State::Idle || cardId.empty()) {
        return;
    }

    try {
        const AccessDecision decision = _access.check(cardId, std::chrono::system_clock::now());

        if (!decision.allowed) {
            denyAccess(decision.reason.value_or(LogReason::UnknownCard), cardId);
            return;
        }

        _currentCardId = cardId;
        _state = State::WaitingForPin;
        std::cout << "Картку прийнято. Введіть PIN.\n";

    } catch (const std::exception& e) {
        abortOnError(cardId, e);
    }
}

void TerminalController::onPinEntered(const std::string& pin) {
    if (_state != State::WaitingForPin || !_currentCardId || pin.empty()) {
        return;
    }

    const std::string cardId = *_currentCardId;

    try {
        const AccessDecision decision = _access.check(cardId, std::chrono::system_clock::now());
        if (!decision.allowed) {
            denyAccess(decision.reason.value_or(LogReason::UnknownCard), cardId);
            return;
        }

        if (_cards.verifyPin(cardId, pin)) {
            _security.onSuccessfulPin(cardId);
            grantAccess();
        } else {
            handleFailedAttempt(cardId);
        }

    } catch (const std::exception& e) {
        abortOnError(cardId, e);
    }
}

void TerminalController::onCancel() {
    if (_state != State::WaitingForPin) {
        return;
    }
    std::cout << "Скасовано.\n";
    endSession();
}

void TerminalController::handleFailedAttempt(const std::string& cardId) {
    const WrongPinOut out = _security.wrongPinOut(cardId);

    if (out.cardBlok) {
        std::cout << "Забагато спроб. Картку заблоковано.\n";
        endSession();
    } else if (out.attempts <= 0) {
        std::cout << "Картка більше недоступна.\n";
        endSession();
    } else {
        std::cout << "Невірний PIN. Залишилось спроб: " << out.attempts << '\n';
    }
}

void TerminalController::grantAccess() {
    _door.unlock();
    _entryDeadline = _clock() + _entryTimeout;
    _state = State::WaitingForEntry;
    std::cout << "Доступ дозволено. Двері розблоковано ("
              << _entryTimeout.count() << " с на вхід).\n";
}

void TerminalController::denyAccess(LogReason reason, const std::string& cardId) {
    _logs.log(cardId, LogResult::Denied, reason);
    // Справжню причину бачить лише адміністратор у журналі. Людині біля дверей
    // не розкриваємо, чи існує така картка, чи вона заблокована / відкликана.
    std::cout << "У доступі відмовлено.\n";
    endSession();
}

// Помилка БД: відмовляємо (двері лишаються замкненими), пробуємо записати в журнал.
// Якщо журнал теж недоступний - не падаємо.
void TerminalController::abortOnError(const std::string& cardId, const std::exception& e) {
    std::cerr << "Помилка бази даних: " << e.what() << '\n';
    try {
        _logs.log(cardId, LogResult::Denied, LogReason::DbUnavailable);
    } catch (const std::exception&) {
        // журнал недоступний - більше нічого зробити не можемо
    }
    std::cout << "Сервіс тимчасово недоступний. У доступі відмовлено.\n";
    endSession();
}

// =====================================================================
// Двері
// =====================================================================

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

    if (_state == State::WaitingForEntry) {
        endSession();   // людина зайшла: замикаємо
    }
}

void TerminalController::onForcedOpened() {
    std::cout << "!!! ВЗЛОМ: двері відчинено без автентифікації !!!\n";
    try {
        _security.triggerAlarm(AlarmReason::ForcedOpen);
    } catch (const std::exception& e) {
        std::cerr << "Не вдалося записати тривогу в журнал: " << e.what() << '\n';
    }
    endSession();
}

bool TerminalController::entryExpired() const {
    return _state == State::WaitingForEntry && !_door.isOpen() && _clock() >= _entryDeadline;
}

bool TerminalController::checkEntryTimeout() {
    if (!entryExpired()) {
        return false;
    }
    std::cout << "Час на вхід вичерпано. Двері зачинено.\n";
    endSession();
    return true;
}

void TerminalController::endSession() {
    _currentCardId.reset();
    _door.lock();
    _state = State::Idle;
}

// не залишає двері відчиненими
void TerminalController::shutdown() {
    endSession();
    _door.simulateDoorClosed();
    _door.lock();
}