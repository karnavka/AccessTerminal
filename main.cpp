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

//returns: ok - 0 ; error - 1
int createDataOnFirstOpen(DataBaseInitializer& dbInit) {
    if (!dbInit.initialize()) return 1;
    int ch = check(dbInit);
    if (ch == 1 ) return 1;
    return 0;
}



int main() {
    std::string dbPath     = std::string(PROJECT_DIR) + "/data/db.sqlite3";
    std::string schemaPath = std::string(PROJECT_DIR) + "/data/schema.sql";
    std::string seedPath = std::string(PROJECT_DIR) + "/data/seed.sql";

    DataBaseInitializer dbInit(dbPath, schemaPath, seedPath);

    if (!dbInit.open())       return 1;

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


    //розкоментуйте на першому запуску
    //або для оновлення БД: видалити data/db.sqlite3, виконати код
    // if (createDataOnFirstOpen(dbInit) == 1) return 1;
    

    std::printf("Database ready\n");

    dbInit.close();
    return 0;
}





