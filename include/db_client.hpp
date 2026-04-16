#pragma once
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct MapPoint {
    double lat;
    double lon;
    long long timestamp;
    int signal_strength;
    std::string type;
};

struct CellData {
    int pci;
    int rsrp;
    int rsrq;
    int tac;
    int mcc;
    int mnc;
    long long ci;
    int earfcn;
    std::string type;
    int dbm;
    long long timestamp;
};

struct TrafficData {
    long long mobile_rx_bytes;
    long long mobile_tx_bytes;
    long long total_rx_bytes;
    long long total_tx_bytes;
    long long timestamp;
};

struct LocationData {
    double latitude;
    double longitude;
    double altitude;
    float accuracy;
    float speed;
    long long timestamp;
};

class DBClient {
public:
    DBClient(const std::string& conn_string);
    ~DBClient();
    
    bool isConnected() const;
    
    bool initializeSchema();
    
    bool importJsonFile(const std::string& json_path);
    bool importJsonDirectory(const std::string& directory_path);
    bool importJsonData(const json& data);
    
    std::vector<MapPoint> loadPoints(int limit = 10000);
    std::vector<MapPoint> loadPointsInArea(double min_lat, double max_lat, 
                                           double min_lon, double max_lon, int limit = 10000);
    std::vector<CellData> loadCells(int limit = 2000);
    std::vector<CellData> loadCellsByPci(int pci, int limit = 100);
    std::vector<TrafficData> loadTraffic(int limit = 2000);
    std::vector<LocationData> loadLocations(int limit = 2000);
    
    int getMeasurementCount();
    int getCellCount();
    int getLocationCount();
    int getTrafficCount();
    
    bool clearAllData();
    bool clearOldData(int days_to_keep = 30);
    
private:
    std::vector<json> parseCellInfo(const std::string& cell_info_str);
    
    long long insertMeasurement(long long timestamp, const std::string& imei);
    void insertLocation(long long measurement_id, const json& loc);
    void insertCells(long long measurement_id, const std::vector<json>& cells);
    void insertTraffic(long long measurement_id, const json& traffic);
    
    std::vector<std::string> findJsonFiles(const std::string& directory);
    
    std::unique_ptr<pqxx::connection> m_conn;
};