#include "Services/DataBaseInitializer.h"
#include <string>
#include <cstdio>


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

int main() {
    std::string dbPath     = std::string(PROJECT_DIR) + "/data/db.sqlite3";
    std::string schemaPath = std::string(PROJECT_DIR) + "/data/schema.sql";
    std::string seedPath = std::string(PROJECT_DIR) + "/data/seed.sql";

    DataBaseInitializer dbInit(dbPath, schemaPath, seedPath);

    if (!dbInit.open())       return 1;

    //розкоментуйте ТІЛЬКИ на першому запуску!
    //бо будуть постійно дописуватися рядочки
    //або: видалити db.sqlite3 і запустити знову

    // if (!dbInit.initialize()) return 1;
    // int ch = check(dbInit);
    // if (ch == 1 ) return 1;

    std::printf("Database ready\n");

    dbInit.close();
    return 0;
}


// int main() {
//     std::cout << "Klumba access terminal\n";

//     sqlite3_close(db);

//     return 0;
// }
//
// Created by karnavka on 10/15/26.
//


