//helper class for UI in Terminal Controller

#include "ConsoleInput.h"

#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <conio.h>
#include <io.h>
#else
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace console {

#ifndef _WIN32
namespace {
termios g_saved{};

bool waitReadable(int timeoutMs) {
    pollfd fd{STDIN_FILENO, POLLIN, 0};
    return ::poll(&fd, 1, timeoutMs) > 0;
}
} // namespace
#endif

bool isInteractive() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#else
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
#endif
}

RawModeGuard::RawModeGuard() {
#ifndef _WIN32
    if (!isInteractive() || tcgetattr(STDIN_FILENO, &g_saved) != 0) {
        return;
    }
    termios raw = g_saved;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    _active = true;
#endif
}

RawModeGuard::~RawModeGuard() {
#ifndef _WIN32
    if (_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
    }
#endif
}

#ifdef _WIN32

KeyPress pollKey(int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!_kbhit()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return {};
        }
        Sleep(10);
    }

    const int ch = _getch();
    if (ch == 0 || ch == 224) {              // стрілки / F-клавіші приходять двома кодами
        _getch();
        return {};
    }
    if (ch == '\r' || ch == '\n') return {KeyKind::Enter, 0};
    if (ch == '\b')               return {KeyKind::Backspace, 0};
    if (ch == '\t')               return {KeyKind::Tab, 0};
    if (ch == 3 || ch == 26)      return {KeyKind::Eof, 0};   // Ctrl+C / Ctrl+Z
    if (ch >= 32 && ch < 127)     return {KeyKind::Char, static_cast<char>(ch)};
    return {};
}

#else

KeyPress pollKey(int timeoutMs) {
    if (!waitReadable(timeoutMs)) {
        return {};
    }

    unsigned char ch = 0;
    if (read(STDIN_FILENO, &ch, 1) != 1 || ch == 3 || ch == 4) {
        return {KeyKind::Eof, 0};
    }
    if (ch == 27) {
        unsigned char skip = 0;
        while (waitReadable(0) && read(STDIN_FILENO, &skip, 1) == 1) {}
        return {};
    }
    if (ch == '\n' || ch == '\r') return {KeyKind::Enter, 0};
    if (ch == 127 || ch == 8)     return {KeyKind::Backspace, 0};
    if (ch == '\t')               return {KeyKind::Tab, 0};
    if (ch >= 32 && ch < 127)     return {KeyKind::Char, static_cast<char>(ch)};
    return {};
}

#endif

}