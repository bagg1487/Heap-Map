#include "heatmap.hpp"
#include "tile_manager.hpp"
#include "imgui.h"
#include <cmath>
#include <vector>

static TileManager tileManager;
static std::vector<MapPoint> current_points;

static const double PI = 3.141592653589793;

static double lon_to_x(double lon, int z) {
    return (lon + 180.0) / 360.0 * (1 << z);
}

static double lat_to_y(double lat, int z) {
    double r = lat * PI / 180.0;
    return (1.0 - std::log(std::tan(PI / 4.0 + r / 2.0)) / PI)
           / 2.0 * (1 << z);
}

static double x_to_lon(double x, int z) {
    return x / (double)(1 << z) * 360.0 - 180.0;
}

static double y_to_lat(double y, int z) {
    double n = PI - 2.0 * PI * y / (1 << z);
    return 180.0 / PI * std::atan(std::sinh(n));
}

static int zoom = 12;
static double center_lon = 82.9445;
static double center_lat = 55.0079;

void init_heatmap() {
    current_points.reserve(10000);
}

void update_map_points(const std::vector<MapPoint>& points) {
    current_points = points;
}

void set_map_center(double lat, double lon, int z) {
    center_lat = lat;
    center_lon = lon;
    zoom = z;
}

void draw_heatmap(ImDrawList* dl, ImVec2 pos, ImVec2 size) {
    dl->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
    tileManager.updateGL();

    int tiles_x = (int)std::ceil(size.x / 256.0) + 2; 
    int tiles_y = (int)std::ceil(size.y / 256.0) + 2;

    double cx = lon_to_x(center_lon, zoom);
    double cy = lat_to_y(center_lat, zoom);

    double start_tile_x = cx - (size.x / 512.0); 
    double start_tile_y = cy - (size.y / 512.0);

    float tile_w = 256.0f; 
    float tile_h = 256.0f;

    float off_x = (float)(std::floor(start_tile_x) - start_tile_x) * tile_w;
    float off_y = (float)(std::floor(start_tile_y) - start_tile_y) * tile_h;

    for (int x = 0; x < tiles_x; x++) {
        for (int y = 0; y < tiles_y; y++) {
            int tx = (int)std::floor(start_tile_x) + x;
            int ty = (int)std::floor(start_tile_y) + y;

            Tile* t = tileManager.get(zoom, tx, ty);
            
            float x1 = pos.x + x * tile_w + off_x;
            float y1 = pos.y + y * tile_h + off_y;
            float x2 = x1 + tile_w;
            float y2 = y1 + tile_h;

            if (t && t->ready && t->tex) {
                dl->AddImage((void*)(intptr_t)t->tex, ImVec2(x1, y1), ImVec2(x2, y2));
            } else {
                dl->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(30, 30, 30, 255));
            }
        }
    }

    for (const auto& p : current_points) {
        double px = lon_to_x(p.lon, zoom);
        double py = lat_to_y(p.lat, zoom);

        float screen_x = pos.x + (float)(px - start_tile_x) * tile_w;
        float screen_y = pos.y + (float)(py - start_tile_y) * tile_h;

        if (screen_x >= pos.x && screen_x <= pos.x + size.x &&
            screen_y >= pos.y && screen_y <= pos.y + size.y) 
        {
            dl->AddCircleFilled(ImVec2(screen_x, screen_y), 4.0f, IM_COL32(0, 255, 0, 200));
        }
    }

    dl->PopClipRect();
}

void draw_heatmap_ui() {
    ImGui::Text("Zoom: %d", zoom);
    ImGui::Text("Lat: %.5f, Lon: %.5f", center_lat, center_lon);
    ImGui::Text("Points: %zu", current_points.size());
}

void handle_map_input(ImVec2 pos, ImVec2 size) {
    if (!ImGui::IsItemHovered()) return;

    ImGuiIO& io = ImGui::GetIO();

    if (io.MouseWheel != 0) {
        int old_zoom = zoom;
        zoom += (io.MouseWheel > 0 ? 1 : -1);
        if (zoom < 1) zoom = 1;
        if (zoom > 18) zoom = 18;
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = io.MouseDelta;

        double tile_px_w = size.x / 6.0;
        double tile_px_h = size.y / 4.0;

        double dx = delta.x / tile_px_w;
        double dy = delta.y / tile_px_h;

        double cx = lon_to_x(center_lon, zoom);
        double cy = lat_to_y(center_lat, zoom);

        center_lon = x_to_lon(cx - dx, zoom);
        center_lat = y_to_lat(cy - dy, zoom);
    }
}