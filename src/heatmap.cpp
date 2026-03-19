#include "heatmap.hpp"
#include "../third-party/imgui/imgui.h"
#include <fstream>
#include <iostream>
#include <ctime>
#include <regex>
#include <iomanip>

using namespace std;
using json = nlohmann::json;

string formatTime(long long ts) {
    if (ts == 0) return "N/A";
    
    if (ts > 1000000000000LL) {
        time_t t = ts / 1000;
        char buf[80];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
        return string(buf);
    } else {
        time_t t = ts;
        char buf[80];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
        return string(buf);
    }
}

long long parseTimeString(const string& time_str) {
    tm tm = {};
    stringstream ss(time_str);
    ss >> get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) return 0;
    time_t t = mktime(&tm);
    return t * 1000LL;
}

void load_points_from_json(vector<MapPoint>& map_points) {
    map_points.clear();
    
    ifstream file("data/locations.json");
    if (!file.is_open()) {
        ifstream old_file("location_data.txt");
        if (!old_file.is_open()) return;
        
        string line;
        MapPoint current_point;
        bool in_record = false;
        
        while (getline(old_file, line)) {
            if (line.find("Record #") != string::npos) {
                if (in_record && current_point.lat != 0) {
                    map_points.push_back(current_point);
                }
                current_point = MapPoint();
                current_point.rsrp = -100;
                current_point.type = "unknown";
                in_record = true;
            }
            else if (line.find("Latitude:") != string::npos) {
                regex lat_regex("Latitude:\\s*([0-9\\.]+)");
                smatch match;
                if (regex_search(line, match, lat_regex)) {
                    current_point.lat = stod(match[1]);
                }
            }
            else if (line.find("Longitude:") != string::npos) {
                regex lon_regex("Longitude:\\s*([0-9\\.]+)");
                smatch match;
                if (regex_search(line, match, lon_regex)) {
                    current_point.lon = stod(match[1]);
                }
            }
            else if (line.find("Time:") != string::npos && line.find("Timestamp:") == string::npos) {
                if (line.find("2026-") != string::npos) {
                    regex time_regex("Time:\\s*([0-9-]+ [0-9:]+)");
                    smatch match;
                    if (regex_search(line, match, time_regex)) {
                        current_point.timestamp = parseTimeString(match[1]);
                    }
                } else {
                    regex ts_regex("Time:\\s*([0-9]+)");
                    smatch match;
                    if (regex_search(line, match, ts_regex)) {
                        current_point.timestamp = stoll(match[1]);
                    }
                }
            }
            else if (line.find("Timestamp:") != string::npos) {
                regex ts_regex("Timestamp:\\s*([0-9]+)");
                smatch match;
                if (regex_search(line, match, ts_regex)) {
                    current_point.timestamp = stoll(match[1]);
                }
            }
            else if (line.find("---") != string::npos) {
                if (in_record && current_point.lat != 0) {
                    map_points.push_back(current_point);
                }
                in_record = false;
            }
        }
        
        if (in_record && current_point.lat != 0) {
            map_points.push_back(current_point);
        }
        
        cout << "Loaded " << map_points.size() << " points from text file" << endl;
        return;
    }
    
    try {
        json all_data = json::parse(file);
        
        if (all_data.is_array()) {
            for (const auto& item : all_data) {
                MapPoint point;
                point.rsrp = -100;
                point.type = "unknown";
                
                if (item.contains("location")) {
                    point.lat = item["location"].value("latitude", 0.0);
                    point.lon = item["location"].value("longitude", 0.0);
                } else if (item.contains("latitude") && item.contains("longitude")) {
                    point.lat = item.value("latitude", 0.0);
                    point.lon = item.value("longitude", 0.0);
                } else {
                    continue;
                }
                
                if (item.contains("timestamp")) {
                    point.timestamp = item.value("timestamp", 0LL);
                } else if (item.contains("time")) {
                    if (item["time"].is_number()) {
                        point.timestamp = item.value("time", 0LL);
                    }
                }
                
                map_points.push_back(point);
            }
        }
        
        cout << "Loaded " << map_points.size() << " points from JSON" << endl;
    } catch (const exception& e) {
        cerr << "Error parsing JSON: " << e.what() << endl;
    }
}

void draw_minimap(const vector<MapPoint>& points, int point_size) {
    if (points.empty()) {
        ImGui::TextColored(ImVec4(1,1,0,1), "No map points loaded");
        return;
    }
    
    double min_lat = 90, max_lat = -90, min_lon = 180, max_lon = -180;
    for (const auto& p : points) {
        min_lat = min(min_lat, p.lat);
        max_lat = max(max_lat, p.lat);
        min_lon = min(min_lon, p.lon);
        max_lon = max(max_lon, p.lon);
    }
    
    double lat_pad = (max_lat - min_lat) * 0.1;
    double lon_pad = (max_lon - min_lon) * 0.1;
    if (lat_pad < 0.0001) lat_pad = 0.001;
    if (lon_pad < 0.0001) lon_pad = 0.001;
    
    min_lat -= lat_pad;
    max_lat += lat_pad;
    min_lon -= lon_pad;
    max_lon += lon_pad;
    
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.y = 500;
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    
    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(30, 30, 40, 255));
    draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(100, 100, 100, 255));
    
    auto transform = [&](double lat, double lon) -> ImVec2 {
        double x = (lon - min_lon) / (max_lon - min_lon) * canvas_size.x;
        double y = (1.0 - (lat - min_lat) / (max_lat - min_lat)) * canvas_size.y;
        return ImVec2(canvas_pos.x + x, canvas_pos.y + y);
    };
    
    for (int i = 0; i <= 4; i++) {
        double lat = min_lat + (max_lat - min_lat) * i / 4;
        double lon = min_lon + (max_lon - min_lon) * i / 4;
        
        ImVec2 p_lat1 = transform(lat, min_lon);
        ImVec2 p_lat2 = transform(lat, max_lon);
        draw_list->AddLine(p_lat1, p_lat2, IM_COL32(60, 60, 70, 255));
        
        ImVec2 p_lon1 = transform(min_lat, lon);
        ImVec2 p_lon2 = transform(max_lat, lon);
        draw_list->AddLine(p_lon1, p_lon2, IM_COL32(60, 60, 70, 255));
    }
    
    for (const auto& p : points) {
        ImVec2 pos = transform(p.lat, p.lon);
        
        ImU32 color = IM_COL32(255, 0, 0, 255);
        
        draw_list->AddCircleFilled(pos, point_size, color);
        
        float hover_size = point_size + 2;
        ImVec2 min_rect = ImVec2(pos.x - hover_size, pos.y - hover_size);
        ImVec2 max_rect = ImVec2(pos.x + hover_size, pos.y + hover_size);
        
        if (ImGui::IsMouseHoveringRect(min_rect, max_rect)) {
            ImGui::BeginTooltip();
            ImGui::Text("Lat: %.6f", p.lat);
            ImGui::Text("Lon: %.6f", p.lon);
            ImGui::Text("Time: %s", formatTime(p.timestamp).c_str());
            ImGui::EndTooltip();
        }
    }
    
    ImGui::Dummy(ImVec2(canvas_size.x, canvas_size.y + 20));
}

void generate_html_map(const vector<MapPoint>& points, const string& filename) {
    ofstream file(filename);
    
    file << "<!DOCTYPE html>\n";
    file << "<html>\n";
    file << "<head>\n";
    file << "    <title>Heatmap</title>\n";
    file << "    <link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css' />\n";
    file << "    <script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'></script>\n";
    file << "    <style>\n";
    file << "        #map { height: 100vh; }\n";
    file << "    </style>\n";
    file << "</head>\n";
    file << "<body>\n";
    file << "    <div id='map'></div>\n";
    file << "    <script>\n";
    
    double center_lat = 0, center_lon = 0;
    if (!points.empty()) {
        for (const auto& p : points) {
            center_lat += p.lat;
            center_lon += p.lon;
        }
        center_lat /= points.size();
        center_lon /= points.size();
    } else {
        center_lat = 55.0132;
        center_lon = 82.9507;
    }
    
    file << "        var map = L.map('map').setView([" << center_lat << ", " << center_lon << "], 15);\n";
    file << "        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {\n";
    file << "            attribution: '&copy; <a href=\"https://www.openstreetmap.org/copyright\">OpenStreetMap</a>'\n";
    file << "        }).addTo(map);\n";
    
    file << "        var markers = [\n";
    for (const auto& p : points) {
        file << "            [" << p.lat << ", " << p.lon << "],\n";
    }
    file << "        ];\n";
    
    file << "        markers.forEach(function(m) {\n";
    file << "            L.marker(m).addTo(map);\n";
    file << "        });\n";
    
    file << "    </script>\n";
    file << "</body>\n";
    file << "</html>\n";
    
    file.close();
    cout << "HTML map generated: " << filename << endl;
}