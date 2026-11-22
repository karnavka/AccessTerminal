//helper class for UI in Terminal Controller

#pragma once

namespace console {

bool isInteractive();

// У справжньому терміналі вимикає буферизацію рядка, ехо та Ctrl+C-сигнал,
// у деструкторі повертає як було. Якщо ввід не термінальний - нічого не робить.
class RawModeGuard {
public:
    RawModeGuard();
    ~RawModeGuard();
    RawModeGuard(const RawModeGuard&) = delete;
    RawModeGuard& operator=(const RawModeGuard&) = delete;
private:
    bool _active = false;
};

enum class KeyKind { None, Char, Enter, Backspace, Tab, Eof };

struct KeyPress {
    KeyKind kind = KeyKind::None;
    char ch = 0;
};

// Чекає до timeoutMs мілісекунд. None - нічого не натиснули.
// Символи: лише друковані ASCII (ID картки та PIN).
KeyPress pollKey(int timeoutMs);

} 