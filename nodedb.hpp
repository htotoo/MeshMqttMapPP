#ifndef NODEDB_HPP
#define NODEDB_HPP

#include <iostream>
#include <sqlite3.h>
#include "nodenamemap.hpp"
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

    void loadNodeNames(NodeNameMap& names) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!db) return;

        sqlite3_stmt* stmt;
        const char* sql = "SELECT node_id, short_name FROM nodes";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                uint32_t nodeId = sqlite3_column_int(stmt, 0);
                const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                if (!name) continue;
                names.setNodeName(nodeId, name);
            }
        } else {
            std::cerr << "Error preparing statement: " << sqlite3_errmsg(db) << std::endl;
        }
        sqlite3_finalize(stmt);
    }

    void saveChatMessage(uint32_t nodeId, uint16_t chan_id, const std::string& message) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!db) return;

        sqlite3_stmt* stmt;
        const char* sql = "INSERT INTO chat (node_id, chan_id, message) VALUES (?, ?, ?)";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, nodeId);
            sqlite3_bind_int(stmt, 2, chan_id);
            sqlite3_bind_text(stmt, 3, message.c_str(), -1, SQLITE_STATIC);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                std::cerr << "Error inserting chat message: " << sqlite3_errmsg(db) << std::endl;
            }
        } else {
            std::cerr << "Error preparing statement: " << sqlite3_errmsg(db) << std::endl;
        }
        sqlite3_finalize(stmt);
    }

    void setNodeInfo(uint32_t nodeId, const std::string& shortName, const std::string& longName) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!db) return;

        sqlite3_stmt* stmt;
        const char* sql =
            "INSERT INTO nodes (node_id, short_name, long_name, last_updated) VALUES (?, ?, ?, CURRENT_TIMESTAMP) "
            "ON CONFLICT(node_id) DO UPDATE SET short_name=excluded.short_name, long_name=excluded.long_name, last_updated=CURRENT_TIMESTAMP";
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

    void setNodeTemperature(uint32_t nodeId, float temperature) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!db) return;
        if (temperature < -100 || temperature > 300) {
            return;  // Skip invalid temperature values
        }

        sqlite3_stmt* stmt;
        const char* sql = "UPDATE nodes SET temperature = ?, last_updated = CURRENT_TIMESTAMP WHERE node_id = ?";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_double(stmt, 1, temperature);
            sqlite3_bind_int(stmt, 2, nodeId);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                std::cerr << "Error updating node temperature: " << sqlite3_errmsg(db) << std::endl;
            }
        } else {
            std::cerr << "Error preparing statement: " << sqlite3_errmsg(db) << std::endl;
        }
        sqlite3_finalize(stmt);
    }

    void setNodeBattery(uint32_t nodeId, int batteryLevel) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!db) return;
        if (batteryLevel < 0 || batteryLevel > 101) {
            return;  // Skip invalid battery levels
        }

        sqlite3_stmt* stmt;
        const char* sql = "UPDATE nodes SET battery_level = ?, last_updated = CURRENT_TIMESTAMP WHERE node_id = ?";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, batteryLevel);
            sqlite3_bind_int(stmt, 2, nodeId);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                std::cerr << "Error updating node battery level: " << sqlite3_errmsg(db) << std::endl;
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
            "temperature REAL, "
            "battery_level INTEGER, "
            "last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP);";
        const char* sql2 =
            "CREATE TABLE IF NOT EXISTS chat ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "node_id INTEGER, "
            "chan_id INTEGER, "
            "message TEXT, "
            "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP);";
        if (db) {
            char* errMsg = nullptr;
            if (sqlite3_exec(db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
                std::cerr << "SQL error: " << errMsg << std::endl;
                sqlite3_free(errMsg);
                errMsg = nullptr;
            }
            if (sqlite3_exec(db, sql2, nullptr, nullptr, &errMsg) != SQLITE_OK) {
                std::cerr << "SQL error: " << errMsg << std::endl;
                sqlite3_free(errMsg);
            }
        }
    }
    sqlite3* db = nullptr;
    std::mutex mtx;
};

#endif  // NODEDB_HPP