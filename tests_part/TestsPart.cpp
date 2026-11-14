#include <cassert>

#include "tests_part/TestsPart.h"

#include "Services/HashService.h"

#include <sstream>
#include <iostream>

void testHash(){
    HashService service;

    auto hash1 = service.hash("123456");
    auto hash2 = service.hash("123456");

    // assert(hash1 != hash2);

    assert(service.verify("123456", hash1));
    assert(!service.verify("123457", hash1));
    assert(!service.verify("123456", hash2) == false);
}

//згенерований чек щоб подивитися що всьо працює :P
int check(const DataBaseInitializer& db){
    const char* sql =
        "SELECT u.name, u.role, c.card_id, COUNT(w.id) "
        "FROM users u "
        "JOIN cards c ON c.user_id = u.id "
        "LEFT JOIN card_time_windows w ON w.card_id = c.card_id "
        "GROUP BY c.card_id ORDER BY c.card_id;";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db.getConnection(), sql, -1, &st, nullptr) != SQLITE_OK) {
        std::printf("Check failed: %s\n", sqlite3_errmsg(db.getConnection()));
        return 1;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        std::printf("%s | %s | %s | windows: %d\n",
                    reinterpret_cast<const char*>(sqlite3_column_text(st, 0)),
                    reinterpret_cast<const char*>(sqlite3_column_text(st, 1)),
                    reinterpret_cast<const char*>(sqlite3_column_text(st, 2)),
                    sqlite3_column_int(st, 3));
    }
    sqlite3_finalize(st);
    return 0;
}

void testAdmin(AdminController& ac){

    std::string input;
    std::cout << "хочете тестувати адміна? [y/n]" << std::endl;

    if (!std::getline(std::cin, input) || (input == "n")) {
        return;
    }
    if (input == "y"){
        ac.run();
    }
}