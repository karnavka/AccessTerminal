#pragma once

#include <string>
#include <vector>

#include "../Services/UserService.h"
#include "../Services/CardService.h"
#include "../Services/LogService.h"
#include "../Services/HashService.h"

class AdminController {
public:
    AdminController(UserService& users, CardService& cards, LogService& logs, HashService& hash);

    void run();

private:
    // вхід в режим адміністратора
    bool login();
    // головне меню
    void showMenu();

    // 1. перегляд інформації
    void showInformationMenu();
    void showUsers();
    void showCards();
    void showLog();
    void filterLog();

    // 2. додати користувача
    void addUser();

    // 3. додати картку користувачу
    void issueCard();

    int readMinutes(const std::string& prompt);
    int readDayOfWeek(const std::string& prompt);
    bool isWindowOk(const TimeWindow& window, const std::vector<TimeWindow>& others);
    bool askYesNo(const std::string& question);

    // 4. змінити дані картки
    void editCard();

    void changePin(const std::string& cardId);
    void revokeCard(const std::string& cardId);
    void blockCard(const std::string& cardId);
    void unblockCard(const std::string& cardId);

    void editTimeWindows(const std::string& cardId);
    void showTimeWindows(const std::string& cardId);
    void addTimeWindow(const std::string& cardId);
    void removeTimeWindow(const std::string& cardId);
    void editTimeWindow(const std::string& cardId);

private:
    UserService& _users;
    CardService& _cards;
    LogService& _logs;
    HashService& _hash;

    std::string _currentCardId;
    int _currentUserId = 0;
};
