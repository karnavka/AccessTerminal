//
// Created by Христина Кузь on 08.11.2026.
//

#include "AdminController.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iostream>
#include <sstream>

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
#include <cstdio>
// у старих версіях MinGW цієї константи може не бути
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace {

bool readLine(std::string& line) {
    if (!std::getline(std::cin, line)) {
        std::cout << '\n';
        return false;
    }
    line = CardService::trimInput(line);
    return true;
}

bool parseInt(const std::string& input, int& value) {
    std::istringstream stream(input);
    if (!(stream >> value)) {
        return false;
    }
    stream >> std::ws;
    return stream.eof();
}

bool cardIdOk(const std::string& id) {
    return !id.empty() && id.size() <= 32;
}

bool pinOk(const std::string& pin) {
    try {
        CardService::validatePin(pin);
        return true;
    } catch (const std::invalid_argument& e) {
        std::cout << e.what() << ". Спробуйте ще раз або введіть 0 для скасування.\n";
        return false;
    }
}

std::tm toLocalTm(TimePoint tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    return local;
}

std::string toLocalString(TimePoint tp) {
    const std::tm local = toLocalTm(tp);
    char buf[20];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &local);
    return buf;
}

bool parseLocalDate(const std::string& date, TimePoint& dayStart, TimePoint& nextDayStart) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }
    for (std::size_t i = 0; i < date.size(); i++) {
        if (i != 4 && i != 7 && !std::isdigit(static_cast<unsigned char>(date[i]))) {
            return false;
        }
    }

    const int year = std::stoi(date.substr(0, 4));
    const int month = std::stoi(date.substr(5, 2));
    const int day = std::stoi(date.substr(8, 2));

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_isdst = -1;
    const std::time_t start = std::mktime(&tm);

    if (start == -1 || tm.tm_year != year - 1900 || tm.tm_mon != month - 1 || tm.tm_mday != day) {
        return false;
    }

    std::tm next{};
    next.tm_year = year - 1900;
    next.tm_mon = month - 1;
    next.tm_mday = day + 1;
    next.tm_isdst = -1;

    dayStart = std::chrono::system_clock::from_time_t(start);
    nextDayStart = std::chrono::system_clock::from_time_t(std::mktime(&next));
    return true;
}

void printLogEntry(const LogEntity& entry) {
    std::cout << "ID запису: " << entry.id << '\n';
    std::cout << "Час: " << toLocalString(entry.timestamp) << '\n';
    std::cout << "ID картки: ";
    if (entry.cardId) {
        std::cout << *entry.cardId << '\n';
    } else {
        std::cout << " - \n";
    }
    std::cout << "Результат: " << toString(entry.result) << '\n';
    std::cout << "Причина: ";
    if (entry.reason) {
        std::cout << toString(*entry.reason) << '\n';
    } else {
        std::cout << " - \n";
    }
    std::cout << "-----------------------------\n";
}

std::string formatMinutes(int totalMinutes) {
    const int hours = totalMinutes / 60;
    const int minutes = totalMinutes % 60;
    std::string result;
    if (hours < 10) result += '0';
    result += std::to_string(hours) + ':';
    if (minutes < 10) result += '0';
    result += std::to_string(minutes);
    return result;
}

// ---------- меню зі стрілочками ----------

struct MenuItem {
    int number;
    std::string text;
};

// чи ми в справжньому терміналі (тоді працюють стрілочки)
// у звичайному вікні Run у CLion і в тестах із перенаправленим вводом - ні
bool isInteractive() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#else
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
#endif
}

// на вінді треба окремо увімкнути керуючі послідовності (переміщення курсора)
void enableAnsiOnce() {
#ifdef _WIN32
    static bool enabled = false;
    if (enabled) return;
    enabled = true;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode)) {
        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

enum class KeyKind { Up, Down, Enter, Digit, End, Other };

struct Key {
    KeyKind kind = KeyKind::Other;
    int digit = 0;
};


Key readKey() {
    Key key;
#ifdef _WIN32
    const int ch = _getch();
    if (ch == 0 || ch == 224) {          // стрілки приходять двома кодами
        const int code = _getch();
        if (code == 72) key.kind = KeyKind::Up;
        if (code == 80) key.kind = KeyKind::Down;
    } else if (ch == '\r' || ch == '\n') {
        key.kind = KeyKind::Enter;
    } else if (ch == 26 || ch == 3) {    // Ctrl+Z / Ctrl+C
        key.kind = KeyKind::End;
    } else if (ch >= '0' && ch <= '9') {
        key.kind = KeyKind::Digit;
        key.digit = ch - '0';
    }
#else
    termios oldSettings{};
    tcgetattr(STDIN_FILENO, &oldSettings);
    termios raw = oldSettings;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    unsigned char ch = 0;
    if (read(STDIN_FILENO, &ch, 1) != 1 || ch == 4) {
        key.kind = KeyKind::End;
    } else if (ch == 27) {
        unsigned char seq[2] = {0, 0};
        if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[' &&
            read(STDIN_FILENO, &seq[1], 1) == 1) {
            if (seq[1] == 'A') key.kind = KeyKind::Up;
            if (seq[1] == 'B') key.kind = KeyKind::Down;
        }
    } else if (ch == '\n' || ch == '\r') {
        key.kind = KeyKind::Enter;
    } else if (ch >= '0' && ch <= '9') {
        key.kind = KeyKind::Digit;
        key.digit = ch - '0';
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldSettings);
#endif
    return key;
}

std::size_t displayWidth(const std::string& text) {
    std::size_t width = 0;
    for (unsigned char ch : text) {
        if ((ch & 0xC0) != 0x80) width++;
    }
    return width;
}

std::string repeat(const std::string& part, std::size_t count) {
    std::string result;
    for (std::size_t i = 0; i < count; i++) result += part;
    return result;
}

int chooseByNumber(const std::vector<MenuItem>& items) {
    while (true) {
        for (const MenuItem& item : items) {
            std::cout << item.number << ". " << item.text << '\n';
        }
        std::cout << "Виберіть варіант: ";

        std::string input;
        if (!readLine(input)) {
            return -1;
        }
        int choice;
        if (!parseInt(input, choice)) {
            std::cout << "Некоректне введення. Спробуйте ще раз.\n";
            continue;
        }
        for (const MenuItem& item : items) {
            if (item.number == choice) {
                return choice;
            }
        }
        std::cout << "Такого варіанту немає. Спробуйте ще раз.\n";
    }
}

// меню: стрілочками вверх/вниз, вибраний пункт обведений рамкою, Enter - вибрати
// (або одразу цифра пункту). Повертає number вибраного пункту, -1 якщо ввід закінчився
int chooseMenuItem(const std::string& title, const std::vector<MenuItem>& items) {
    std::cout << title << '\n';

    if (!isInteractive()) {
        return chooseByNumber(items);
    }
    enableAnsiOnce();

    std::size_t textWidth = 0;
    for (const MenuItem& item : items) {
        textWidth = std::max(textWidth, displayWidth(std::to_string(item.number) + ". " + item.text));
    }

    std::size_t selected = 0;
    int printedLines = 0;

    while (true) {
        if (printedLines > 0) {
            std::cout << "\033[" << printedLines << "A\r\033[J";
        }
        printedLines = 0;

        for (std::size_t i = 0; i < items.size(); i++) {
            const std::string line = std::to_string(items[i].number) + ". " + items[i].text;
            if (i == selected) {
                const std::string padding(textWidth - displayWidth(line), ' ');
                std::cout << "  ┌" << repeat("─", textWidth + 2) << "┐\n";
                std::cout << "  │ " << line << padding << " │\n";
                std::cout << "  └" << repeat("─", textWidth + 2) << "┘\n";
                printedLines += 3;
            } else {
                std::cout << "    " << line << '\n';
                printedLines += 1;
            }
        }
        std::cout << "  ↑/↓ - вибір, Enter - підтвердити\n";
        printedLines += 1;
        std::cout.flush();

        const Key key = readKey();
        switch (key.kind) {
            case KeyKind::Up:
                selected = (selected == 0) ? items.size() - 1 : selected - 1;
                break;
            case KeyKind::Down:
                selected = (selected + 1) % items.size();
                break;
            case KeyKind::Enter:
                return items[selected].number;
            case KeyKind::Digit:
                for (const MenuItem& item : items) {
                    if (item.number == key.digit) {
                        return item.number;
                    }
                }
                break;
            case KeyKind::End:
                return -1;
            case KeyKind::Other:
                break;
        }
    }
}

} // namespace

AdminController::AdminController(UserService &users, CardService &cards, LogService &logs,
                                 HashService &hash, SecurityService &security)
    : _users(users), _cards(cards), _logs(logs), _hash(hash), _security(security) {
}

void AdminController::run() {
    if (!login()) {
        return;
    }
    showMenu();
}

bool AdminController::login() {
    std::string cardId;
    std::optional<CardEntity> card;

    while (true) {
        std::cout << "Введіть ID карточки (0 - вийти): ";
        if (!readLine(cardId)) {
            return false;
        }
        if (cardId == "0") {
            return false;
        }
        if (!cardIdOk(cardId)) {
            std::cout << "ID картки має містити від 1 до 32 символів. Спробуйте ще раз.\n";
            continue;
        }

        card = _cards.getCard(cardId);
        if (!card) {
            _logs.log(cardId, LogResult::Denied, LogReason::UnknownCard);
            std::cout << "Картку не знайдено. Спробуйте ще раз.\n";
            continue;
        }
        break;
    }

    if (!_users.isAdmin(card->userId)) {
        std::cout << "Ви не адміністратор! Ану пішли геть!\n";
        return false;
    }

    try {

        if (_cards.releaseIfBlockExpired(cardId)){
            card = _cards.getCard(cardId);
            if (!card) {
                std::cout << "Картку не знайдено.\n";
                return false;
            }
        }

        if (card->status == CardStatus::Revoked) {
            _logs.log(cardId, LogResult::Denied, LogReason::Revoked);
            std::cout << "Картку відкликано.\n";
            return false;
        }

        if (card->status == CardStatus::Blocked) {
            _logs.log(cardId, LogResult::Denied, LogReason::Blocked);
            std::cout << "Картку заблоковано.\n";
            return false;
        }

        std::string pin;
        while (true) {
            std::cout << "Введіть PIN (0 - вийти): ";
            if (!readLine(pin)) return false;
            if (pin == "0") return false;

            if (_hash.verify(pin, card->pinHash)) {
                _security.onSuccessfulPin(cardId);
                break;
            }

            _security.wrongPinOut(cardId);
            card = _cards.getCard(cardId);
            if (!card || card->status == CardStatus::Blocked) {
                std::cout << "Забагато невдалих спроб. Картку заблоковано.\n";
                return false;
            }
            _logs.log(cardId, LogResult::Denied, LogReason::WrongPin);
            std::cout << "Неправильний PIN. Спробуйте ще раз.\n";
        }
        _currentCardId = cardId;
        _currentUserId = card->userId;
        std::cout << "Вхід успішний.\n";
        return true;

    } catch (const std::exception& e) {

        std::cout << "Помилка під час входу: " << e.what() << '\n';
        return false;
    }
}

void AdminController::showMenu() {
    while (true) {
        const int choice = chooseMenuItem("\n===== МЕНЮ адміністратора =====", {
            {1, "Перегляд інформації"},
            {2, "Додати користувача"},
            {3, "Додати карту для користувача"},
            {4, "Змінити дані картки"},
            {0, "Вийти"}
        });

        switch (choice) {
            case 1:
                showInformationMenu();
                break;
            case 2:
                addUser();
                break;
            case 3:
                issueCard();
                break;
            case 4:
                editCard();
                break;
            case 0:
            case -1:
                std::cout << "Вихід з режиму адміністратора...\n";
                return;
            default:
                std::cout << "Такого варіанту немає. Спробуйте ще раз.\n";
        }
    }
}

void AdminController::showInformationMenu() {
    while (true) {
        const int choice = chooseMenuItem("\n===== Перегляд інформації =====", {
            {1, "Список користувачів"},
            {2, "Список карток"},
            {3, "Журнал спроб доступу"},
            {0, "Назад"}
        });

        switch (choice) {
            case 1:
                showUsers();
                break;
            case 2:
                showCards();
                break;
            case 3:
                showLog();
                break;
            case 0:
            case -1:
                return;
            default:
                std::cout << "Такого варіанту немає. Спробуйте ще раз.\n";
        }
    }
}

void AdminController::showUsers() {
    std::vector<UserEntity> users = _users.getAllUsers();

    std::cout << "\n===== Список користувачів =====\n";

    if (users.empty()) {
        std::cout << "Користувачів поки немає :(\n";
        return;
    }
    for (const UserEntity& user : users) {
        std::cout << "ID: " << user.id << '\n';
        std::cout << "Ім'я: " << user.name << '\n';
        std::cout << "Роль: " << toString(user.role) << '\n';
        std::cout << "--------------------\n";
    }
}

void AdminController::showCards() {
    std::vector<CardEntity> cards = _cards.getAllCards();

    std::cout << "\n===== Список карток =====\n";

    if (cards.empty()) {
        std::cout << "Карток поки немає :(\n";
        return;
    }
    for (const CardEntity& card : cards) {
        std::cout << "ID картки: " << card.cardId << '\n';
        std::cout << "ID користувача: " << card.userId << '\n';
        std::cout << "Статус: " << toString(card.status) << '\n';
        std::cout << "К-ть невдалих спроб: " << card.failedAttempts << '\n';
        std::cout << "--------------------\n";
    }
}

void AdminController::showLog() {
    std::vector<LogEntity> logs = _logs.getAll();

    std::cout << "\n===== Журнал спроб доступу =====\n";

    if (logs.empty()) {
        std::cout << "Журнал порожній :(\n";
        return;
    }

    for (const LogEntity& entry : logs) {
        printLogEntry(entry);
    }
    filterLog();
}

void AdminController::filterLog() {
    LogFilter filter;

    const int choice = chooseMenuItem("\nБажаєте відфільтрувати журнал?", {
        {1, "За результатом"},
        {2, "За датою"},
        {3, "За ID картки"},
        {0, "Ні"}
    });

    switch (choice) {
        case 1: {
            const int resultChoice = chooseMenuItem("\nОберіть результат:", {
                {1, "Успішний доступ"},
                {2, "Відмовлено в доступі"},
                {3, "Тривога"},
                {0, "Назад"}
            });

            if (resultChoice == 1) {
                filter.result = LogResult::Success;
            } else if (resultChoice == 2) {
                filter.result = LogResult::Denied;
            } else if (resultChoice == 3) {
                filter.result = LogResult::Alarm;
            } else {
                return;
            }
            break;
        }

        case 2: {
            std::string date;

            while (true) {
                std::cout << "Введіть дату (YYYY-MM-DD) (0 - скасувати фільтрацію): ";

                if (!readLine(date)) {
                    return;
                }
                if (date == "0") {
                    return;
                }

                TimePoint from;
                TimePoint to;
                if (parseLocalDate(date, from, to)) {
                    filter.from = from;
                    filter.to = to;
                    break;
                }
                std::cout << "Некоректна дата. Спробуйте ще раз або натисніть 0 для скасування.\n";
            }
            break;
        }

        case 3: {
            std::string cardId;

            while (true) {
                std::cout << "Введіть ID картки (0 - скасувати фільтрацію): ";

                if (!readLine(cardId)) {
                    return;
                }
                if (cardId == "0") {
                    return;
                }
                if (cardIdOk(cardId)) {
                    filter.cardId = cardId;
                    break;
                }
                std::cout << "ID картки має містити від 1 до 32 символів. Спробуйте ще раз або натисніть 0 для скасування.\n";
            }
            break;
        }
        case 0:
            return;
    }

    std::vector<LogEntity> filteredLogs = _logs.filter(filter);

    std::cout << "\n===== Результат фільтрації =====\n";

    if (filteredLogs.empty()) {
        std::cout << "Записів за заданим фільтром не знайдено.\n";
        return;
    }

    for (const LogEntity& entry : filteredLogs) {
        printLogEntry(entry);
    }
}

void AdminController::addUser() {
    std::string name;
    UserRole role;

    std::cout << "\n===== Додавання користувача =====\n";

    while (true) {
        std::cout << "Введіть ім'я користувача (0 - скасувати): ";
        if (!readLine(name)) {
            return;
        }

        if (name == "0") {
            std::cout << "Додавання користувача скасовано.\n";
            return;
        }

        try {
            UserService::validateName(name);
            break;
        } catch (const std::invalid_argument &e) {
            std::cout << e.what() << ". Спробуйте ще раз або натисніть 0 для скасування.\n";
        }
    }

    const int roleChoice = chooseMenuItem("\nОберіть роль:", {
        {1, "Звичайний користувач"},
        {2, "Адміністратор"},
        {0, "Скасувати додавання користувача"}
    });

    if (roleChoice == 1) {
        role = UserRole::User;
    } else if (roleChoice == 2) {
        role = UserRole::Admin;
    } else {
        std::cout << "Додавання користувача скасовано.\n";
        return;
    }

    try {
        int userId = _users.addUser(name, role);
        std::cout << "Користувача успішно додано!\n";
        std::cout << "ID користувача: " << userId << '\n';
    } catch (const std::exception& e) {
        std::cout << "Не вдалося додати користувача: " << e.what() << '\n';
    }
}

int AdminController::readMinutes(const std::string& prompt) {
    std::string time;

    while (true) {
        std::cout << prompt << " (0 - скасувати): ";
        if (!readLine(time)) {
            return -1;
        }

        if (time == "0") {
            std::cout << "Введення часу скасовано.\n";
            return -1;
        }

        int hours;
        int minutes;
        char separator;

        std::istringstream input(time);

        if (input >> hours >> separator >> minutes && separator == ':' && hours >= 0 && hours <= 24 &&
            minutes >= 0 && minutes <= 59 && (hours != 24 || minutes == 0)) {
            input >> std::ws;
            if (input.eof()) {
                return hours * 60 + minutes;
            }
    }
        std::cout << "Некоректний формат часу. Введіть час у форматі HH:MM або 0 для скасування.\n";
    }
}

int AdminController::readDayOfWeek(const std::string& prompt) {
    while (true) {
        std::string input;

        std::cout << prompt << " (1 - понеділок, 7 - неділя, 0 - скасувати): ";
        if (!readLine(input)) {
            return -1;
        }
        if (input == "0") {
            return -1;
        }

        int day;
        if (parseInt(input, day) && day >= 1 && day <= 7) {
            return day - 1;
        }
        std::cout << "Некоректний день тижня. Введіть число від 1 до 7 або 0 для скасування.\n";
    }
}

bool AdminController::isWindowOk(const TimeWindow &window, const std::vector<TimeWindow> &others) {
    std::vector<TimeWindow> all = others;
    all.push_back(window);
    try {
        CardService::validateWindows(all);
        return true;
    } catch (const std::invalid_argument &e) {
        std::cout << e.what() << ".\n";
        return false;
    }
}

bool AdminController::askYesNo(const std::string& question) {
    while (true) {
        std::string answer;

        std::cout << question << " (так/ні): ";
        if (!readLine(answer)) {
            return false;
        }
        if (answer == "так" || answer == "y" || answer == "yes") {
            return true;
        }
        if (answer == "ні" || answer == "n" || answer == "no") {
            return false;
        }
        std::cout << "Введіть \"так\" або \"ні\".\n";
    }
}

void AdminController::issueCard() {
    int userId;
    int numberOfWindows;

    std::string cardId;
    std::string pin;
    std::vector<TimeWindow> windows;

    std::cout << "\n===== Додавання картки для користувача =====\n";

    while (true) {
        std::string input;

        std::cout << "Введіть ID користувача (0 - скасувати): ";
        if (!readLine(input)) {
            return;
        }

        if (input == "0") {
            std::cout << "Додавання картки скасовано.\n";
            return;
        }

        if (!parseInt(input, userId) || userId <= 0) {
            std::cout << "Некоректний ID користувача. Введіть додатне ціле число або 0 для скасування.\n";
            continue;
        }

        if (!_users.getUser(userId)) {
            std::cout << "Користувача з таким ID не знайдено. Спробуйте ще раз або введіть 0 для скасування.\n";
            continue;
        }

        if (_users.hasActiveCard(userId)) {
            std::cout << "У цього користувача вже є активна картка. Спочатку відкличте її.\n";
            continue;
        }
        break;
    }

    while (true) {
        std::cout << "Введіть PIN (0 - скасувати): ";
        if (!readLine(pin)) {
            return;
        }

        if (pin == "0") {
            std::cout << "Додавання картки скасовано.\n";
            return;
        }
        if (pinOk(pin)) {
            break;
        }
    }

    while (true) {
        std::string input;

        std::cout << "Введіть кількість часових проміжків (0 - скасувати): ";
        if (!readLine(input)) {
            return;
        }

        if (input == "0") {
            std::cout << "Додавання картки скасовано.\n";
            return;
        }

        if (parseInt(input, numberOfWindows) && numberOfWindows > 0) {
            break;
        }
        std::cout << "Некоректна кількість проміжків. Введіть додатне ціле число або 0 для скасування.\n";
    }

    try {
        for (int i = 0; i < numberOfWindows; i++) {
            TimeWindow window;

            std::cout << "\nЧасовий проміжок " << i + 1 << '\n';

            while (true) {
                window.dayOfWeek = readDayOfWeek("День тижня");
                if (window.dayOfWeek == -1) {
                    std::cout << "Додавання картки скасовано.\n";
                    return;
                }

                window.startMinute = readMinutes("Час початку (HH:MM)");
                if (window.startMinute == -1) {
                    std::cout << "Додавання картки скасовано.\n";
                    return;
                }

                window.endMinute = readMinutes("Час завершення (HH:MM)");
                if (window.endMinute == -1) {
                    std::cout << "Додавання картки скасовано.\n";
                    return;
                }

                if (isWindowOk(window, windows)) {
                    break;
                }
                std::cout << "Введіть цей проміжок ще раз.\n";
            }
            windows.push_back(window);
        }

        cardId = _cards.issueCard(userId, pin, windows);
        std::cout << "Картку успішно додано! ID картки: " << cardId << '\n';
    } catch (const std::exception& e) {
        std::cout << "Не вдалося додати картку: " << e.what() << '\n';
    }
}

void AdminController::editCard() {
    std::string cardId;

    while (true) {
        std::cout << "Введіть ID картки (0 - скасувати): ";
        if (!readLine(cardId)) {
            return;
        }

        if (cardId == "0") {
            std::cout << "Редагування картки скасовано.\n";
            return;
        }
        if (!cardIdOk(cardId)) {
            std::cout << "ID картки має містити від 1 до 32 символів. Спробуйте ще раз або введіть 0 для скасування.\n";
            continue;
        }
        if (!_cards.getCard(cardId)) {
            std::cout << "Картку не знайдено. Перевірте ID або введіть 0 для скасування.\n";
            continue;
        }
        break;
    }

    while (true) {
        auto card = _cards.getCard(cardId);
        if (!card) {
            std::cout << "Картку не знайдено.\n";
            return;
        }

        if (card->status == CardStatus::Revoked) {
            showTimeWindows(cardId);
            std::cout << "Картку " << cardId << " відкликано, змінювати її вже не можна.\n";
            return;
        }

        const bool isBlocked = card->status == CardStatus::Blocked;

        std::vector<MenuItem> items = {{1, "Змінити PIN"}, {2, "Відкликати картку"}};
        if (isBlocked) {
            items.push_back({3, "Розблокувати картку"});
        }
        items.push_back({4, "Редагувати часові вікна"});
        if (!isBlocked) {
            items.push_back({5, "Заблокувати картку"});
        }
        items.push_back({0, "Назад"});

        const int choice = chooseMenuItem(
            "\n===== Редагування картки " + cardId + " (" + toString(card->status) + ") =====", items);
        if (choice == -1) {
            return;
        }

        try {
            switch (choice) {
                case 1:
                    changePin(cardId);
                    break;
                case 2:
                    revokeCard(cardId);
                    break;
                case 3:
                    if (!isBlocked) {
                        std::cout << "Картка не заблокована.\n";
                        break;
                    }
                    unblockCard(cardId);
                    break;
                case 4:
                    editTimeWindows(cardId);
                    break;
                case 5:
                    if (isBlocked) {
                        std::cout << "Картка вже заблокована.\n";
                        break;
                    }
                    blockCard(cardId);
                    break;
                case 0:
                    return;
                default:
                    std::cout << "Невідомий пункт меню.\n";
            }
        } catch (const std::exception& e) {
            std::cout << "Помилка: " << e.what() << '\n';
        }
    }
}

void AdminController::editTimeWindows(const std::string& cardId) {
    while (true) {
        showTimeWindows(cardId);

        const int choice = chooseMenuItem("\n===== Редагування часових вікон =====", {
            {1, "Додати часове вікно"},
            {2, "Видалити часове вікно"},
            {3, "Редагувати часове вікно"},
            {0, "Назад"}
        });
        if (choice == -1) {
            return;
        }

        try {
            switch (choice) {
                case 1:
                    addTimeWindow(cardId);
                    break;
                case 2:
                    removeTimeWindow(cardId);
                    break;
                case 3:
                    editTimeWindow(cardId);
                    break;
                case 0:
                    return;
                default:
                    std::cout << "Невідомий пункт меню. Введіть число від 0 до 3.\n";
            }
        } catch (const std::exception& e) {
            std::cout << "Помилка: " << e.what() << '\n';
        }
    }
}

void AdminController::showTimeWindows(const std::string& cardId) {
    auto card = _cards.getCard(cardId);

    if (!card) {
        std::cout << "Картку не знайдено.\n";
        return;
    }
    if (card->timeWindows.empty()) {
        std::cout << "\nДля цієї картки часових вікон поки немає.\n";
        return;
    }

    const std::vector<std::string> days = {
        "Понеділок", "Вівторок", "Середа", "Четвер", "П'ятниця", "Субота", "Неділя"
    };

    std::cout << "\n===== Часові вікна картки " << cardId << " =====\n";

    for (std::size_t i = 0; i < card->timeWindows.size(); i++) {
        const TimeWindow& window = card->timeWindows[i];

        std::cout << i + 1 << ". ";

        if (window.dayOfWeek >= 0 && window.dayOfWeek <= 6) {
            std::cout << days[window.dayOfWeek];
        } else {
            std::cout << "Невідомий день";
        }

        std::cout << " | " << formatMinutes(window.startMinute)
                  << " - " << formatMinutes(window.endMinute) << '\n';
    }
}

void AdminController::addTimeWindow(const std::string& cardId) {
    auto card = _cards.getCard(cardId);
    if (!card) {
        std::cout << "Картку не знайдено.\n";
        return;
    }

    TimeWindow window;

    while (true) {
        window.dayOfWeek = readDayOfWeek("Введіть день тижня");
        if (window.dayOfWeek == -1) {
            std::cout << "Додавання часового вікна скасовано.\n";
            return;
        }

        window.startMinute = readMinutes("Введіть час початку (HH:MM)");
        if (window.startMinute == -1) {
            std::cout << "Додавання часового вікна скасовано.\n";
            return;
        }
        window.endMinute = readMinutes("Введіть час завершення (HH:MM)");
        if (window.endMinute == -1) {
            std::cout << "Додавання часового вікна скасовано.\n";
            return;
        }

        if (isWindowOk(window, card->timeWindows)) {
            break;
        }
        std::cout << "Введіть вікно ще раз.\n";
    }

    try {
        _cards.addTimeWindow(cardId, window);
        std::cout << "Часове вікно успішно додано.\n";
    } catch (const std::exception& e) {
        std::cout << "Не вдалося додати часове вікно: " << e.what() << '\n';
    }
}

void AdminController::removeTimeWindow(const std::string& cardId) {
    auto card = _cards.getCard(cardId);

    if (!card || card->timeWindows.empty()) {
        std::cout << "Немає часових вікон для видалення.\n";
        return;
    }

    int windowNumber;

    while (true) {
        std::string input;

        std::cout << "Введіть номер часового вікна для видалення (0 - скасувати): ";
        if (!readLine(input)) {
            return;
        }

        if (input == "0") {
            std::cout << "Видалення часового вікна скасовано.\n";
            return;
        }

        if (parseInt(input, windowNumber) && windowNumber >= 1 && windowNumber <= static_cast<int>(card->timeWindows.size())) {
            break;
        }
        std::cout << "Некоректний номер часового вікна. Введіть число від 1 до "
        << card->timeWindows.size() << " або 0 для скасування.\n";
    }

    try {
        _cards.removeTimeWindow(cardId, windowNumber - 1);
        std::cout << "Часове вікно успішно видалено.\n";
    } catch (const std::exception& e) {
        std::cout << "Не вдалося видалити часове вікно: " << e.what() << '\n';
    }
}

void AdminController::editTimeWindow(const std::string& cardId) {
    auto card = _cards.getCard(cardId);

    if (!card || card->timeWindows.empty()) {
        std::cout << "Немає часових вікон для редагування.\n";
        return;
    }

    int windowNumber;

    while (true) {
        std::string input;

        std::cout << "Введіть номер часового вікна для редагування (0 - скасувати): ";
        if (!readLine(input)) {
            std::cout << "Редагування часового вікна скасовано.\n";
            return;
        }
        if (input == "0") {
            std::cout << "Редагування часового вікна скасовано.\n";
            return;
        }

        if (parseInt(input, windowNumber) && windowNumber >= 1 && windowNumber <= static_cast<int>(card->timeWindows.size())) {
            break;
        }
        std::cout << "Некоректний номер часового вікна. Введіть число від 1 до "
        << card->timeWindows.size() << " або 0 для скасування.\n";
    }

    std::vector<TimeWindow> others = card->timeWindows;
    others.erase(others.begin() + (windowNumber - 1));

    TimeWindow newWindow;

    while (true) {
        newWindow.dayOfWeek = readDayOfWeek("Введіть новий день тижня");
        if (newWindow.dayOfWeek == -1) {
            std::cout << "Редагування часового вікна скасовано.\n";
            return;
        }

        newWindow.startMinute = readMinutes("Введіть новий час початку (HH:MM)");
        if (newWindow.startMinute == -1) {
            std::cout << "Редагування часового вікна скасовано.\n";
            return;
        }
        newWindow.endMinute = readMinutes("Введіть новий час завершення (HH:MM)");
        if (newWindow.endMinute == -1) {
            std::cout << "Редагування часового вікна скасовано.\n";
            return;
        }

        if (isWindowOk(newWindow, others)) {
            break;
        }
        std::cout << "Введіть вікно ще раз.\n";
    }

    try {
        _cards.editTimeWindow(cardId, windowNumber - 1, newWindow);
        std::cout << "Часове вікно успішно відредаговано.\n";
    } catch (const std::exception& e) {
        std::cout << "Не вдалося відредагувати часове вікно: " << e.what() << '\n';
    }
}

void AdminController::revokeCard(const std::string& cardId) {
    if (cardId == _currentCardId) {
        std::cout << "Неможливо відкликати картку адміна.\n";
        return;
    }

    if (!askYesNo("Відкликання не можна скасувати. Відкликати картку " + cardId + "?")) {
        std::cout << "Відкликання скасовано.\n";
        return;
    }

    try {
        _cards.revokeCard(cardId);
        std::cout << "Картку успішно відкликано!\n";
    } catch (const std::exception& e) {
        std::cout << "Не вдалося відкликати картку: " << e.what() << '\n';
    }
}

void AdminController::blockCard(const std::string& cardId) {
    if (cardId == _currentCardId) {
        std::cout << "Не можна заблокувати картку, з якою ви зараз увійшли.\n";
        return;
    }

    try {
        _cards.blockCard(cardId);
        std::cout << "Картку успішно заблоковано!\n";
    } catch (const std::exception& e) {
        std::cout << "Не вдалося заблокувати картку: " << e.what() << '\n';
    }
}

void AdminController::unblockCard(const std::string& cardId) {
    try {
        _cards.unblockCard(cardId);
        std::cout << "Картку успішно розблоковано!\n";
    } catch (const std::exception& e) {
        std::cout << "Не вдалося розблокувати картку: " << e.what() << '\n';
    }
}

void AdminController::changePin(const std::string& cardId) {
    std::string newPin;
    while(true){

        std::cout << "Введіть новий PIN (0 - скасувати): ";

        if(!readLine(newPin)){
            std::cout << "Зміну PIN скасовано.\n";
            return;
        }
        if (newPin == "0") {
            std::cout << "Зміну PIN скасовано.\n";
            return;
        }
        if (!pinOk(newPin)) {
            continue;
        }

        try {
            _cards.changePin(cardId, newPin);

            std::cout << "PIN успішно змінено!\n";
            return;
        } catch (const std::invalid_argument& e) {
            std::cout << e.what() << '\n';
        }catch (const std::exception& e) {
            std::cout << "Не вдалося змінити PIN: "
                      << e.what() << '\n';
            return;
        }
    }
}
