#include "server.hpp"
#include "../third-party/imgui/imgui.h"
#include "../third-party/imgui/backends/imgui_impl_glfw.h"
#include "../third-party/imgui/backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <ctime>
#include <iomanip>
#include <vector>
#include <map>
#include <algorithm>

using namespace std;
using namespace ImGui;

string format_time(long long timestamp_ms) {
    if (timestamp_ms == 0) return "0";
    time_t seconds = timestamp_ms / 1000;
    struct tm* timeinfo = localtime(&seconds);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M:%S", timeinfo);
    return string(buffer);
}

string format_bytes(long long bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unit = 0;
    double size = bytes;
    while (size >= 1024 && unit < 3) {
        size /= 1024;
        unit++;
    }
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unit]);
    return string(buffer);
}

string format_json_compact(const string& json_str) {
    if (json_str.empty()) return "{}";
    if (json_str.length() > 100) {
        return json_str.substr(0, 100) + "...";
    }
    return json_str;
}

struct TelephonyRecord {
    long long timestamp;
    string data;
};

struct TrafficRecord {
    long long timestamp;
    long long mobile_rx;
    long long mobile_tx;
    long long total_rx;
    long long total_tx;
    map<string, json> top_apps;
};

void run_gui(SharedData* shared) {
    if (!glfwInit()) return;

    GLFWwindow* window = glfwCreateWindow(1600, 900, "GPS Server Monitor", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    CreateContext();
    ImGuiIO& io = GetIO(); (void)io;
    StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    ImFontConfig font_config;
    font_config.SizePixels = 20.0f;
    io.Fonts->AddFontDefault(&font_config);

    ImGuiStyle& style = GetStyle();
    style.WindowRounding = 10.0f;
    style.FrameRounding = 8.0f;
    style.ScrollbarSize = 24.0f;
    style.GrabMinSize = 16.0f;
    style.WindowBorderSize = 2.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.2f, 0.3f, 0.5f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.2f, 0.3f, 0.5f, 0.5f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.4f, 0.6f, 0.7f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.4f, 0.5f, 0.7f, 0.8f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.3f, 0.4f, 0.6f, 1.0f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.2f, 0.3f, 0.5f, 1.0f);

    vector<TelephonyRecord> telephony_history;
    vector<TrafficRecord> traffic_history;
    long long last_telephony_load = 0;
    long long last_traffic_load = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        long long current_time = chrono::duration_cast<chrono::seconds>(
            chrono::system_clock::now().time_since_epoch()
        ).count();

        if (current_time - last_telephony_load > 2) {
            last_telephony_load = current_time;
            telephony_history.clear();
            
            ifstream telephony_file("telephony_data.json");
            if (telephony_file.is_open()) {
                try {
                    json all_data = json::parse(telephony_file);
                    if (all_data.is_array()) {
                        for (const auto& record : all_data) {
                            TelephonyRecord tr;
                            tr.timestamp = record.value("timestamp", 0LL);
                            if (record.contains("data") && !record["data"].is_null()) {
                                tr.data = record["data"].dump(2);
                            } else {
                                tr.data = "{}";
                            }
                            telephony_history.push_back(tr);
                        }
                    }
                } catch (...) {}
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        NewFrame();

        const ImGuiViewport* viewport = GetMainViewport();
        SetNextWindowPos(viewport->Pos);
        SetNextWindowSize(viewport->Size);
        SetNextWindowViewport(viewport->ID);

        Begin("GPS Server Monitor", nullptr, 
            ImGuiWindowFlags_NoTitleBar | 
            ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings);

        Columns(2, "MainColumns", false);
        SetColumnWidth(0, 600);

        BeginChild("CurrentData", ImVec2(0, 0), true);
        
        PushFont(io.Fonts->Fonts[0]);
        
        TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "CURRENT GPS POSITION");
        Separator();
        Spacing();

        {
            lock_guard<mutex> lock(shared->data_mutex);
            
            Text("Total Records: "); 
            SameLine(); 
            TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%d", shared->counter);

            Spacing();
            SeparatorText("COORDINATES");
            
            Text("Latitude:"); 
            SameLine(180); 
            TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%.6f", shared->current_location.latitude);
            
            Text("Longitude:"); 
            SameLine(180); 
            TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%.6f", shared->current_location.longitude);
            
            Text("Altitude:"); 
            SameLine(180); 
            TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%.2f m", shared->current_location.altitude);
            
            Text("Accuracy:"); 
            SameLine(180); 
            TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%.1f m", shared->current_location.accuracy);
            
            Text("Timestamp:"); 
            SameLine(180); 
            TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", format_time(shared->current_location.timestamp).c_str());

            Spacing();
            SeparatorText("ACTIVITY");
            
            float activity_indicator = (shared->counter > 0) ? 1.0f : 0.0f;
            ProgressBar(activity_indicator, ImVec2(-1, 30), shared->counter > 0 ? "Active" : "Waiting for data");
        }

        Spacing();
        Separator();

        TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "CONTROL");
        Spacing();
        
        if (Button("Clear Log Files", ImVec2(220, 45))) {
            ofstream("location_data.json").close();
            ofstream("telephony_data.json").close();
            ofstream("traffic_data.json").close();
            ofstream("location_log.txt").close();
        }
        
        SameLine();
        
        if (Button("Reset Counter", ImVec2(220, 45))) {
            lock_guard<mutex> lock(shared->data_mutex);
            shared->counter = 0;
            shared->recent_records.clear();
        }

        PopFont();
        EndChild();
        NextColumn();

        BeginChild("HistoryData", ImVec2(0, 0), true);
        PushFont(io.Fonts->Fonts[0]);

        if (BeginTabBar("DataTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
            
            if (BeginTabItem("Location History")) {
                BeginChild("HistoryList", ImVec2(0, 0), true);
                
                lock_guard<mutex> lock(shared->data_mutex);
                
                if (shared->recent_records.empty()) {
                    TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No records yet...");
                } else {
                    if (BeginTable("LocationTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, GetContentRegionAvail().y - 30))) {
                        TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 60);
                        TableSetupColumn("Latitude", ImGuiTableColumnFlags_WidthFixed, 180);
                        TableSetupColumn("Longitude", ImGuiTableColumnFlags_WidthFixed, 180);
                        TableSetupColumn("Altitude", ImGuiTableColumnFlags_WidthFixed, 120);
                        TableSetupColumn("Accuracy", ImGuiTableColumnFlags_WidthFixed, 120);
                        TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 220);
                        TableHeadersRow();

                        int idx = 0;
                        for (auto it = shared->recent_records.rbegin(); it != shared->recent_records.rend(); ++it) {
                            TableNextRow();
                            
                            double lat = 0.0, lon = 0.0, alt = 0.0, acc = 0.0;
                            long long ts = 0;
                            
                            if (it->contains("location") && !(*it)["location"].is_null()) {
                                auto& loc = (*it)["location"];
                                lat = loc.value("Latitude", 0.0);
                                lon = loc.value("Longitude", 0.0);
                                alt = loc.value("Altitude", 0.0);
                                acc = loc.value("Accuracy", 0.0);
                                ts = loc.value("timestamp", 0LL);
                            } else {
                                lat = it->value("Latitude", 0.0);
                                lon = it->value("Longitude", 0.0);
                                alt = it->value("Altitude", 0.0);
                                acc = it->value("Accuracy", 0.0);
                                ts = it->value("timestamp", 0LL);
                            }
                            
                            TableSetColumnIndex(0); Text("%d", shared->counter - idx);
                            TableSetColumnIndex(1); Text("%.4f", lat);
                            TableSetColumnIndex(2); Text("%.4f", lon);
                            TableSetColumnIndex(3); Text("%.1f", alt);
                            TableSetColumnIndex(4); Text("%.1f", acc);
                            TableSetColumnIndex(5); Text("%s", format_time(ts).c_str());
                            
                            idx++;
                        }
                        EndTable();
                    }
                }
                
                EndChild();
                EndTabItem();
            }

            if (BeginTabItem("Telephony Data")) {
                BeginChild("TelephonyList", ImVec2(0, 0), true);
                
                if (telephony_history.empty()) {
                    TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No telephony data yet...");
                } else {
                    if (BeginTable("TelephonyTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, GetContentRegionAvail().y - 30))) {
                        TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 60);
                        TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthFixed, 220);
                        TableSetupColumn("Data", ImGuiTableColumnFlags_WidthStretch);
                        TableHeadersRow();

                        int idx = 1;
                        for (auto it = telephony_history.rbegin(); it != telephony_history.rend(); ++it) {
                            TableNextRow();
                            
                            TableSetColumnIndex(0); Text("%d", idx++);
                            TableSetColumnIndex(1); Text("%s", format_time(it->timestamp).c_str());
                            
                            TableSetColumnIndex(2);
                            string preview = format_json_compact(it->data);
                            if (Selectable(preview.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                                OpenPopup("TelephonyDetails");
                            }
                            
                            if (BeginPopup("TelephonyDetails")) {
                                TextWrapped("%s", it->data.c_str());
                                EndPopup();
                            }
                        }
                        EndTable();
                    }
                }
                
                EndChild();
                EndTabItem();
            }

            if (BeginTabItem("Traffic Data")) {
                BeginChild("TrafficList", ImVec2(0, 0), true);
                
                if (traffic_history.empty()) {
                    if (current_time - last_traffic_load > 2) {
                        last_traffic_load = current_time;
                        traffic_history.clear();
                        
                        ifstream traffic_file("traffic_data.json");
                        if (traffic_file.is_open()) {
                            try {
                                json all_data = json::parse(traffic_file);
                                if (all_data.is_array()) {
                                    for (const auto& record : all_data) {
                                        TrafficRecord tr;
                                        tr.timestamp = record.value("timestamp", 0LL);
                                        tr.mobile_rx = record.value("mobile_rx_bytes", 0LL);
                                        tr.mobile_tx = record.value("mobile_tx_bytes", 0LL);
                                        tr.total_rx = record.value("total_rx_bytes", 0LL);
                                        tr.total_tx = record.value("total_tx_bytes", 0LL);
                                        
                                        if (record.contains("top_apps") && !record["top_apps"].is_null()) {
                                            for (auto& [key, val] : record["top_apps"].items()) {
                                                tr.top_apps[key] = val;
                                            }
                                        }
                                        traffic_history.push_back(tr);
                                    }
                                }
                            } catch (...) {}
                        }
                    }
                    
                    if (traffic_history.empty()) {
                        TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No traffic data yet...");
                    }
                }
                
                if (!traffic_history.empty()) {
                    if (BeginTabBar("TrafficTabs")) {
                        if (BeginTabItem("Summary")) {
                            if (BeginTable("TrafficTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, GetContentRegionAvail().y - 30))) {
                                TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 220);
                                TableSetupColumn("Mobile RX", ImGuiTableColumnFlags_WidthFixed, 150);
                                TableSetupColumn("Mobile TX", ImGuiTableColumnFlags_WidthFixed, 150);
                                TableSetupColumn("Total RX", ImGuiTableColumnFlags_WidthFixed, 150);
                                TableSetupColumn("Total TX", ImGuiTableColumnFlags_WidthFixed, 150);
                                TableHeadersRow();

                                for (auto it = traffic_history.rbegin(); it != traffic_history.rend(); ++it) {
                                    TableNextRow();
                                    
                                    TableSetColumnIndex(0); Text("%s", format_time(it->timestamp).c_str());
                                    TableSetColumnIndex(1); Text("%s", format_bytes(it->mobile_rx).c_str());
                                    TableSetColumnIndex(2); Text("%s", format_bytes(it->mobile_tx).c_str());
                                    TableSetColumnIndex(3); Text("%s", format_bytes(it->total_rx).c_str());
                                    TableSetColumnIndex(4); Text("%s", format_bytes(it->total_tx).c_str());
                                }
                                EndTable();
                            }
                            EndTabItem();
                        }
                        
                        if (BeginTabItem("Top Apps")) {
                            if (BeginTable("AppsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, GetContentRegionAvail().y - 30))) {
                                TableSetupColumn("App Name", ImGuiTableColumnFlags_WidthFixed, 250);
                                TableSetupColumn("RX", ImGuiTableColumnFlags_WidthFixed, 150);
                                TableSetupColumn("TX", ImGuiTableColumnFlags_WidthFixed, 150);
                                TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 150);
                                TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 220);
                                TableHeadersRow();

                                for (auto it = traffic_history.rbegin(); it != traffic_history.rend(); ++it) {
                                    for (const auto& [pkg, app] : it->top_apps) {
                                        TableNextRow();
                                        
                                        string app_name = app.value("app_name", pkg);
                                        long long rx = app.value("rx_bytes", 0LL);
                                        long long tx = app.value("tx_bytes", 0LL);
                                        long long total = app.value("total_bytes", 0LL);
                                        
                                        TableSetColumnIndex(0); Text("%s", app_name.c_str());
                                        TableSetColumnIndex(1); Text("%s", format_bytes(rx).c_str());
                                        TableSetColumnIndex(2); Text("%s", format_bytes(tx).c_str());
                                        TableSetColumnIndex(3); Text("%s", format_bytes(total).c_str());
                                        TableSetColumnIndex(4); Text("%s", format_time(it->timestamp).c_str());
                                    }
                                }
                                EndTable();
                            }
                            EndTabItem();
                        }
                        EndTabBar();
                    }
                }
                
                EndChild();
                EndTabItem();
            }

            if (BeginTabItem("Statistics")) {
                lock_guard<mutex> lock(shared->data_mutex);
                
                Spacing();
                TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "SERVER STATISTICS");
                Separator();
                Spacing();

                Text("Total location records:"); SameLine(300); 
                TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%d", shared->counter);
                
                Text("Total telephony records:"); SameLine(300); 
                TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%d", (int)telephony_history.size());
                
                Text("Total traffic records:"); SameLine(300); 
                TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%d", (int)traffic_history.size());
                
                Text("Records in memory:"); SameLine(300); 
                TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%d", (int)shared->recent_records.size());
                
                if (shared->recent_records.size() > 1) {
                    Spacing();
                    SeparatorText("Altitude Trend");
                    
                    vector<float> altitudes;
                    for (const auto& record : shared->recent_records) {
                        float alt = 0.0f;
                        if (record.contains("location") && !record["location"].is_null()) {
                            alt = record["location"].value("Altitude", 0.0f);
                        } else {
                            alt = record.value("Altitude", 0.0f);
                        }
                        altitudes.push_back(alt);
                    }
                    
                    PlotLines("##Altitude", altitudes.data(), altitudes.size(), 0, NULL, 0.0f, 100.0f, ImVec2(0, 150));
                }
                
                EndTabItem();
            }

            if (BeginTabItem("Server Info")) {
                Spacing();
                
                SeparatorText("CONFIGURATION");
                Text("ZeroMQ endpoint:"); SameLine(300); 
                TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "tcp://*:8080");
                
                Text("Data files:"); SameLine(300); 
                TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "location_data.json");
                SameLine();
                TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "telephony_data.json");
                SameLine();
                TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "traffic_data.json");
                SameLine();
                TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "location_log.txt");
                
                Spacing();
                SeparatorText("STATUS");
                TextWrapped("Server is running and accepting connections. GPS, telephony and traffic data are being saved to separate JSON files.");
                
                Spacing();
                SeparatorText("FORMATS");
                BulletText("Location data: Stored in location_data.json as array");
                BulletText("Telephony data: Stored in telephony_data.json with timestamps");
                BulletText("Traffic data: Stored in traffic_data.json with app statistics");
                BulletText("Text log: Human-readable format in location_log.txt");
                
                EndTabItem();
            }
            
            EndTabBar();
        }

        PopFont();
        EndChild();
        Columns(1);
        End();

        Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(GetDrawData());

        glfwSwapBuffers(window);
        this_thread::sleep_for(chrono::milliseconds(16));
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
}