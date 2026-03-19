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

void generate_html_map(const std::vector<MapPoint>& points, const std::string& filename);
void draw_minimap(const std::vector<MapPoint>& points, int point_size);
void load_points_from_json(std::vector<MapPoint>& map_points);
std::string formatTime(long long ts);

using namespace std;
using json = nlohmann::json;

zmq::context_t* g_context = nullptr;
zmq::socket_t* g_command_socket = nullptr;

vector<MapPoint> map_points;

string formatBytes(long long bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = bytes;
    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f %s", size, units[unit]);
    return string(buf);
}

void send_filter_command(SharedData* shared, const string& filter_name, bool value) {
    if (!g_command_socket) return;
    
    try {
        json command;
        command["type"] = "filter";
        command["filter"] = filter_name;
        command["value"] = value;
        
        string cmd_str = command.dump();
        
        zmq::message_t request(cmd_str.size());
        memcpy(request.data(), cmd_str.c_str(), cmd_str.size());
        
        if (g_command_socket->send(request, zmq::send_flags::dontwait)) {
            zmq::message_t reply;
            try {
                g_command_socket->recv(reply, zmq::recv_flags::dontwait);
                cout << "Filter command sent: " << filter_name << " = " << value << endl;
            } catch (...) {}
        }
    } catch (const exception& e) {
        cerr << "Failed to send filter command: " << e.what() << endl;
    }
}

struct SignalData {
    float rsrp;
    float rsrq;
    float rssnr;
    float dbm;
    float level;
    long long timestamp;
};

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
    g_command_socket = new zmq::socket_t(*g_context, zmq::socket_type::req);
    g_command_socket->set(zmq::sockopt::rcvtimeo, 1000);
    g_command_socket->set(zmq::sockopt::sndtimeo, 1000);
    g_command_socket->set(zmq::sockopt::linger, 0);
    
    string server_ip = get_local_ip();
    
    try {
        g_command_socket->connect("tcp://" + server_ip + ":8080");
        cout << "Command socket connected to port 8080" << endl;
    } catch (const exception& e) {
        cerr << "Failed to connect command socket: " << e.what() << endl;
    }
    
    load_points_from_json(map_points);
    
    vector<json> locations;
    vector<pair<string, json>> cells;
    vector<json> traffic;
    
    int last_count = 0;
    int last_points_count = 0;
    
    deque<float> rsrp_history;
    deque<float> rsrq_history;
    deque<float> rssnr_history;
    deque<float> dbm_history;
    deque<float> level_history;
    deque<float> time_history;
    const int max_history = 200;
    
    int sample_count = 0;
    
    bool prev_filter_location = shared->filter_location;
    bool prev_filter_telephony = shared->filter_telephony;
    bool prev_filter_traffic = shared->filter_traffic;
    bool prev_filter_lte = shared->filter_lte;
    bool prev_filter_gsm = shared->filter_gsm;
    bool prev_filter_wcdma = shared->filter_wcdma;
    
    bool command_socket_connected = false;
    bool show_heatmap = true;
    int heatmap_point_size = 5;
    
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        if (shared->counter != last_count) {
            last_count = shared->counter;
            
            lock_guard<mutex> lock(shared->data_mutex);
            locations.clear();
            cells.clear();
            traffic.clear();
            
            for (auto& item : shared->recent_records) {
                if (item.contains("location")) {
                    locations.push_back(item);
                }
                
                if (item.contains("telephony")) {
                    auto& tele = item["telephony"];
                    for (auto& [key, val] : tele.items()) {
                        if (key.find("cell_") == 0) {
                            cells.push_back({key, val});
                            
                            int dbm = val.value("dbm", -120);
                            int rsrp = val.value("rsrp", -140);
                            int rsrq = val.value("rsrq", -20);
                            int rssnr = val.value("rssnr", 0);
                            
                            int level = 0;
                            if (dbm >= -70) level = 4;
                            else if (dbm >= -80) level = 3;
                            else if (dbm >= -90) level = 2;
                            else if (dbm >= -100) level = 1;
                            else level = 0;
                            
                            if (time_history.size() >= max_history) {
                                time_history.pop_front();
                                dbm_history.pop_front();
                                level_history.pop_front();
                                rsrp_history.pop_front();
                                rsrq_history.pop_front();
                                rssnr_history.pop_front();
                            }
                            
                            time_history.push_back(sample_count);
                            dbm_history.push_back(dbm);
                            level_history.push_back(level);
                            
                            string type = val.value("type", "");
                            if (type == "LTE" || type == "NR") {
                                rsrp_history.push_back(rsrp);
                                rsrq_history.push_back(rsrq);
                                rssnr_history.push_back(rssnr);
                            } else {
                                rsrp_history.push_back(-140);
                                rsrq_history.push_back(-20);
                                rssnr_history.push_back(0);
                            }
                        }
                    }
                    sample_count++;
                }
                
                if (item.contains("traffic")) {
                    traffic.push_back(item["traffic"]);
                }
            }
        }
        
        if (locations.size() != last_points_count) {
            last_points_count = locations.size();
            load_points_from_json(map_points);
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
        
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("GPS Monitor Pro", NULL, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        
        if (ImGui::BeginTabBar("Tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
            
            if (ImGui::BeginTabItem("Dashboard")) {
                ImGui::Text("=== System Overview ===");
                ImGui::Separator();
                
                ImGui::Columns(2);
                ImGui::Text("Total Records: %d", shared->counter);
                ImGui::Text("Active Cells: %zu", cells.size());
                ImGui::NextColumn();
                ImGui::Text("Traffic Records: %zu", traffic.size());
                ImGui::Text("Graph Points: %zu", time_history.size());
                ImGui::Text("Map Points: %zu", map_points.size());
                ImGui::Columns(1);
                
                ImGui::Separator();
                
                if (!cells.empty()) {
                    auto& [key, cell] = *cells.rbegin();
                    ImGui::Text("=== Last Cell Info ===");
                    ImGui::Text("Type: %s", cell.value("type", "Unknown").c_str());
                    ImGui::Text("PCI: %d", cell.value("pci", 0));
                    ImGui::Text("RSRP: %d dBm", cell.value("rsrp", -140));
                    ImGui::Text("RSRQ: %d dB", cell.value("rsrq", -20));
                    ImGui::Text("RSSNR: %d dB", cell.value("rssnr", 0));
                    ImGui::Text("dBm: %d", cell.value("dbm", -120));
                }
                
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Heatmap")) {
    ImGui::Checkbox("Show Mini-map", &show_heatmap);
    ImGui::SameLine();
    ImGui::SliderInt("Point Size", &heatmap_point_size, 2, 10);
    ImGui::SameLine();
    if (ImGui::Button("Reload Data")) {
        load_points_from_json(map_points);
    }
    
    if (ImGui::Button("Open in Browser (Full Map)")) {
        generate_html_map(map_points, "heatmap.html");
        system("xdg-open heatmap.html"); // Linux
        // system("open heatmap.html"); // Mac
        // system("start heatmap.html"); // Windows
    }
    
    ImGui::Separator();
    
    if (show_heatmap) {
        draw_minimap(map_points, heatmap_point_size);
    }
    
    ImGui::EndTabItem();
}
            
            if (ImGui::BeginTabItem("Filters")) {
                ImGui::Text("=== Data Type Filters ===");
                ImGui::Separator();
                
                ImGui::Checkbox("Location Data", &shared->filter_location);
                ImGui::SameLine();
                ImGui::TextColored(shared->filter_location ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1), 
                    shared->filter_location ? "[ENABLED]" : "[DISABLED]");
                
                ImGui::Checkbox("Telephony Data (Cell Towers)", &shared->filter_telephony);
                ImGui::SameLine();
                ImGui::TextColored(shared->filter_telephony ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1), 
                    shared->filter_telephony ? "[ENABLED]" : "[DISABLED]");
                
                ImGui::Checkbox("Traffic Data", &shared->filter_traffic);
                ImGui::SameLine();
                ImGui::TextColored(shared->filter_traffic ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1), 
                    shared->filter_traffic ? "[ENABLED]" : "[DISABLED]");
                
                ImGui::Separator();
                ImGui::Text("=== Network Type Filters ===");
                ImGui::Separator();
                
                ImGui::Checkbox("4G/LTE Cells", &shared->filter_lte);
                ImGui::SameLine();
                ImGui::TextColored(shared->filter_lte ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1), 
                    shared->filter_lte ? "[ENABLED]" : "[DISABLED]");
                
                ImGui::Checkbox("2G/GSM Cells", &shared->filter_gsm);
                ImGui::SameLine();
                ImGui::TextColored(shared->filter_gsm ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1), 
                    shared->filter_gsm ? "[ENABLED]" : "[DISABLED]");
                
                ImGui::Checkbox("3G/WCDMA Cells", &shared->filter_wcdma);
                ImGui::SameLine();
                ImGui::TextColored(shared->filter_wcdma ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1), 
                    shared->filter_wcdma ? "[ENABLED]" : "[DISABLED]");
                
                ImGui::Separator();
                
                if (ImGui::Button("Enable All", ImVec2(120, 30))) {
                    shared->filter_location = true;
                    shared->filter_telephony = true;
                    shared->filter_traffic = true;
                    shared->filter_lte = true;
                    shared->filter_gsm = true;
                    shared->filter_wcdma = true;
                }
                
                ImGui::SameLine();
                
                if (ImGui::Button("Disable All", ImVec2(120, 30))) {
                    shared->filter_location = false;
                    shared->filter_telephony = false;
                    shared->filter_traffic = false;
                    shared->filter_lte = false;
                    shared->filter_gsm = false;
                    shared->filter_wcdma = false;
                }
                
                ImGui::SameLine();
                
                if (ImGui::Button("Send Filters Now", ImVec2(150, 30))) {
                    send_filter_command(shared, "location", shared->filter_location);
                    send_filter_command(shared, "telephony", shared->filter_telephony);
                    send_filter_command(shared, "traffic", shared->filter_traffic);
                    send_filter_command(shared, "lte", shared->filter_lte);
                    send_filter_command(shared, "gsm", shared->filter_gsm);
                    send_filter_command(shared, "wcdma", shared->filter_wcdma);
                }
                
                ImGui::Separator();
                
                ImGui::Text("Command Socket Status:");
                ImGui::SameLine();
                
                if (g_command_socket) {
                    ImGui::TextColored(ImVec4(0,1,0,1), "CONNECTED");
                } else {
                    ImGui::TextColored(ImVec4(1,0,0,1), "DISCONNECTED");
                }
                
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Signal Graphs")) {
                if (dbm_history.empty() && rsrp_history.empty()) {
                    ImGui::TextColored(ImVec4(1,1,0,1), "No signal data. Waiting for data...");
                } else {
                    if (!time_history.empty()) {
                        vector<float> times(time_history.begin(), time_history.end());
                        
                        if (!dbm_history.empty()) {
                            vector<float> dbm_vals(dbm_history.begin(), dbm_history.end());
                            
                            if (ImPlot::BeginPlot("Signal Strength (dBm)", ImVec2(-1, 200))) {
                                ImPlot::SetupAxes("Samples", "dBm");
                                ImPlot::SetupAxisLimits(ImAxis_Y1, -120, -50);
                                ImPlot::PlotLine("dBm", times.data(), dbm_vals.data(), dbm_vals.size());
                                ImPlot::EndPlot();
                            }
                        }
                        
                        if (!rsrp_history.empty()) {
                            vector<float> rsrp_vals(rsrp_history.begin(), rsrp_history.end());
                            
                            if (ImPlot::BeginPlot("RSRP (LTE)", ImVec2(-1, 200))) {
                                ImPlot::SetupAxes("Samples", "dBm");
                                ImPlot::SetupAxisLimits(ImAxis_Y1, -140, -60);
                                ImPlot::PlotLine("RSRP", times.data(), rsrp_vals.data(), rsrp_vals.size());
                                ImPlot::EndPlot();
                            }
                        }
                        
                        if (!rsrq_history.empty()) {
                            vector<float> rsrq_vals(rsrq_history.begin(), rsrq_history.end());
                            
                            if (ImPlot::BeginPlot("RSRQ", ImVec2(-1, 200))) {
                                ImPlot::SetupAxes("Samples", "dB");
                                ImPlot::SetupAxisLimits(ImAxis_Y1, -20, -3);
                                ImPlot::PlotLine("RSRQ", times.data(), rsrq_vals.data(), rsrq_vals.size());
                                ImPlot::EndPlot();
                            }
                        }
                        
                        if (!rssnr_history.empty()) {
                            vector<float> rssnr_vals(rssnr_history.begin(), rssnr_history.end());
                            
                            if (ImPlot::BeginPlot("RSSNR", ImVec2(-1, 200))) {
                                ImPlot::SetupAxes("Samples", "dB");
                                ImPlot::SetupAxisLimits(ImAxis_Y1, -10, 30);
                                ImPlot::PlotLine("RSSNR", times.data(), rssnr_vals.data(), rssnr_vals.size());
                                ImPlot::EndPlot();
                            }
                        }
                        
                        if (!level_history.empty()) {
                            vector<float> level_vals(level_history.begin(), level_history.end());
                            
                            if (ImPlot::BeginPlot("Signal Level (0-4)", ImVec2(-1, 150))) {
                                ImPlot::SetupAxes("Samples", "Level");
                                ImPlot::SetupAxisLimits(ImAxis_Y1, -0.5, 4.5);
                                ImPlot::PlotLine("Level", times.data(), level_vals.data(), level_vals.size());
                                ImPlot::EndPlot();
                            }
                        }
                    }
                }
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Cell Info")) {
                if (cells.empty()) {
                    ImGui::Text("No cell data");
                } else {
                    ImGui::BeginChild("CellList");
                    
                    map<string, int> type_count;
                    for (auto& [key, cell] : cells) {
                        string type = cell.value("type", "Unknown");
                        type_count[type]++;
                    }
                    
                    ImGui::Text("Cell Types:");
                    for (auto& [type, count] : type_count) {
                        ImGui::Text("  %s: %d", type.c_str(), count);
                    }
                    ImGui::Separator();
                    
                    for (auto it = cells.rbegin(); it != cells.rend(); ++it) {
                        auto& [key, cell] = *it;
                        
                        string type = cell.value("type", "Unknown");
                        int pci = cell.value("pci", 0);
                        int earfcn = cell.value("earfcn", 0);
                        int tac = cell.value("tac", 0);
                        int ci = cell.value("ci", 0);
                        int mcc = cell.value("mcc", 0);
                        int mnc = cell.value("mnc", 0);
                        
                        int dbm = cell.value("dbm", -120);
                        int rsrp = cell.value("rsrp", -140);
                        int rsrq = cell.value("rsrq", -20);
                        int rssnr = cell.value("rssnr", 0);
                        
                        if (type == "LTE") ImGui::TextColored(ImVec4(0,1,0,1), "[4G] %s", key.c_str());
                        else if (type == "NR") ImGui::TextColored(ImVec4(1,0,1,1), "[5G] %s", key.c_str());
                        else ImGui::Text("[%s] %s", type.c_str(), key.c_str());
                        
                        ImGui::Indent(20);
                        
                        ImGui::Text("PCI: %d | EARFCN: %d | TAC: %d | CI: %d", pci, earfcn, tac, ci);
                        ImGui::Text("MCC: %d | MNC: %d", mcc, mnc);
                        
                        ImVec4 color;
                        if (rsrp >= -80) color = ImVec4(0,1,0,1);
                        else if (rsrp >= -90) color = ImVec4(0,1,0,0.7);
                        else if (rsrp >= -100) color = ImVec4(1,1,0,1);
                        else if (rsrp >= -110) color = ImVec4(1,0.5,0,1);
                        else color = ImVec4(1,0,0,1);
                        
                        ImGui::TextColored(color, "RSRP: %d dBm | RSRQ: %d dB | RSSNR: %d dB", rsrp, rsrq, rssnr);
                        ImGui::Text("dBm: %d | Level: %d", dbm, cell.value("level", 0));
                        
                        ImGui::Unindent(20);
                        ImGui::Separator();
                    }
                    
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Locations")) {
                if (locations.empty()) {
                    ImGui::Text("No location data");
                } else {
                    ImGui::BeginChild("Scroll");
                    for (size_t i = 0; i < locations.size(); i++) {
                        auto& item = locations[i];
                        long long ts = item.value("timestamp", 0LL);
                        
                        if (item.contains("location")) {
                            auto& loc = item["location"];
                            double lat = loc.value("latitude", 0.0);
                            double lon = loc.value("longitude", 0.0);
                            double alt = loc.value("altitude", 0.0);
                            double acc = loc.value("accuracy", 0.0);
                            
                            ImGui::Text("[%zu] %s", i+1, formatTime(ts).c_str());
                            ImGui::Text("  Lat: %.6f, Lon: %.6f", lat, lon);
                            ImGui::Text("  Alt: %.1fm, Acc: %.1fm", alt, acc);
                            ImGui::Separator();
                        }
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Traffic")) {
                if (traffic.empty()) {
                    ImGui::Text("No traffic data");
                } else {
                    ImGui::BeginChild("TrafficList");
                    
                    long long total_rx_sum = 0;
                    long long total_tx_sum = 0;
                    
                    for (auto it = traffic.rbegin(); it != traffic.rend(); ++it) {
                        auto& t = *it;
                        long long total_rx = t.value("total_rx_bytes", t.value("total_rx", 0LL));
                        long long total_tx = t.value("total_tx_bytes", t.value("total_tx", 0LL));
                        
                        total_rx_sum += total_rx;
                        total_tx_sum += total_tx;
                        
                        ImGui::Text("RX: %s", formatBytes(total_rx).c_str());
                        ImGui::Text("TX: %s", formatBytes(total_tx).c_str());
                        
                        if (t.contains("top_apps") && !t["top_apps"].empty()) {
                            ImGui::Indent();
                            for (auto& [app, bytes] : t["top_apps"].items()) {
                                ImGui::Text("%s: %s", app.c_str(), formatBytes(bytes.get<long long>()).c_str());
                            }
                            ImGui::Unindent();
                        }
                        ImGui::Separator();
                    }
                    
                    ImGui::Text("TOTAL - RX: %s", formatBytes(total_rx_sum).c_str());
                    ImGui::Text("TOTAL - TX: %s", formatBytes(total_tx_sum).c_str());
                    
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        ImGui::Separator();
        ImGui::Text("Status: ");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0,1,0,1), "● ONLINE");
        ImGui::SameLine();
        ImGui::Text(" | Records: %d | Cells: %zu | Traffic: %zu | Samples: %zu | Map Points: %zu", 
                   shared->counter, cells.size(), traffic.size(), time_history.size(), map_points.size());
        
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