//
// Created by Христина Кузь on 08.11.2026.
//

#include "AdminController.h"
#include <cctype>
#include <ctime>
#include <iostream>
#include <sstream>

namespace {

//фікс нескінченного циклу: false, якщо ввід закінчився (Ctrl+D / Ctrl+Z)
bool readLine(std::string& line) {
    if (!std::getline(std::cin, line)) {
        std::cout << '\n';
        return false;
    }
    // прибираємо пробіли і \r по краях (на вінді консоль може додати \r в кінці)
    const auto first = line.find_first_not_of(" \t\r");
    const auto last = line.find_last_not_of(" \t\r");
    line = (first == std::string::npos) ? "" : line.substr(first, last - first + 1);
    return true;
}

//це перевірка на те що введено саме число, без хвостів
bool parseInt(const std::string& input, int& value) {
    std::istringstream stream(input);
    if (!(stream >> value)) {
        return false;
    }
    stream >> std::ws;
    return stream.eof();
}

//це перевірка на те що пін з 4-8 цифр
bool isValidPin(const std::string& pin) {
    if (pin.size() < 4 || pin.size() > 8) {
        return false;
    }
    for (char ch : pin) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
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

// в бд час в UTC, а адміну показуємо місцевий
std::string toLocalString(TimePoint tp) {
    const std::tm local = toLocalTm(tp);
    char buf[20];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &local);
    return buf;
}

//це перевірка на те що така дата справді існує
// повертає початок цього дня за місцевим часом
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

    // mktime може не повернути фолз а зсунути дні, тому перевріємо і якщо що повертаємо фвлз
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

} // namespace

AdminController::AdminController(UserService& users, CardService& cards, LogService& logs, HashService& hash)
    : _users(users), _cards(cards), _logs(logs), _hash(hash) {}

// запуск ракєти
void AdminController::run() {
    if (!login()) {
        return;
    }
    showMenu();
}

bool AdminController::login() {
    std::string cardId;
    std::optional<CardEntity> card;

    // цикл щоб можна було ввести id ще раз
    while (true) {
        std::cout << "Введіть ID карточки (0 - вийти): ";
        if (!readLine(cardId)) {
            return false;
        }
        if (cardId == "0") {
            return false;
        }
        if (cardId.empty()) {
            std::cout << "ID картки не може бути порожнім. Спробуйте ще раз.\n";
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

        // для адміна часові вікна не перевіряємо, він може зайти будь-коли

        std::string pin;
        while (true) {
            std::cout << "Введіть PIN (0 - вийти): ";
            if (!readLine(pin)) {
                return false;
            }
            if (pin == "0") {
                return false;
            }

            //наш хеш
            if (_hash.verify(pin, card->pinHash)) {
                break;
            }

            //адмін у нас один, тому його картку не блокуємо і пін можна вводити скільки завгодно
            //(але в журнал невдалу спробу все одно пишемо)
            _logs.log(cardId, LogResult::Denied, LogReason::WrongPin);
            std::cout << "Неправильний PIN. Спробуйте ще раз.\n";
        }

        _logs.log(cardId, LogResult::Success);

        // запамʼятовуємо хто зайшов, щоб адмін не відкликав сам себе
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
    int choice;

    while (true) {
        std::cout << "\n===== МЕНЮ адміністратора =====\n";
        std::cout << "1. Перегляд інформації\n";
        std::cout << "2. Додати користувача\n";
        std::cout << "3. Додати карту для користувача\n";
        std::cout << "4. Змінити дані картки\n";
        std::cout << "0. Вийти\n";
        std::cout << "Виберіть варіант: ";

        std::string input;
        //фікс нескінченного циклу
        if (!readLine(input)) {
            std::cout << "Вихід з режиму адміністратора...\n";
            return;
        }
        if (!parseInt(input, choice)) {
            std::cout << "Некоректне введення. Спробуйте ще раз.\n";
            continue;
        }

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
                std::cout << "Вихід з режиму адміністратора...\n";
                return;
            default:
                std::cout << "Такого варіанту немає. Спробуйте ще раз.\n";
        }
    }
}

void AdminController::showInformationMenu() {
    int choice;

    while (true) {
        std::cout << "\n===== Перегляд інформації =====\n";
        std::cout << "1. Список користувачів\n";
        std::cout << "2. Список карток\n";
        std::cout << "3. Журнал спроб доступу\n";
        std::cout << "0. Назад\n";
        std::cout << "Виберіть варіант: ";

        std::string input;
        //фікс нескінченного циклу
        if (!readLine(input)) {
            return;
        }
        if (!parseInt(input, choice)) {
            std::cout << "Некоректне введення. Спробуйте ще раз.\n";
            continue;
        }

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
    int choice;
    LogFilter filter;
    std::string input;

    while (true) {
        std::cout << "\nБажаєте відфільтрувати журнал?\n";
        std::cout << "1. За результатом\n";
        std::cout << "2. За датою\n";
        std::cout << "3. За ID картки\n";
        std::cout << "0. Ні\n";
        std::cout << "Виберіть варіант: ";

        if (!readLine(input)) {
            return;
        }

        if (!parseInt(input, choice)) {
            std::cout << "Некоректне введення. Спробуйте ще раз або натисніть 0 для повернення.\n";
            continue;
        }
        if (choice >= 0 && choice <= 3) {
            break;
        }
        std::cout << "Невідомий пункт меню. Спробуйте ще раз або натисніть 0 для повернення.\n";
    }

    switch (choice) {
        case 1: {
            int resultChoice;

            while (true) {
                std::cout << "\nОберіть результат:\n";
                std::cout << "1. Успішний доступ\n";
                std::cout << "2. Відмовлено в доступі\n";
                std::cout << "3. Тривога\n";
                std::cout << "0. Назад\n";
                std::cout << "Ваш вибір: ";

                if (!readLine(input)) {
                    return;
                }

                if (!parseInt(input, resultChoice)) {
                    std::cout << "Некоректне введення. Спробуйте ще раз або натисніть 0 для скасування.\n";
                    continue;
                }

                if (resultChoice == 0) {
                    return;
                }
                if (resultChoice == 1) {
                    filter.result = LogResult::Success;
                    break;
                }
                if (resultChoice == 2) {
                    filter.result = LogResult::Denied;
                    break;
                }
                if (resultChoice == 3) {
                    filter.result = LogResult::Alarm;
                    break;
                }
                std::cout << "Невідомий результат. Спробуйте ще раз або натисніть 0 для скасування.\n";
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

                // день рахуємо за місцевим часом, а не по UTC
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
                if (!cardId.empty()) {
                    filter.cardId = cardId;
                    break;
                }
                std::cout << "ID картки не може бути порожнім. Спробуйте ще раз або натисніть 0 для скасування.\n";
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
    int roleChoice;
    UserRole role;

    std::cout << "\n===== Додавання користувача =====\n";

    while (true) {
        std::cout << "Введіть ім'я користувача (0 - скасувати): ";
        //фікс нескінченного циклу
        if (!readLine(name)) {
            return;
        }

        if (name == "0") {
            std::cout << "Додавання користувача скасовано.\n";
            return;
        }

        bool isEmpty = true;

        for (char ch : name) {
            if (!std::isspace(static_cast<unsigned char>(ch))) {
                isEmpty = false;
                break;
            }
        }
        if (!isEmpty) {
            break;
        }
        std::cout << "Ім'я не може бути порожнім. Спробуйте ще раз або натисніть 0 для скасування.\n";
    }

    while (true) {
        std::cout << "\nОберіть роль:\n";
        std::cout << "1. Звичайний користувач\n";
        std::cout << "2. Адміністратор\n";
        std::cout << "0. Скасувати додавання користувача\n";
        std::cout << "Ваш вибір: ";

        std::string input;
        //фікс нескінченного циклу
        if (!readLine(input)) {
            return;
        }
        if (!parseInt(input, roleChoice)) {
            std::cout << "Некоректне введення. Спробуйте ще раз.\n";
            continue;
        }

        if (roleChoice == 0) {
            std::cout << "Додавання користувача скасовано.\n";
            return;
        }
        if (roleChoice == 1) {
            role = UserRole::User;
            break;
        }
        if (roleChoice == 2) {
            role = UserRole::Admin;
            break;
        }
        std::cout << "Такої ролі не існує. Спробуйте ще раз або натисніть 0 для скасування.\n";
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
        //фікс нескінченного циклу
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

        if (input >> hours >> separator >> minutes && separator == ':' && hours >= 0 && hours <= 23 && minutes >= 0 && minutes <= 59) {
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
        //фікс нескінченного циклу
        if (!readLine(input)) {
            return -1;
        }
        if (input == "0") {
            return -1;
        }

        int day;
        if (parseInt(input, day) && day >= 1 && day <= 7) {
            // у бд дні з 0 до 6, а людині зручніше вводити з 1 до 7
            return day - 1;
        }
        std::cout << "Некоректний день тижня. Введіть число від 1 до 7 або 0 для скасування.\n";
    }
}

bool AdminController::isWindowOk(const TimeWindow& window, const std::vector<TimeWindow>& others) {
    //це перевірка на те що часове вікно валідне
    if (window.startMinute >= window.endMinute) {
        std::cout << "Час початку має бути раніше за час завершення.\n";
        return false;
    }

    //це перевірка на те щоб вікна в один день не перетиналися
    for (const TimeWindow& other : others) {
        if (other.dayOfWeek == window.dayOfWeek &&
            window.startMinute < other.endMinute &&
            other.startMinute < window.endMinute) {
            std::cout << "Це вікно перетинається з іншим вікном цього дня ("
                      << formatMinutes(other.startMinute) << " - " << formatMinutes(other.endMinute) << ").\n";
            return false;
        }
    }
    return true;
}

bool AdminController::askYesNo(const std::string& question) {
    while (true) {
        std::string answer;

        std::cout << question << " (так/ні): ";
        //фікс нескінченного циклу
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
        //фікс нескінченного циклу
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

        //це перевірка на те що такий користувач існує
        if (!_users.getUser(userId)) {
            std::cout << "Користувача з таким ID не знайдено. Спробуйте ще раз або введіть 0 для скасування.\n";
            continue;
        }

        //це перевірка на те що в людини ще немає картки (щоб не вводити все дарма)
        if (_users.hasActiveCard(userId)) {
            std::cout << "У цього користувача вже є активна картка. Спочатку відкличте її.\n";
            continue;
        }
        break;
    }

    // TODO генератор ID, не самостійне введення
    // можливо з кард сервіс викликається метод генерації id
    // цей блок while прибрати і викликати метод генерування
    // зберігати id в змінній cardId
    while (true) {
        std::cout << "Введіть ID картки (0 - скасувати): ";
        //фікс нескінченного циклу
        if (!readLine(cardId)) {
            return;
        }

        if (cardId == "0") {
            std::cout << "Додавання картки скасовано.\n";
            return;
        }
        if (cardId.empty()) {
            std::cout << "ID картки не може бути порожнім. Спробуйте ще раз або введіть 0 для скасування.\n";
            continue;
        }
        //це перевірка на те що такого id ще немає
        if (_cards.getCard(cardId)) {
            std::cout << "Картка з таким ID вже існує. Спробуйте інший або введіть 0 для скасування.\n";
            continue;
        }
        break;
    }

    while (true) {
        std::cout << "Введіть PIN (0 - скасувати): ";
        //фікс нескінченного циклу
        if (!readLine(pin)) {
            return;
        }

        if (pin == "0") {
            std::cout << "Додавання картки скасовано.\n";
            return;
        }
        if (isValidPin(pin)) {
            break;
        }
        std::cout << "PIN має складатися з 4-8 цифр. Спробуйте ще раз або введіть 0 для скасування.\n";
    }

    while (true) {
        std::string input;

        std::cout << "Введіть кількість часових проміжків (0 - скасувати): ";
        //фікс нескінченного циклу
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

            // якщо вікно погане, вводимо саме його ще раз, а не все з початку
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

        //наш хеш: пін хешує сам CardService::issueCard,
        //тому сюди передаємо звичайний пін (якщо хешувати і тут, то хешується двічі і validatePin не пропускає)
        _cards.issueCard(userId, cardId, pin, windows);
        std::cout << "Картку успішно додано!\n";
    } catch (const std::exception& e) {
        std::cout << "Не вдалося додати картку: " << e.what() << '\n';
    }
}

void AdminController::editCard() {
    std::string cardId;

    while (true) {
        std::cout << "Введіть ID картки (0 - скасувати): ";
        //фікс нескінченного циклу
        if (!readLine(cardId)) {
            return;
        }

        if (cardId == "0") {
            std::cout << "Редагування картки скасовано.\n";
            return;
        }
        if (cardId.empty()) {
            std::cout << "ID картки не може бути порожнім. Спробуйте ще раз або введіть 0 для скасування.\n";
            continue;
        }
        if (!_cards.getCard(cardId)) {
            std::cout << "Картку не знайдено. Перевірте ID або введіть 0 для скасування.\n";
            continue;
        }
        break;
    }

    int choice;

    while (true) {
        // статус міг змінитися після попередньої дії, тому читаємо картку щоразу
        auto card = _cards.getCard(cardId);
        if (!card) {
            std::cout << "Картку не знайдено.\n";
            return;
        }

        //це перевірка на те що відкликану картку вже не редагують
        if (card->status == CardStatus::Revoked) {
            showTimeWindows(cardId);
            std::cout << "Картку " << cardId << " відкликано, змінювати її вже не можна.\n";
            return;
        }

        const bool isBlocked = card->status == CardStatus::Blocked;

        std::cout << "\n===== Редагування картки " << cardId << " (" << toString(card->status) << ") =====\n";
        std::cout << "1. Змінити PIN\n";
        std::cout << "2. Відкликати картку\n";
        if (isBlocked) {
            std::cout << "3. Розблокувати картку\n";
        }
        std::cout << "4. Редагувати часові вікна\n";
        if (!isBlocked) {
            std::cout << "5. Заблокувати картку\n";
        }
        std::cout << "0. Назад\n";
        std::cout << "Ваш вибір: ";

        std::string input;
        //фікс нескінченного циклу
        if (!readLine(input)) {
            return;
        }

        if (!parseInt(input, choice)) {
            std::cout << "Некоректне введення. Введіть номер пункту меню.\n";
            continue;
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
                    //це перевірка на те що розблоковують тільки заблоковану
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
    int choice;

    while (true) {
        showTimeWindows(cardId);

        std::cout << "\n===== Редагування часових вікон =====\n";
        std::cout << "1. Додати часове вікно\n";
        std::cout << "2. Видалити часове вікно\n";
        std::cout << "3. Редагувати часове вікно\n";
        std::cout << "0. Назад\n";
        std::cout << "Ваш вибір: ";

        std::string input;
        //фікс нескінченного циклу
        if (!readLine(input)) {
            return;
        }

        if (!parseInt(input, choice)) {
            std::cout << "Некоректне введення. Введіть номер пункту меню.\n";
            continue;
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

    // у бд 0 - понеділок ... 6 - неділя
    const std::vector<std::string> days = {
        "Понеділок", "Вівторок", "Середа", "Четвер","П'ятниця", "Субота", "Неділя"
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
        //фікс нескінченного циклу
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

    // вікно, яке редагуємо, саме з собою не порівнюємо
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
    //це перевірка на те щоб адмін не відкликав картку, з якою зайшов
    //(тоді завжди лишається хоча б один адмін з робочою карткою)
    if (cardId == _currentCardId) {
        std::cout << "Не можна відкликати картку, з якою ви зараз увійшли.\n";
        return;
    }

    //це перевірка на те що користувач ТОЧНО впевнений
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
    //це перевірка на те щоб адмін не заблокував сам себе
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
        if (!isValidPin(newPin)) {
            std::cout << "PIN має складатися з 4-8 цифр. Спробуйте ще раз.\n";
            continue;
        }

        try {
            //наш хеш (хешує CardService::changePin)
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
