#ifndef NODEDB_HPP
#define NODEDB_HPP

#include <iostream>
#include <sqlite3.h>

#include <mutex>

class NodeDb {
   public:
    NodeDb(const std::string& dbFile) {
        std::lock_guard<std::mutex> lock(mtx);
        if (sqlite3_open(dbFile.c_str(), &db) != SQLITE_OK) {
            std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
            db = nullptr;
        }
        createTables();
    }

    ~NodeDb() {
        std::lock_guard<std::mutex> lock(mtx);
        if (db) {
            sqlite3_close(db);
        }
    }

    void setNodeInfo(uint32_t nodeId, const std::string& shortName, const std::string& longName) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!db) return;

        sqlite3_stmt* stmt;
        const char* sql = "INSERT OR REPLACE INTO nodes (node_id, short_name, long_name, last_updated) VALUES (?, ?, ?, CURRENT_TIMESTAMP)";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, nodeId);
            sqlite3_bind_text(stmt, 2, shortName.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, longName.c_str(), -1, SQLITE_STATIC);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                std::cerr << "Error inserting node info: " << sqlite3_errmsg(db) << std::endl;
            }
        } else {
            std::cerr << "Error preparing statement: " << sqlite3_errmsg(db) << std::endl;
        }
        sqlite3_finalize(stmt);
    }
    void setNodePosition(uint32_t nodeId, int64_t latitude, int64_t longitude, int altitude) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!db) return;

        sqlite3_stmt* stmt;
        const char* sql = "UPDATE nodes SET latitude = ?, longitude = ?, altitude = ?, last_updated = CURRENT_TIMESTAMP WHERE node_id = ?";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, latitude);
            sqlite3_bind_int64(stmt, 2, longitude);
            sqlite3_bind_int(stmt, 3, altitude);
            sqlite3_bind_int(stmt, 4, nodeId);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                std::cerr << "Error updating node position: " << sqlite3_errmsg(db) << std::endl;
            }
        } else {
            std::cerr << "Error preparing statement: " << sqlite3_errmsg(db) << std::endl;
        }
        sqlite3_finalize(stmt);
    }

   private:
    void createTables() {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS nodes ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "node_id INTEGER UNIQUE, "
            "short_name TEXT, "
            "long_name TEXT, "
            "latitude INTEGER, "
            "longitude INTEGER, "
            "altitude INTEGER, "
            "last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP);";
        if (db) {
            char* errMsg = nullptr;
            if (sqlite3_exec(db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
                std::cerr << "SQL error: " << errMsg << std::endl;
                sqlite3_free(errMsg);
            }
        }
    }
    sqlite3* db = nullptr;
    std::mutex mtx;
};

#endif  // NODEDB_HPP