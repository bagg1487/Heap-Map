#include "db_client.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <regex>
#include <pqxx/pqxx>

namespace fs = std::filesystem;

DBClient::DBClient(const std::string& conn_string) {
    try {
        m_conn = std::make_unique<pqxx::connection>(conn_string);
        if (m_conn->is_open()) {
            std::cout << "Connected to PostgreSQL: " << m_conn->dbname() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "DB connection error: " << e.what() << std::endl;
    }
}

DBClient::~DBClient() = default;

bool DBClient::isConnected() const {
    return m_conn && m_conn->is_open();
}

bool DBClient::initializeSchema() {
    if (!isConnected()) return false;
    
    try {
        pqxx::work txn(*m_conn);
        
        // Таблица measurements
        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS measurements (
                id SERIAL PRIMARY KEY,
                timestamp BIGINT,
                imei TEXT
            );
        )");
        
        // Таблица locations
        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS locations (
                id SERIAL PRIMARY KEY,
                measurement_id INT REFERENCES measurements(id) ON DELETE CASCADE,
                latitude DOUBLE PRECISION,
                longitude DOUBLE PRECISION,
                altitude DOUBLE PRECISION,
                accuracy DOUBLE PRECISION,
                speed DOUBLE PRECISION
            );
        )");
        
        // Таблица cells
        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS cells (
                id SERIAL PRIMARY KEY,
                measurement_id INT REFERENCES measurements(id) ON DELETE CASCADE,
                type TEXT,
                dbm INT,
                rsrp INT,
                pci INT,
                tac INT,
                mcc INT,
                mnc INT,
                ci BIGINT,
                earfcn INT
            );
        )");
        
        // Таблица traffic
        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS traffic (
                id SERIAL PRIMARY KEY,
                measurement_id INT REFERENCES measurements(id) ON DELETE CASCADE,
                mobile_rx BIGINT,
                mobile_tx BIGINT,
                total_rx BIGINT,
                total_tx BIGINT
            );
        )");
        
        // Индексы для производительности
        txn.exec("CREATE INDEX IF NOT EXISTS idx_locations_coords ON locations(latitude, longitude);");
        txn.exec("CREATE INDEX IF NOT EXISTS idx_cells_signal ON cells(dbm, rsrp);");
        txn.exec("CREATE INDEX IF NOT EXISTS idx_cells_pci ON cells(pci);");
        txn.exec("CREATE INDEX IF NOT EXISTS idx_measurements_timestamp ON measurements(timestamp);");
        txn.exec("CREATE INDEX IF NOT EXISTS idx_locations_measurement ON locations(measurement_id);");
        txn.exec("CREATE INDEX IF NOT EXISTS idx_cells_measurement ON cells(measurement_id);");
        
        txn.commit();
        std::cout << "Database schema initialized" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Schema initialization error: " << e.what() << std::endl;
        return false;
    }
}

// Парсинг cellInfo строки (полный аналог Python скрипта)
std::vector<json> DBClient::parseCellInfo(const std::string& cell_info_str) {
    std::vector<json> cells;
    
    // Паттерн для поиска CellIdentity{Type}:{...}:CellSignalStrength{Type}: {...}
    std::regex pattern(R"(CellIdentity(\w+):\{([^}]+)\}:CellSignalStrength\1:\s*\{([^}]+)\})");
    std::smatch match;
    std::string::const_iterator searchStart(cell_info_str.cbegin());
    
    while (std::regex_search(searchStart, cell_info_str.cend(), match, pattern)) {
        json cell;
        std::string cell_type = match[1].str();
        std::string identity = match[2].str();
        std::string signal = match[3].str();
        
        if (cell_type == "Gsm") {
            // Парсим GSM
            std::regex lac_re(R"(mLac=(\d+))");
            std::regex cid_re(R"(mCid=(\d+))");
            std::regex arfcn_re(R"(mArfcn=(\d+))");
            std::regex mcc_re(R"(mMcc=(\d+))");
            std::regex mnc_re(R"(mMnc=(\d+))");
            std::regex rssi_re(R"(rssi=(-?\d+))");
            
            std::smatch m;
            
            if (std::regex_search(identity, m, lac_re)) cell["tac"] = std::stoi(m[1].str());
            if (std::regex_search(identity, m, cid_re)) cell["ci"] = std::stoll(m[1].str());
            if (std::regex_search(identity, m, arfcn_re)) cell["earfcn"] = std::stoi(m[1].str());
            if (std::regex_search(identity, m, mcc_re)) cell["mcc"] = std::stoi(m[1].str());
            if (std::regex_search(identity, m, mnc_re)) cell["mnc"] = std::stoi(m[1].str());
            if (std::regex_search(signal, m, rssi_re)) cell["dbm"] = std::stoi(m[1].str());
            
            cell["type"] = "GSM";
            cell["pci"] = 0;
            
        } else if (cell_type == "Lte") {
            // Парсим LTE
            std::regex pci_re(R"(mPci=(\d+))");
            std::regex tac_re(R"(mTac=(\d+))");
            std::regex ci_re(R"(mCi=(\d+))");
            std::regex earfcn_re(R"(mEarfcn=(\d+))");
            std::regex mcc_re(R"(mMcc=(\d+))");
            std::regex mnc_re(R"(mMnc=(\d+))");
            std::regex rsrp_re(R"(rsrp=(-?\d+))");
            std::regex rsrq_re(R"(rsrq=(-?\d+))");
            std::regex rssnr_re(R"(rssnr=(-?\d+))");
            
            std::smatch m;
            
            if (std::regex_search(identity, m, pci_re)) cell["pci"] = std::stoi(m[1].str());
            if (std::regex_search(identity, m, tac_re)) cell["tac"] = std::stoi(m[1].str());
            if (std::regex_search(identity, m, ci_re)) cell["ci"] = std::stoll(m[1].str());
            if (std::regex_search(identity, m, earfcn_re)) cell["earfcn"] = std::stoi(m[1].str());
            if (std::regex_search(identity, m, mcc_re)) cell["mcc"] = std::stoi(m[1].str());
            if (std::regex_search(identity, m, mnc_re)) cell["mnc"] = std::stoi(m[1].str());
            if (std::regex_search(signal, m, rsrp_re)) cell["rsrp"] = std::stoi(m[1].str());
            if (std::regex_search(signal, m, rsrq_re)) cell["rsrq"] = std::stoi(m[1].str());
            
            cell["type"] = "LTE";
            cell["dbm"] = cell.value("rsrp", -120);
            
        } else if (cell_type == "Wcdma") {
            // Парсим WCDMA
            std::regex psc_re(R"(mPsc=(\d+))");
            std::regex uarfcn_re(R"(mUarfcn=(\d+))");
            std::regex mcc_re(R"(mMcc=(\d+))");
            std::regex mnc_re(R"(mMnc=(\d+))");
            std::regex dbm_re(R"(dbm=(-?\d+))");
            
            std::smatch m;
            
            if (std::regex_search(identity, m, psc_re)) cell["pci"] = std::stoi(m[1].str());
            if (std::regex_search(identity, m, uarfcn_re)) cell["earfcn"] = std::stoi(m[1].str());
            if (std::regex_search(identity, m, mcc_re)) cell["mcc"] = std::stoi(m[1].str());
            if (std::regex_search(identity, m, mnc_re)) cell["mnc"] = std::stoi(m[1].str());
            if (std::regex_search(signal, m, dbm_re)) cell["dbm"] = std::stoi(m[1].str());
            
            cell["type"] = "WCDMA";
            cell["rsrp"] = cell.value("dbm", -120);
        }
        
        if (!cell.empty()) {
            cells.push_back(cell);
        }
        
        searchStart = match.suffix().first;
    }
    
    return cells;
}

long long DBClient::insertMeasurement(long long timestamp, const std::string& imei) {
    pqxx::work txn(*m_conn);
    pqxx::result res = txn.exec_params(
        "INSERT INTO measurements (timestamp, imei) VALUES ($1, $2) RETURNING id",
        timestamp, imei
    );
    long long id = res[0][0].as<long long>();
    txn.commit();
    return id;
}

void DBClient::insertLocation(long long measurement_id, const json& loc) {
    if (!loc.contains("latitude") || loc["latitude"].is_null()) return;
    
    pqxx::work txn(*m_conn);
    txn.exec_params(
        "INSERT INTO locations (measurement_id, latitude, longitude, altitude, accuracy, speed) "
        "VALUES ($1, $2, $3, $4, $5, $6)",
        measurement_id,
        loc.value("latitude", 0.0),
        loc.value("longitude", 0.0),
        loc.value("altitude", 0.0),
        loc.value("accuracy", 0.0f),
        loc.value("speed", 0.0f)
    );
    txn.commit();
}

void DBClient::insertCells(long long measurement_id, const std::vector<json>& cells) {
    if (cells.empty()) return;
    
    pqxx::work txn(*m_conn);
    for (const auto& cell : cells) {
        txn.exec_params(
            "INSERT INTO cells (measurement_id, type, dbm, rsrp, pci, tac, mcc, mnc, ci, earfcn) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)",
            measurement_id,
            cell.value("type", ""),
            cell.value("dbm", -120),
            cell.value("rsrp", -120),
            cell.value("pci", 0),
            cell.value("tac", 0),
            cell.value("mcc", 0),
            cell.value("mnc", 0),
            cell.value("ci", 0LL),
            cell.value("earfcn", 0)
        );
    }
    txn.commit();
}

void DBClient::insertTraffic(long long measurement_id, const json& traffic) {
    if (traffic.empty()) return;
    
    pqxx::work txn(*m_conn);
    txn.exec_params(
        "INSERT INTO traffic (measurement_id, mobile_rx, mobile_tx, total_rx, total_tx) "
        "VALUES ($1, $2, $3, $4, $5)",
        measurement_id,
        traffic.value("mobile_rx_bytes", 0LL),
        traffic.value("mobile_tx_bytes", 0LL),
        traffic.value("total_rx_bytes", 0LL),
        traffic.value("total_tx_bytes", 0LL)
    );
    txn.commit();
}

bool DBClient::importJsonData(const json& data) {
    if (!isConnected()) return false;
    
    try {
        // Если data - массив, импортируем каждый элемент
        if (data.is_array()) {
            for (const auto& item : data) {
                importJsonData(item);
            }
            return true;
        }
        
        // Импорт одного измерения
        long long timestamp = data.value("timestamp", 0LL);
        std::string imei = data.value("imei", "");
        
        long long measurement_id = insertMeasurement(timestamp, imei);
        
        // Location
        if (data.contains("location") && data["location"].is_object()) {
            insertLocation(measurement_id, data["location"]);
        } else {
            // Альтернативный формат (поля прямо в корне)
            json alt_loc;
            if (data.contains("latitude")) alt_loc["latitude"] = data["latitude"];
            if (data.contains("longitude")) alt_loc["longitude"] = data["longitude"];
            if (data.contains("altitude")) alt_loc["altitude"] = data["altitude"];
            if (data.contains("accuracy")) alt_loc["accuracy"] = data["accuracy"];
            if (data.contains("speed")) alt_loc["speed"] = data["speed"];
            if (!alt_loc.empty()) {
                insertLocation(measurement_id, alt_loc);
            }
        }
        
        // Cells from telephony
        std::vector<json> all_cells;
        
        if (data.contains("telephony") && data["telephony"].is_object()) {
            for (auto& [key, cell] : data["telephony"].items()) {
                if (cell.is_object()) {
                    json cell_data = cell;
                    if (!cell_data.contains("type")) {
                        // Определяем тип по ключу или по наличию полей
                        if (cell_data.contains("pci") && cell_data.contains("rsrp")) {
                            cell_data["type"] = "LTE";
                        } else if (cell_data.contains("dbm") && !cell_data.contains("rsrp")) {
                            cell_data["type"] = "GSM";
                        } else {
                            cell_data["type"] = "Unknown";
                        }
                    }
                    all_cells.push_back(cell_data);
                }
            }
        }
        
        // Cells from cellInfo
        if (data.contains("cellInfo") && data["cellInfo"].is_string()) {
            std::string cell_info_str = data["cellInfo"];
            auto parsed_cells = parseCellInfo(cell_info_str);
            all_cells.insert(all_cells.end(), parsed_cells.begin(), parsed_cells.end());
        }
        
        insertCells(measurement_id, all_cells);
        
        // Traffic
        if (data.contains("traffic") && data["traffic"].is_object()) {
            insertTraffic(measurement_id, data["traffic"]);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error importing JSON data: " << e.what() << std::endl;
        return false;
    }
}

bool DBClient::importJsonFile(const std::string& json_path) {
    if (!isConnected()) return false;
    
    std::cout << "Importing: " << json_path << std::endl;
    
    try {
        std::ifstream file(json_path);
        if (!file.is_open()) {
            std::cerr << "Cannot open file: " << json_path << std::endl;
            return false;
        }
        
        json data;
        file >> data;
        file.close();
        
        bool result = importJsonData(data);
        if (result) {
            std::cout << "Successfully imported: " << json_path << std::endl;
        }
        return result;
        
    } catch (const std::exception& e) {
        std::cerr << "Error reading JSON file " << json_path << ": " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::string> DBClient::findJsonFiles(const std::string& directory) {
    std::vector<std::string> json_files;
    
    try {
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                json_files.push_back(entry.path().string());
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning directory: " << e.what() << std::endl;
    }
    
    return json_files;
}

bool DBClient::importJsonDirectory(const std::string& directory_path) {
    if (!isConnected()) return false;
    
    auto json_files = findJsonFiles(directory_path);
    
    if (json_files.empty()) {
        std::cout << "No JSON files found in: " << directory_path << std::endl;
        return false;
    }
    
    std::cout << "Found " << json_files.size() << " JSON files" << std::endl;
    
    int success_count = 0;
    for (const auto& file : json_files) {
        if (importJsonFile(file)) {
            success_count++;
        }
    }
    
    std::cout << "Imported " << success_count << "/" << json_files.size() << " files" << std::endl;
    return success_count > 0;
}

// Загрузка данных для GUI
std::vector<MapPoint> DBClient::loadPoints(int limit) {
    std::vector<MapPoint> points;
    if (!isConnected()) return points;
    
    try {
        pqxx::work txn(*m_conn);
        std::string query = 
            "SELECT l.latitude, l.longitude, m.timestamp, COALESCE(c.rsrp, c.dbm, -120) as signal "
            "FROM locations l "
            "JOIN measurements m ON l.measurement_id = m.id "
            "LEFT JOIN cells c ON m.id = c.measurement_id "
            "WHERE l.latitude IS NOT NULL AND l.longitude IS NOT NULL "
            "ORDER BY l.id DESC LIMIT " + std::to_string(limit);
        
        pqxx::result res = txn.exec(query);
        for (const auto& row : res) {
            MapPoint p;
            p.lat = row[0].as<double>();
            p.lon = row[1].as<double>();
            p.timestamp = row[2].as<long long>();
            p.signal_strength = row[3].as<int>();
            p.type = "GPS";
            points.push_back(p);
        }
        txn.commit();
        std::cout << "Loaded " << points.size() << " points from database" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "DB error (loadPoints): " << e.what() << std::endl;
    }
    return points;
}

std::vector<MapPoint> DBClient::loadPointsInArea(double min_lat, double max_lat, 
                                                  double min_lon, double max_lon, int limit) {
    std::vector<MapPoint> points;
    if (!isConnected()) return points;
    
    try {
        pqxx::work txn(*m_conn);
        pqxx::result res = txn.exec_params(
            "SELECT l.latitude, l.longitude, m.timestamp, COALESCE(c.rsrp, c.dbm, -120) "
            "FROM locations l "
            "JOIN measurements m ON l.measurement_id = m.id "
            "LEFT JOIN cells c ON m.id = c.measurement_id "
            "WHERE l.latitude BETWEEN $1 AND $2 "
            "AND l.longitude BETWEEN $3 AND $4 "
            "ORDER BY l.id DESC LIMIT $5",
            min_lat, max_lat, min_lon, max_lon, limit
        );
        
        for (const auto& row : res) {
            MapPoint p;
            p.lat = row[0].as<double>();
            p.lon = row[1].as<double>();
            p.timestamp = row[2].as<long long>();
            p.signal_strength = row[3].as<int>();
            p.type = "GPS";
            points.push_back(p);
        }
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "DB error (loadPointsInArea): " << e.what() << std::endl;
    }
    return points;
}

std::vector<CellData> DBClient::loadCells(int limit) {
    std::vector<CellData> cells;
    if (!isConnected()) return cells;
    
    try {
        pqxx::work txn(*m_conn);
        pqxx::result res = txn.exec(
            "SELECT c.pci, c.rsrp, c.rsrq, c.tac, c.mcc, c.mnc, c.ci, c.earfcn, c.type, c.dbm, m.timestamp "
            "FROM cells c "
            "JOIN measurements m ON c.measurement_id = m.id "
            "WHERE c.pci IS NOT NULL AND c.pci > 0 "
            "ORDER BY c.id DESC LIMIT " + std::to_string(limit)
        );
        
        for (const auto& row : res) {
            CellData cell;
            cell.pci = row[0].as<int>();
            cell.rsrp = row[1].as<int>();
            cell.rsrq = row[2].is_null() ? 0 : row[2].as<int>();
            cell.tac = row[3].is_null() ? 0 : row[3].as<int>();
            cell.mcc = row[4].is_null() ? 0 : row[4].as<int>();
            cell.mnc = row[5].is_null() ? 0 : row[5].as<int>();
            cell.ci = row[6].is_null() ? 0 : row[6].as<long long>();
            cell.earfcn = row[7].is_null() ? 0 : row[7].as<int>();
            cell.type = row[8].is_null() ? "Unknown" : row[8].as<std::string>();
            cell.dbm = row[9].is_null() ? -120 : row[9].as<int>();
            cell.timestamp = row[10].as<long long>();
            cells.push_back(cell);
        }
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "DB error (loadCells): " << e.what() << std::endl;
    }
    return cells;
}

std::vector<TrafficData> DBClient::loadTraffic(int limit) {
    std::vector<TrafficData> traffic;
    if (!isConnected()) return traffic;
    
    try {
        pqxx::work txn(*m_conn);
        pqxx::result res = txn.exec(
            "SELECT t.mobile_rx, t.mobile_tx, t.total_rx, t.total_tx, m.timestamp "
            "FROM traffic t "
            "JOIN measurements m ON t.measurement_id = m.id "
            "ORDER BY t.id DESC LIMIT " + std::to_string(limit)
        );
        
        for (const auto& row : res) {
            TrafficData td;
            td.mobile_rx_bytes = row[0].as<long long>();
            td.mobile_tx_bytes = row[1].as<long long>();
            td.total_rx_bytes = row[2].as<long long>();
            td.total_tx_bytes = row[3].as<long long>();
            td.timestamp = row[4].as<long long>();
            traffic.push_back(td);
        }
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "DB error (loadTraffic): " << e.what() << std::endl;
    }
    return traffic;
}

std::vector<LocationData> DBClient::loadLocations(int limit) {
    std::vector<LocationData> locations;
    if (!isConnected()) return locations;
    
    try {
        pqxx::work txn(*m_conn);
        pqxx::result res = txn.exec(
            "SELECT l.latitude, l.longitude, COALESCE(l.altitude, 0), "
            "COALESCE(l.accuracy, 0), COALESCE(l.speed, 0), m.timestamp "
            "FROM locations l "
            "JOIN measurements m ON l.measurement_id = m.id "
            "WHERE l.latitude IS NOT NULL "
            "ORDER BY l.id DESC LIMIT " + std::to_string(limit)
        );
        
        for (const auto& row : res) {
            LocationData ld;
            ld.latitude = row[0].as<double>();
            ld.longitude = row[1].as<double>();
            ld.altitude = row[2].as<double>();
            ld.accuracy = row[3].as<float>();
            ld.speed = row[4].as<float>();
            ld.timestamp = row[5].as<long long>();
            locations.push_back(ld);
        }
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "DB error (loadLocations): " << e.what() << std::endl;
    }
    return locations;
}

int DBClient::getMeasurementCount() {
    if (!isConnected()) return 0;
    try {
        pqxx::work txn(*m_conn);
        pqxx::result res = txn.exec("SELECT COUNT(*) FROM measurements");
        int count = res[0][0].as<int>();
        txn.commit();
        return count;
    } catch (...) {
        return 0;
    }
}

int DBClient::getCellCount() {
    if (!isConnected()) return 0;
    try {
        pqxx::work txn(*m_conn);
        pqxx::result res = txn.exec("SELECT COUNT(*) FROM cells");
        int count = res[0][0].as<int>();
        txn.commit();
        return count;
    } catch (...) {
        return 0;
    }
}

int DBClient::getLocationCount() {
    if (!isConnected()) return 0;
    try {
        pqxx::work txn(*m_conn);
        pqxx::result res = txn.exec("SELECT COUNT(*) FROM locations");
        int count = res[0][0].as<int>();
        txn.commit();
        return count;
    } catch (...) {
        return 0;
    }
}

int DBClient::getTrafficCount() {
    if (!isConnected()) return 0;
    try {
        pqxx::work txn(*m_conn);
        pqxx::result res = txn.exec("SELECT COUNT(*) FROM traffic");
        int count = res[0][0].as<int>();
        txn.commit();
        return count;
    } catch (...) {
        return 0;
    }
}

bool DBClient::clearAllData() {
    if (!isConnected()) return false;
    try {
        pqxx::work txn(*m_conn);
        txn.exec("TRUNCATE TABLE traffic, cells, locations, measurements RESTART IDENTITY CASCADE");
        txn.commit();
        std::cout << "All data cleared" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error clearing data: " << e.what() << std::endl;
        return false;
    }
}

bool DBClient::clearOldData(int days_to_keep) {
    if (!isConnected()) return false;
    try {
        long long cutoff_time = std::time(nullptr) * 1000LL - days_to_keep * 24LL * 3600LL * 1000LL;
        
        pqxx::work txn(*m_conn);
        txn.exec_params(
            "DELETE FROM measurements WHERE timestamp < $1",
            cutoff_time
        );
        txn.commit();
        std::cout << "Cleared data older than " << days_to_keep << " days" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error clearing old data: " << e.what() << std::endl;
        return false;
    }
}

std::vector<CellData> DBClient::loadCellsByPci(int pci, int limit) {
    std::vector<CellData> cells;
    if (!isConnected()) return cells;
    
    try {
        pqxx::work txn(*m_conn);
        pqxx::result res = txn.exec_params(
            "SELECT c.pci, c.rsrp, c.rsrq, c.tac, c.mcc, c.mnc, c.ci, c.earfcn, c.type, c.dbm, m.timestamp "
            "FROM cells c "
            "JOIN measurements m ON c.measurement_id = m.id "
            "WHERE c.pci = $1 "
            "ORDER BY m.timestamp DESC LIMIT $2",
            pci, limit
        );
        
        for (const auto& row : res) {
            CellData cell;
            cell.pci = row[0].as<int>();
            cell.rsrp = row[1].as<int>();
            cell.rsrq = row[2].is_null() ? 0 : row[2].as<int>();
            cell.tac = row[3].is_null() ? 0 : row[3].as<int>();
            cell.mcc = row[4].is_null() ? 0 : row[4].as<int>();
            cell.mnc = row[5].is_null() ? 0 : row[5].as<int>();
            cell.ci = row[6].is_null() ? 0 : row[6].as<long long>();
            cell.earfcn = row[7].is_null() ? 0 : row[7].as<int>();
            cell.type = row[8].is_null() ? "Unknown" : row[8].as<std::string>();
            cell.dbm = row[9].is_null() ? -120 : row[9].as<int>();
            cell.timestamp = row[10].as<long long>();
            cells.push_back(cell);
        }
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "DB error (loadCellsByPci): " << e.what() << std::endl;
    }
    return cells;
}