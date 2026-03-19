#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct MapPoint {
    double lat;
    double lon;
    float rsrp;
    long long timestamp;
    std::string type;
};

void load_points_from_json(std::vector<MapPoint>& map_points);
void generate_html_map(const std::vector<MapPoint>& points, const std::string& filename);