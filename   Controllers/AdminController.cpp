//
// Created by Христина Кузь on 08.11.2026.
//

#include "AdminController.h"
#include <iostream>
#include <limits>
#include <sstream>

AdminController::AdminController(UserService& users, CardService& cards, LogService& logs)
    : _users(users), _cards(cards), _logs(logs) {}

// запуск ракєти
void AdminController::run() {
    if (!login()) {
        return;
    }
    showMenu();
}

// TODO той, хто писатиме це, то треба ще додати цикли while для id і pin
// щоб якщо людина помилилась, то можна спробувати ще раз
bool AdminController::login() {
    std::string cardId;
    std::string pin;

    std::cout << "Введіть ID карточки: ";
    std::getline(std::cin, cardId);

    std::cout << "Введіть PIN: ";
    std::getline(std::cin, pin);

    // TODO: cardId і pin у AccessTerminalService
    // перевірити всьо що треба і отримати userId власника картки

    // TODO: перевірити роль користувача через UserService::isAdmin
    // if (!_users.isAdmin(userId)) {
    //     std::cout << "Ви не адміністратор! Ану пішли геть!\n";
    //     return false;
    // }
    return true; // замінити!!! в залежності від перевірки
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

        std::cin >> choice;

        if (std::cin.fail()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Некоректне введення. Спробуйте ще раз.\n";
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

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

        std::cin >> choice;

        if (std::cin.fail()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Некоректне введення. Спробуйте ще раз.\n";
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

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
        std::cout << "ID запису: " << entry.id << '\n';
        std::cout << "Час: " << toIso(entry.timestamp) << '\n';
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

        if (!std::getline(std::cin, input)) {
            return;
        }
        std::istringstream stream(input);

        if (!(stream >> choice)) {
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

                if (!std::getline(std::cin, input)) {
                    return;
                }
                std::istringstream stream(input);

                if (!(stream >> resultChoice)) {
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

                if (!std::getline(std::cin, date)) {
                    return;
                }
                if (date == "0") {
                    return;
                }

                try {
                    TimePoint from = fromIso(date + "T00:00:00Z");
                    filter.from = from;
                    filter.to = from + std::chrono::hours(24);
                    break;
                } catch (const std::exception&) {
                    std::cout << "Некоректна дата. Спробуйте ще раз або натисніть 0 для скасування.\n";
                }
            }
            break;
        }

        case 3: {
            std::string cardId;

            while (true) {
                std::cout << "Введіть ID картки (0 - скасувати фільтрацію): ";

                if (!std::getline(std::cin, cardId)) {
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
        std::cout << "ID запису: " << entry.id << '\n';
        std::cout << "Час: " << toIso(entry.timestamp) << '\n';
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
}

void AdminController::addUser() {
    std::string name;
    int roleChoice;
    UserRole role;

    std::cout << "\n===== Додавання користувача =====\n";

    while (true) {
        std::cout << "Введіть ім'я користувача (0 - скасувати): ";
        std::getline(std::cin, name);

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

        std::cin >> roleChoice;

        if (std::cin.fail()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
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
        std::getline(std::cin, time);

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
        std::getline(std::cin, input);

        if (input == "0") {
            std::cout << "Додавання картки скасовано.\n";
            return;
        }
        std::istringstream stream(input);

        if (stream >> userId) {
            stream >> std::ws;

            if (stream.eof() && userId > 0) {
                break;
            }
        }
        std::cout << "Некоректний ID користувача. Введіть додатне ціле число або 0 для скасування.\n";
    }

    // TODO генератор ID, не самостійне введення
    // можливо з кард сервіс викликається метод генерації id
    // цей блок while прибрати і викликати метод генерування
    // зберігати id в змінній cardId
    while (true) {
        std::cout << "Введіть ID картки (0 - скасувати): ";
        std::getline(std::cin, cardId);

        if (cardId == "0") {
            std::cout << "Додавання картки скасовано.\n";
            return;
        }
        if (!cardId.empty()) {
            break;
        }
        std::cout << "ID картки не може бути порожнім. Спробуйте ще раз або введіть 0 для скасування.\n";
    }

    while (true) {
        std::cout << "Введіть PIN (0 - скасувати): ";
        std::getline(std::cin, pin);

        if (pin == "0") {
            std::cout << "Додавання картки скасовано.\n";
            return;
        }
        if (!pin.empty()) {
            break;
        }
        std::cout << "PIN не може бути порожнім. Спробуйте ще раз або введіть 0 для скасування.\n";
    }

    while (true) {
        std::string input;

        std::cout << "Введіть кількість часових проміжків (0 - скасувати): ";
        std::getline(std::cin, input);

        if (input == "0") {
            std::cout << "Додавання картки скасовано.\n";
            return;
        }
        std::istringstream stream(input);

        if (stream >> numberOfWindows) {
            stream >> std::ws;
            if (stream.eof() && numberOfWindows > 0) {
                break;
            }
        }
        std::cout << "Некоректна кількість проміжків. Введіть додатне ціле число або 0 для скасування.\n";
    }

    try {
        for (int i = 0; i < numberOfWindows; i++) {
            TimeWindow window;

            std::cout << "\nЧасовий проміжок " << i + 1 << '\n';

            while (true) {
                std::string input;

                std::cout << "День тижня (1 - понеділок, 7 - неділя, 0 - скасувати): ";
                std::getline(std::cin, input);

                if (input == "0") {
                    std::cout << "Додавання картки скасовано.\n";
                    return;
                }
                std::istringstream stream(input);

                if (stream >> window.dayOfWeek) {
                    stream >> std::ws;
                    if (stream.eof() &&
                        window.dayOfWeek >= 1 &&
                        window.dayOfWeek <= 7) {
                        break;
                    }
                }
                std::cout << "Некоректний день тижня. Введіть число від 1 до 7, або 0 для скасування.\n";
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
            windows.push_back(window);
        }

        // TODO: HashService - безпечне хешування PIN перед його збереженням

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
        std::getline(std::cin, cardId);

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
        std::cout << "\n===== Редагування картки =====\n";
        std::cout << "1. Змінити PIN\n";
        std::cout << "2. Відкликати картку\n";
        std::cout << "3. Розблокувати картку\n";
        std::cout << "4. Редагувати часові вікна\n";
        std::cout << "0. Назад\n";
        std::cout << "Ваш вибір: ";

        std::string input;
        std::getline(std::cin, input);

        std::istringstream stream(input);

        if (!(stream >> choice)) {
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
                    unblockCard(cardId);
                    break;
                case 4:
                    editTimeWindows(cardId);
                    break;
                case 0:
                    return;
                default:
                    std::cout << "Невідомий пункт меню. Введіть число від 0 до 4.\n";
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
        std::getline(std::cin, input);

        std::istringstream stream(input);

        if (!(stream >> choice)) {
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

    const std::vector<std::string> days = {
        "Понеділок", "Вівторок", "Середа", "Четвер","П'ятниця", "Субота", "Неділя"
    };

    std::cout << "\n===== Часові вікна картки " << cardId << " =====\n";

    for (std::size_t i = 0; i < card->timeWindows.size(); i++) {
        const TimeWindow& window = card->timeWindows[i];

        std::cout << i + 1 << ". ";

        if (window.dayOfWeek >= 1 && window.dayOfWeek <= 7) {
            std::cout << days[window.dayOfWeek - 1];
        } else {
            std::cout << "Невідомий день";
        }

        int startHours = window.startMinute / 60;
        int startMinutes = window.startMinute % 60;
        int endHours = window.endMinute / 60;
        int endMinutes = window.endMinute % 60;

        std::cout << " | ";
        if (startHours < 10) std::cout << '0';
        std::cout << startHours << ':';
        if (startMinutes < 10) std::cout << '0';
        std::cout << startMinutes;
        std::cout << " - ";
        if (endHours < 10) std::cout << '0';
        std::cout << endHours << ':';
        if (endMinutes < 10) std::cout << '0';
        std::cout << endMinutes << '\n';
    }
}

void AdminController::addTimeWindow(const std::string& cardId) {
    TimeWindow window;

    while (true) {
        std::string input;

        std::cout << "Введіть день тижня (1 - понеділок, 7 - неділя, 0 - скасувати): ";
        std::getline(std::cin, input);

        if (input == "0") {
            std::cout << "Додавання часового вікна скасовано.\n";
            return;
        }
        std::istringstream stream(input);

        if (stream >> window.dayOfWeek) {
            stream >> std::ws;
            if (stream.eof() && window.dayOfWeek >= 1 && window.dayOfWeek <= 7) {
                break;
            }
        }
        std::cout << "Некоректний день тижня. Введіть число від 1 до 7 або 0 для скасування.\n";
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
        std::getline(std::cin, input);

        if (input == "0") {
            std::cout << "Видалення часового вікна скасовано.\n";
            return;
        }

        std::istringstream stream(input);

        if (stream >> windowNumber) {
            stream >> std::ws;
            if (stream.eof() && windowNumber >= 1 && windowNumber <= static_cast<int>(card->timeWindows.size())) {
                break;
            }
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
        if (!std::getline(std::cin, input)) {
            std::cout << "Редагування часового вікна скасовано.\n";
            return;
        }
        if (input == "0") {
            std::cout << "Редагування часового вікна скасовано.\n";
            return;
        }

        std::istringstream stream(input);

        if (stream >> windowNumber) {
            stream >> std::ws;
            if (stream.eof() && windowNumber >= 1 && windowNumber <= static_cast<int>(card->timeWindows.size())) {
                break;
            }
        }
        std::cout << "Некоректний номер часового вікна. Введіть число від 1 до "
        << card->timeWindows.size() << " або 0 для скасування.\n";
    }

    TimeWindow newWindow;

    while (true) {
        std::string input;

        std::cout << "Введіть новий день тижня (1 - понеділок, 7 - неділя, 0 - скасувати): ";
        if (!std::getline(std::cin, input)) {
            std::cout << "Редагування часового вікна скасовано.\n";
            return;
        }
        if (input == "0") {
            std::cout << "Редагування часового вікна скасовано.\n";
            return;
        }

        std::istringstream stream(input);

        if (stream >> newWindow.dayOfWeek) {
            stream >> std::ws;
            if (stream.eof() && newWindow.dayOfWeek >= 1 && newWindow.dayOfWeek <= 7) {
                break;
            }
        }
        std::cout << "Некоректний день тижня. Введіть число від 1 до 7 або 0 для скасування.\n";
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

    try {
        _cards.editTimeWindow(cardId, windowNumber - 1, newWindow);
        std::cout << "Часове вікно успішно відредаговано.\n";
    } catch (const std::exception& e) {
        std::cout << "Не вдалося відредагувати часове вікно: " << e.what() << '\n';
    }
}

void AdminController::revokeCard(const std::string& cardId) {
    try {
        _cards.revokeCard(cardId);
        std::cout << "Картку успішно відкликано!\n";
    } catch (const std::exception& e) {
        std::cout << "Не вдалося відкликати картку: " << e.what() << '\n';
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
    // TODO з HashService і певно CardService (ну там подивитесь)
}