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
#include "Services/SecurityService.h"

#include "Repositories/UserRepository.h"
#include "Repositories/CardRepository.h"
#include "Repositories/LogRepository.h"

#include "  Controllers/AdminController.h"
#include "  Controllers/TerminalController.h"
#include "Entity/DoorSimulator.h"

#include "tests_part/TestsPart.h"

//returns: ok - 0 ; error - 1
int createDataOnFirstOpen(DataBaseInitializer& dbInit) {
    if (!dbInit.initialize()) return 1;
    int ch = check(dbInit);
    if (ch == 1 ) return 1;
    return 0;
}

// імітація сирени
void sirenSink(AlarmReason reason) {
    std::cout << "\n*** СИРЕНА: "
              << (reason == AlarmReason::ForcedOpen ? "злам дверей" : "забагато спроб PIN")
              << " ***\n";
}

void runMenu(TerminalController& terminal, AdminController& admin) {
    while (true) {
        std::cout << "\n1 - Terminal\n2 - Admin (remote)\n0 - Exit\n> ";
        std::string choice;
        if (!std::getline(std::cin, choice) || choice == "0") return;

        if (choice == "1")      terminal.run();
        else if (choice == "2") admin.run();
        else                    std::cout << "Unknown option.\n";
    }
}


int main() {
    std::string dbPath     = std::string(PROJECT_DIR) + "/data/db.sqlite3";
    std::string schemaPath = std::string(PROJECT_DIR) + "/data/schema.sql";
    std::string seedPath = std::string(PROJECT_DIR) + "/data/seed.sql";

    DataBaseInitializer dbInit(dbPath, schemaPath, seedPath);
    if (!dbInit.open()) return 1;

    //розкоментуйте на першому запуску
    //або для оновлення БД: видалити data/db.sqlite3, виконати код
    // if (createDataOnFirstOpen(dbInit) == 1) return 1;

    sqlite3* db = dbInit.getConnection();


    // Repositories
    UserRepository userRepo(db);
    CardRepository cardRepo(db);
    LogRepository logRepo(db);
    SecurityConfig securityConfig{};

    // Services
    UserService users(userRepo);
    HashService hash;
    CardService cards(db, cardRepo, hash, securityConfig);
    AccessPolicyService access(cards);
    LogService logs(logRepo);
    SecurityService security(db, cards, logs, sirenSink);

    // Hardware simulation
    DoorSimulator door;

    //Controllers
    AdminController admin(users, cards, logs, hash);
    TerminalController terminal(cards, security, access, logs, door, securityConfig.doorOpenTimeout);

    runMenu(terminal, admin);

    //tests additional
    // testAdmin(admin);
    // testTerminal(terminal);
    // testHash();


    dbInit.close();
    return 0;
}





