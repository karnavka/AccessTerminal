
#ifdef _WIN32
#include <windows.h>
#endif

#include <string>
#include <cstdio>
#include <cassert>
#include <sstream>
#include <iostream>

#include "Services/DataBaseInitializer.h"
#include "Services/HashService.h"
#include "Services/UserService.h"
#include "Services/AccessPolicyService.h"
#include "Services/LogService.h"

#include "Repositories/UserRepository.h"
#include "Repositories/CardRepository.h"
#include "Repositories/LogRepository.h"

#include "  Controllers/AdminController.h"

#include "tests_part/TestsPart.h"




int main() {
    //без цього у мене якісь виклики демонів у консолі на вінд
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif


    std::string dbPath     = std::string(PROJECT_DIR) + "/data/db.sqlite3";
    std::string schemaPath = std::string(PROJECT_DIR) + "/data/schema.sql";
    std::string seedPath = std::string(PROJECT_DIR) + "/data/seed.sql";

    DataBaseInitializer dbInit(dbPath, schemaPath, seedPath);

    if (!dbInit.open())       return 1;

    // створює таблиці, якщо їх ще немає, а seed додає тільки на першому запуску,
    // тому нічого не треба ні коментувати, ні розкоментовувати
    // щоб скинути бд до початкових даних: видалити data/db.sqlite3 і запустити
    if (!dbInit.initialize()) return 1;

    // all inits

    sqlite3* db = dbInit.getConnection();

    // Repositories
    UserRepository userRepo(db);
    CardRepository cardRepo(db);
    LogRepository logRepo(db);

    // Services
    UserService users(userRepo);

    HashService hash;
    SecurityConfig securityConfig{};
    testHash();

    CardService cards(db, cardRepo, hash, securityConfig);
    LogService logs(logRepo);    

    AdminController admin(users, cards, logs, hash);

    //tests additional
    testAdmin(admin);


    

    std::printf("Database ready\n");

    dbInit.close();
    return 0;
}





