#include "server.hpp"
#include "heatmap.hpp"
#include "../third-party/imgui/imgui.h"
#include "../third-party/imgui/backends/imgui_impl_glfw.h"
#include "../third-party/imgui/backends/imgui_impl_opengl3.h"
#include "../third-party/implot/implot.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <deque>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <zmq.hpp>
#include <cmath>
#include <pqxx/pqxx>

using namespace std;
using json = nlohmann::json;

zmq::context_t* g_context = nullptr;
zmq::socket_t* g_command_socket = nullptr;
vector<MapPoint> map_points;

struct SignalHistory {
    deque<float> times;
    map<int, deque<float>> rsrp;
    map<int, deque<float>> rsrq;
    map<int, deque<float>> rssnr;
    int sample_count = 0;
    const int max_history = 200;
    
    void add_cell(int pci, float rsrp_val, float rsrq_val, float rssnr_val) {
        if (pci <= 0) return;
        
        if (rsrp[pci].size() >= max_history) rsrp[pci].pop_front();
        if (rsrq[pci].size() >= max_history) rsrq[pci].pop_front();
        if (rssnr[pci].size() >= max_history) rssnr[pci].pop_front();
        
        rsrp[pci].push_back(rsrp_val);
        rsrq[pci].push_back(rsrq_val);
        rssnr[pci].push_back(rssnr_val);
    }
    
    void add_sample() {
        if (times.size() >= max_history) {
            times.pop_front();
        }
        times.push_back(sample_count);
        sample_count++;
    }
    
    void clear() {
        times.clear();
        rsrp.clear();
        rsrq.clear();
        rssnr.clear();
        sample_count = 0;
    }
};

struct TrafficHistory {
    deque<float> times;
    deque<long long> rx_bytes;
    deque<long long> tx_bytes;
    int sample_count = 0;
    const int max_history = 200;
    
    void add(long long rx, long long tx) {
        if (rx_bytes.size() >= max_history) rx_bytes.pop_front();
        if (tx_bytes.size() >= max_history) tx_bytes.pop_front();
        
        rx_bytes.push_back(rx);
        tx_bytes.push_back(tx);
        
        if (times.size() >= max_history) {
            times.pop_front();
        }
        times.push_back(sample_count);
        sample_count++;
    }
    
    void clear() {
        times.clear();
        rx_bytes.clear();
        tx_bytes.clear();
        sample_count = 0;
    }
};

struct LocationHistory {
    deque<float> times;
    deque<double> latitudes;
    deque<double> longitudes;
    deque<double> altitudes;
    deque<float> accuracies;
    int sample_count = 0;
    const int max_history = 200;
    
    void add(double lat, double lon, double alt, float acc) {
        if (latitudes.size() >= max_history) latitudes.pop_front();
        if (longitudes.size() >= max_history) longitudes.pop_front();
        if (altitudes.size() >= max_history) altitudes.pop_front();
        if (accuracies.size() >= max_history) accuracies.pop_front();
        
        latitudes.push_back(lat);
        longitudes.push_back(lon);
        altitudes.push_back(alt);
        accuracies.push_back(acc);
        
        if (times.size() >= max_history) {
            times.pop_front();
        }
        times.push_back(sample_count);
        sample_count++;
    }
    
    void clear() {
        times.clear();
        latitudes.clear();
        longitudes.clear();
        altitudes.clear();
        accuracies.clear();
        sample_count = 0;
    }
};

string formatBytes(long long bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = bytes;
    while (size >= 1024 && unit < 4) { size /= 1024; unit++; }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f %s", size, units[unit]);
    return string(buf);
}

string formatTime(long long ts) {
    if (ts == 0) return "N/A";
    time_t t = ts / 1000;
    struct tm* tm_info = localtime(&t);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    return string(buffer);
}

void draw_minimap(const vector<MapPoint>& points, int point_size) {
    if (points.empty()) { ImGui::Text("No points to display"); return; }
    
    double min_lat = 90, max_lat = -90, min_lon = 180, max_lon = -180;
    for (const auto& p : points) {
        min_lat = min(min_lat, p.lat);
        max_lat = max(max_lat, p.lat);
        min_lon = min(min_lon, p.lon);
        max_lon = max(max_lon, p.lon);
    }
    
    if (min_lat == max_lat) { min_lat -= 0.001; max_lat += 0.001; }
    if (min_lon == max_lon) { min_lon -= 0.001; max_lon += 0.001; }
    
    float width = ImGui::GetContentRegionAvail().x;
    float height = width * 0.6f;
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    
    ImGui::InvisibleButton("minimap", ImVec2(width, height));
    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + width, canvas_pos.y + height), IM_COL32(30,30,40,255));
    draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + width, canvas_pos.y + height), IM_COL32(100,100,100,255));
    
    auto lat_to_y = [&](double lat) { return canvas_pos.y + height - (lat - min_lat) / (max_lat - min_lat) * height; };
    auto lon_to_x = [&](double lon) { return canvas_pos.x + (lon - min_lon) / (max_lon - min_lon) * width; };
    
    for (const auto& p : points) {
        float x = lon_to_x(p.lon);
        float y = lat_to_y(p.lat);
        ImU32 color = p.signal_strength >= -80 ? IM_COL32(0,255,0,255) :
                      p.signal_strength >= -90 ? IM_COL32(100,255,0,255) :
                      p.signal_strength >= -100 ? IM_COL32(255,255,0,255) :
                      p.signal_strength >= -110 ? IM_COL32(255,128,0,255) : IM_COL32(255,0,0,255);
        draw_list->AddCircleFilled(ImVec2(x,y), point_size, color);
        draw_list->AddCircle(ImVec2(x,y), point_size, IM_COL32(255,255,255,200), 0, 1.0f);
    }
}

void load_points_from_db(vector<MapPoint>& map_points, pqxx::connection& db_conn) {
    map_points.clear();
    
    try {
        if (!db_conn.is_open()) {
            cerr << "Database not connected" << endl;
            return;
        }
        
        pqxx::work txn(db_conn);
        
        pqxx::result res = txn.exec(
            "SELECT l.latitude, l.longitude, m.timestamp, c.rsrp, c.dbm, c.type "
            "FROM locations l "
            "JOIN measurements m ON l.measurement_id = m.id "
            "LEFT JOIN cells c ON l.measurement_id = c.measurement_id "
            "WHERE l.latitude IS NOT NULL AND l.longitude IS NOT NULL "
            "ORDER BY l.id DESC LIMIT 10000"
        );
        
        for (const auto& row : res) {
            MapPoint p;
            p.lat = row[0].as<double>();
            p.lon = row[1].as<double>();
            p.timestamp = row[2].as<long long>();
            
            if (!row[3].is_null()) p.signal_strength = row[3].as<int>();
            else if (!row[4].is_null()) p.signal_strength = row[4].as<int>();
            else p.signal_strength = -120;
            
            if (!row[5].is_null()) p.type = row[5].as<string>();
            else p.type = "Unknown";
            
            map_points.push_back(p);
        }
        
        txn.commit();
        cout << "Loaded " << map_points.size() << " points from database" << endl;
        
    } catch (const exception& e) {
        cerr << "Database error: " << e.what() << endl;
    }
}

void load_signal_history_from_db(SignalHistory& signal, pqxx::connection& db_conn) {
    try {
        if (!db_conn.is_open()) return;
        
        pqxx::work txn(db_conn);
        
        pqxx::result res = txn.exec(
            "SELECT c.pci, c.rsrp, m.timestamp "
            "FROM cells c "
            "JOIN measurements m ON c.measurement_id = m.id "
            "WHERE c.pci IS NOT NULL AND c.pci > 0 "
            "ORDER BY m.id ASC LIMIT 2000"
        );
        
        signal.clear();
        
        for (const auto& row : res) {
            int pci = row[0].as<int>();
            int rsrp = row[1].as<int>();
            
            int rsrq = -20;
            int rssnr = 0;
            
            if (rsrp > -80) rssnr = 20;
            else if (rsrp > -90) rssnr = 15;
            else if (rsrp > -100) rssnr = 10;
            else if (rsrp > -110) rssnr = 5;
            else rssnr = 0;
            
            if (rsrp > -80) rsrq = -8;
            else if (rsrp > -90) rsrq = -12;
            else if (rsrp > -100) rsrq = -15;
            else if (rsrp > -110) rsrq = -18;
            else rsrq = -20;
            
            signal.add_cell(pci, rsrp, rsrq, rssnr);
            signal.add_sample();
        }
        
        txn.commit();
        cout << "Loaded " << signal.rsrp.size() << " cells with " << signal.sample_count << " samples from database" << endl;
        
    } catch (const exception& e) {
        cerr << "Error loading signal history: " << e.what() << endl;
    }
}

void load_traffic_from_db(TrafficHistory& traffic, pqxx::connection& db_conn) {
    try {
        if (!db_conn.is_open()) return;
        
        pqxx::work txn(db_conn);
        
        pqxx::result res = txn.exec(
            "SELECT t.mobile_rx, t.mobile_tx, m.timestamp "
            "FROM traffic t "
            "JOIN measurements m ON t.measurement_id = m.id "
            "WHERE t.mobile_rx IS NOT NULL "
            "ORDER BY m.id ASC LIMIT 2000"
        );
        
        traffic.clear();
        
        for (const auto& row : res) {
            long long rx = row[0].as<long long>();
            long long tx = row[1].as<long long>();
            traffic.add(rx, tx);
        }
        
        txn.commit();
        cout << "Loaded " << traffic.sample_count << " traffic samples from database" << endl;
        
    } catch (const exception& e) {
        cerr << "Error loading traffic history: " << e.what() << endl;
    }
}

void load_locations_from_db(LocationHistory& locations, pqxx::connection& db_conn) {
    try {
        if (!db_conn.is_open()) return;
        
        pqxx::work txn(db_conn);
        
        pqxx::result res = txn.exec(
            "SELECT l.latitude, l.longitude, l.altitude, l.accuracy, m.timestamp "
            "FROM locations l "
            "JOIN measurements m ON l.measurement_id = m.id "
            "WHERE l.latitude IS NOT NULL "
            "ORDER BY m.id ASC LIMIT 2000"
        );
        
        locations.clear();
        
        for (const auto& row : res) {
            double lat = row[0].as<double>();
            double lon = row[1].as<double>();
            double alt = row[2].as<double>();
            float acc = row[3].as<float>();
            locations.add(lat, lon, alt, acc);
        }
        
        txn.commit();
        cout << "Loaded " << locations.sample_count << " location samples from database" << endl;
        
    } catch (const exception& e) {
        cerr << "Error loading location history: " << e.what() << endl;
    }
}

void send_filter_command(SharedData* shared, const string& filter_name, bool value) {
    if (!g_command_socket) return;
    try {
        json cmd = {{"type","filter"}, {"filter",filter_name}, {"value",value}};
        zmq::message_t req(cmd.dump().size());
        memcpy(req.data(), cmd.dump().c_str(), cmd.dump().size());
        g_command_socket->send(req, zmq::send_flags::dontwait);
    } catch (...) {}
}

void run_gui(SharedData* shared) {
    if (!glfwInit()) return;
    GLFWwindow* window = glfwCreateWindow(1600, 900, "GPS Monitor Pro", NULL, NULL);
    if (!window) { glfwTerminate(); return; }
    
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    
    g_context = new zmq::context_t(1);
    g_command_socket = new zmq::socket_t(*g_context, ZMQ_REQ);
    g_command_socket->set(zmq::sockopt::rcvtimeo, 1000);
    g_command_socket->set(zmq::sockopt::sndtimeo, 1000);
    
    string server_ip = get_local_ip();
    try {
        g_command_socket->connect("tcp://" + server_ip + ":8080");
        cout << "Connected to ZMQ server" << endl;
    } catch (...) {
        cerr << "Failed to connect to ZMQ server" << endl;
    }
    
    pqxx::connection db_conn("dbname=cellmap user=postgres password=postgres host=localhost port=5434");
    if (!db_conn.is_open()) {
        cerr << "Failed to connect to PostgreSQL" << endl;
    } else {
        cout << "Connected to PostgreSQL" << endl;
        load_points_from_db(map_points, db_conn);
    }
    
    SignalHistory signal;
    TrafficHistory traffic_hist;
    LocationHistory location_hist;
    
    map<int, json> cells_by_pci;
    
    int last_count = 0;
    
    bool prev_filter_location = shared->filter_location;
    bool prev_filter_telephony = shared->filter_telephony;
    bool prev_filter_traffic = shared->filter_traffic;
    bool prev_filter_lte = shared->filter_lte;
    bool prev_filter_gsm = shared->filter_gsm;
    bool prev_filter_wcdma = shared->filter_wcdma;
    
    bool show_heatmap = true;
    int heatmap_point_size = 5;
    
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        if (shared->counter != last_count) {
            last_count = shared->counter;
            lock_guard<mutex> lock(shared->data_mutex);
            
            cells_by_pci.clear();
            
            for (auto& item : shared->recent_records) {
                if (item.contains("telephony")) {
                    for (auto& [key, cell] : item["telephony"].items()) {
                        if (cell.is_object()) {
                            int pci = cell.value("pci", 0);
                            if (pci > 0) {
                                cells_by_pci[pci] = cell;
                            }
                        }
                    }
                }
            }
        }
        
        if (prev_filter_location != shared->filter_location) {
            send_filter_command(shared, "location", shared->filter_location);
            prev_filter_location = shared->filter_location;
        }
        if (prev_filter_telephony != shared->filter_telephony) {
            send_filter_command(shared, "telephony", shared->filter_telephony);
            prev_filter_telephony = shared->filter_telephony;
        }
        if (prev_filter_traffic != shared->filter_traffic) {
            send_filter_command(shared, "traffic", shared->filter_traffic);
            prev_filter_traffic = shared->filter_traffic;
        }
        if (prev_filter_lte != shared->filter_lte) {
            send_filter_command(shared, "lte", shared->filter_lte);
            prev_filter_lte = shared->filter_lte;
        }
        if (prev_filter_gsm != shared->filter_gsm) {
            send_filter_command(shared, "gsm", shared->filter_gsm);
            prev_filter_gsm = shared->filter_gsm;
        }
        if (prev_filter_wcdma != shared->filter_wcdma) {
            send_filter_command(shared, "wcdma", shared->filter_wcdma);
            prev_filter_wcdma = shared->filter_wcdma;
        }
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("GPS Monitor Pro", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        
        if (ImGui::BeginTabBar("Tabs")) {
            
            if (ImGui::BeginTabItem("Dashboard")) {
                ImGui::Text("Total Records: %d", shared->counter);
                ImGui::Text("Active Cells: %zu", cells_by_pci.size());
                ImGui::Text("Map Points: %zu", map_points.size());
                ImGui::Separator();
                if (!cells_by_pci.empty()) {
                    auto& [pci, cell] = *cells_by_pci.begin();
                    ImGui::Text("Last Cell - Type: %s | PCI: %d | RSRP: %d dBm", 
                        cell.value("type","Unknown").c_str(), pci, cell.value("rsrp",-140));
                }
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Heatmap")) {
                ImGui::Checkbox("Show Map", &show_heatmap);
                ImGui::SameLine();
                ImGui::SliderInt("Point Size", &heatmap_point_size, 2, 10);
                ImGui::SameLine();
                if (ImGui::Button("Reload from DB")) {
                    if (db_conn.is_open()) {
                        load_points_from_db(map_points, db_conn);
                        load_signal_history_from_db(signal, db_conn);
                        load_traffic_from_db(traffic_hist, db_conn);
                        load_locations_from_db(location_hist, db_conn);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Open Browser")) {
                    string url = "http://" + server_ip + ":8081/heatmap.html";
                    system(("cmd.exe /c start " + url).c_str());
                }
                ImGui::Separator();
                if (show_heatmap && !map_points.empty()) draw_minimap(map_points, heatmap_point_size);
                else if (show_heatmap) ImGui::Text("No points. Click 'Reload from DB'.");
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Signal Graphs")) {
                if (signal.rsrp.empty()) {
                    ImGui::Text("No signal data. Click 'Reload from DB' in Heatmap tab.");
                    if (ImGui::Button("Load Signal Data")) {
                        if (db_conn.is_open()) {
                            load_signal_history_from_db(signal, db_conn);
                        }
                    }
                } else {
                    vector<float> times(signal.times.begin(), signal.times.end());
                    
                    if (ImPlot::BeginPlot("RSRP (dBm)", ImVec2(-1, 300))) {
                        ImPlot::SetupAxes("Sample", "dBm");
                        ImPlot::SetupAxisLimits(ImAxis_Y1, -140, -60);
                        
                        for (auto& [pci, values] : signal.rsrp) {
                            if (values.empty()) continue;
                            vector<float> data(values.begin(), values.end());
                            if (data.size() == times.size()) {
                                ImPlot::PlotLine(("PCI=" + to_string(pci)).c_str(), times.data(), data.data(), data.size());
                            }
                        }
                        ImPlot::EndPlot();
                    }
                    
                    if (ImPlot::BeginPlot("RSRQ (dB)", ImVec2(-1, 250))) {
                        ImPlot::SetupAxes("Sample", "dB");
                        ImPlot::SetupAxisLimits(ImAxis_Y1, -20, -3);
                        for (auto& [pci, values] : signal.rsrq) {
                            if (values.empty()) continue;
                            vector<float> data(values.begin(), values.end());
                            if (data.size() == times.size()) {
                                ImPlot::PlotLine(("PCI=" + to_string(pci)).c_str(), times.data(), data.data(), data.size());
                            }
                        }
                        ImPlot::EndPlot();
                    }
                    
                    if (ImPlot::BeginPlot("RSSNR (dB)", ImVec2(-1, 250))) {
                        ImPlot::SetupAxes("Sample", "dB");
                        ImPlot::SetupAxisLimits(ImAxis_Y1, -10, 30);
                        for (auto& [pci, values] : signal.rssnr) {
                            if (values.empty()) continue;
                            vector<float> data(values.begin(), values.end());
                            if (data.size() == times.size()) {
                                ImPlot::PlotLine(("PCI=" + to_string(pci)).c_str(), times.data(), data.data(), data.size());
                            }
                        }
                        ImPlot::EndPlot();
                    }
                }
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Location Graphs")) {
                if (location_hist.latitudes.empty()) {
                    ImGui::Text("No location data. Click 'Reload from DB' in Heatmap tab.");
                } else {
                    vector<float> times(location_hist.times.begin(), location_hist.times.end());
                    vector<float> lats(location_hist.latitudes.begin(), location_hist.latitudes.end());
                    vector<float> lons(location_hist.longitudes.begin(), location_hist.longitudes.end());
                    vector<float> alts(location_hist.altitudes.begin(), location_hist.altitudes.end());
                    vector<float> accs(location_hist.accuracies.begin(), location_hist.accuracies.end());
                    
                    if (ImPlot::BeginPlot("Latitude", ImVec2(-1, 200))) {
                        ImPlot::SetupAxes("Sample", "Latitude");
                        ImPlot::PlotLine("Latitude", times.data(), lats.data(), lats.size());
                        ImPlot::EndPlot();
                    }
                    
                    if (ImPlot::BeginPlot("Longitude", ImVec2(-1, 200))) {
                        ImPlot::SetupAxes("Sample", "Longitude");
                        ImPlot::PlotLine("Longitude", times.data(), lons.data(), lons.size());
                        ImPlot::EndPlot();
                    }
                    
                    if (ImPlot::BeginPlot("Altitude (m)", ImVec2(-1, 200))) {
                        ImPlot::SetupAxes("Sample", "Meters");
                        ImPlot::PlotLine("Altitude", times.data(), alts.data(), alts.size());
                        ImPlot::EndPlot();
                    }
                    
                    if (ImPlot::BeginPlot("Accuracy (m)", ImVec2(-1, 200))) {
                        ImPlot::SetupAxes("Sample", "Meters");
                        ImPlot::PlotLine("Accuracy", times.data(), accs.data(), accs.size());
                        ImPlot::EndPlot();
                    }
                }
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Traffic Graphs")) {
                if (traffic_hist.rx_bytes.empty()) {
                    ImGui::Text("No traffic data. Click 'Reload from DB' in Heatmap tab.");
                } else {
                    vector<float> times(traffic_hist.times.begin(), traffic_hist.times.end());
                    vector<float> rx_mb, tx_mb;
                    
                    for (size_t i = 0; i < traffic_hist.rx_bytes.size(); i++) {
                        rx_mb.push_back(traffic_hist.rx_bytes[i] / 1024.0f / 1024.0f);
                        tx_mb.push_back(traffic_hist.tx_bytes[i] / 1024.0f / 1024.0f);
                    }
                    
                    if (ImPlot::BeginPlot("Traffic (MB)", ImVec2(-1, 300))) {
                        ImPlot::SetupAxes("Sample", "MB");
                        ImPlot::PlotLine("RX", times.data(), rx_mb.data(), rx_mb.size());
                        ImPlot::PlotLine("TX", times.data(), tx_mb.data(), tx_mb.size());
                        ImPlot::EndPlot();
                    }
                    
                    long long total_rx = 0, total_tx = 0;
                    for (auto& r : traffic_hist.rx_bytes) total_rx += r;
                    for (auto& t : traffic_hist.tx_bytes) total_tx += t;
                    
                    ImGui::Text("Total RX: %s", formatBytes(total_rx).c_str());
                    ImGui::Text("Total TX: %s", formatBytes(total_tx).c_str());
                }
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Cell Info")) {
                if (cells_by_pci.empty()) {
                    ImGui::Text("No cell data");
                } else {
                    ImGui::BeginChild("Cells");
                    for (auto& [pci, cell] : cells_by_pci) {
                        string type = cell.value("type", "Unknown");
                        ImGui::TextColored(type == "LTE" ? ImVec4(0,1,0,1) : ImVec4(1,1,0,1), 
                            "PCI: %d (%s)", pci, type.c_str());
                        ImGui::Indent();
                        ImGui::Text("RSRP: %d dBm", cell.value("rsrp", -140));
                        ImGui::Text("EARFCN: %d | TAC: %d | CI: %d", 
                            cell.value("earfcn",0), cell.value("tac",0), cell.value("ci",0));
                        ImGui::Unindent();
                        ImGui::Separator();
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Filters")) {
                ImGui::Checkbox("Location", &shared->filter_location);
                ImGui::Checkbox("Telephony", &shared->filter_telephony);
                ImGui::Checkbox("Traffic", &shared->filter_traffic);
                ImGui::Separator();
                ImGui::Checkbox("LTE (4G)", &shared->filter_lte);
                ImGui::Checkbox("GSM (2G)", &shared->filter_gsm);
                ImGui::Checkbox("WCDMA (3G)", &shared->filter_wcdma);
                ImGui::Separator();
                if (ImGui::Button("Enable All")) {
                    shared->filter_location = shared->filter_telephony = shared->filter_traffic = true;
                    shared->filter_lte = shared->filter_gsm = shared->filter_wcdma = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Disable All")) {
                    shared->filter_location = shared->filter_telephony = shared->filter_traffic = false;
                    shared->filter_lte = shared->filter_gsm = shared->filter_wcdma = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Apply")) {
                    send_filter_command(shared, "location", shared->filter_location);
                    send_filter_command(shared, "telephony", shared->filter_telephony);
                    send_filter_command(shared, "traffic", shared->filter_traffic);
                    send_filter_command(shared, "lte", shared->filter_lte);
                    send_filter_command(shared, "gsm", shared->filter_gsm);
                    send_filter_command(shared, "wcdma", shared->filter_wcdma);
                }
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        ImGui::Text("Status: ONLINE | Records: %d | Cells: %zu | Map Points: %zu", 
            shared->counter, cells_by_pci.size(), map_points.size());
        ImGui::End();
        
        ImGui::Render();
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }
    
    delete g_command_socket;
    delete g_context;
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
}