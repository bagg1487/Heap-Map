#ifndef HEATMAP_HPP
#define HEATMAP_HPP

#include <vector>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct DataPoint {
    double lat;
    double lon;
    int rsrp;
    int dbm;
    long long timestamp;
    std::string type;
    bool has_location;
    bool has_signal;
};

struct MapPoint {
    double lat;
    double lon;
    int signal_strength;
    long long timestamp;
    std::string type;
};

void generate_python_heatmap_script(const std::vector<DataPoint>& points, const std::string& output_file);
void generate_signal_graphs(const std::vector<DataPoint>& points, const std::string& output_dir);
void generate_traffic_graphs(const std::string& output_dir);
void run_heatmap_generator();
void draw_minimap(const std::vector<MapPoint>& points, int point_size);
void load_points_from_json(std::vector<MapPoint>& map_points);

#endif