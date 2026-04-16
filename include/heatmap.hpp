#pragma once
#include <vector>
#include "imgui.h"

struct MapPoint {
    double lat;
    double lon;
    int signal_strength;
};

void init_heatmap();
void update_map_points(const std::vector<MapPoint>& points);
void set_map_center(double lat, double lon, int zoom);

void draw_heatmap(ImDrawList*, ImVec2, ImVec2);
void draw_heatmap_ui();
void handle_map_input(ImVec2, ImVec2);