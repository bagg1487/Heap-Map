#include "server.hpp"
#include "../third-party/imgui/imgui.h"
#include "../third-party/imgui/backends/imgui_impl_glfw.h"
#include "../third-party/imgui/backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>

using namespace std;

string format_timestamp(long long timestamp) {
    if (timestamp == 0) return "N/A";
    time_t time = timestamp;
    char buffer[30];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&time));
    return string(buffer);
}

void run_gui(SharedData* shared) {
    if (!glfwInit()) {
        cerr << "Failed to initialize GLFW" << endl;
        return;
    }

    GLFWwindow* window = glfwCreateWindow(1280, 720, "GPS Server Monitor", NULL, NULL);
    if (!window) {
        cerr << "Failed to create window" << endl;
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Инициализация ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Стилизация
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.2f, 0.3f, 0.5f, 1.0f);

    // Основной цикл GUI
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Создаем главное окно на весь экран
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::Begin("GPS Server Monitor", nullptr, 
            ImGuiWindowFlags_NoTitleBar | 
            ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings);

        // Разделяем окно на две колонки
        ImGui::Columns(2, "MainColumns", false);
        ImGui::SetColumnWidth(0, 400);

        // ЛЕВАЯ КОЛОНКА - Текущие данные
        ImGui::BeginChild("CurrentData", ImVec2(0, 0), true);
        
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "📡 CURRENT GPS POSITION");
        ImGui::Separator();
        ImGui::Spacing();

        {
            lock_guard<mutex> lock(shared->data_mutex);
            
            ImGui::Text("Total Records: "); 
            ImGui::SameLine(); 
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%d", shared->counter);

            ImGui::Spacing();
            
            ImGui::SeparatorText("📍 COORDINATES");
            
            ImGui::Text("Latitude:"); 
            ImGui::SameLine(120); 
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%.6f°", shared->current_location.latitude);
            
            ImGui::Text("Longitude:"); 
            ImGui::SameLine(120); 
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%.6f°", shared->current_location.longitude);
            
            ImGui::Text("Altitude:"); 
            ImGui::SameLine(120); 
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%.2f m", shared->current_location.altitude);
            
            ImGui::Text("Timestamp:"); 
            ImGui::SameLine(120); 
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", 
                format_timestamp(shared->current_location.timestamp).c_str());

            ImGui::Spacing();
            ImGui::SeparatorText("⚡ ACTIVITY");
            
            float activity_indicator = (shared->counter > 0) ? 1.0f : 0.0f;
            ImGui::ProgressBar(activity_indicator, ImVec2(-1, 20), 
                shared->counter > 0 ? "Active" : "Waiting for data");
        }

        ImGui::Spacing();
        ImGui::Separator();

        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "🛠️ CONTROL");
        ImGui::Spacing();
        
        if (ImGui::Button("Clear Log Files", ImVec2(150, 30))) {
            ofstream("location_data.json").close();
            ofstream("location_log.txt").close();
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Reset Counter", ImVec2(150, 30))) {
            lock_guard<mutex> lock(shared->data_mutex);
            shared->counter = 0;
            shared->recent_records.clear();
        }

        ImGui::EndChild();
        ImGui::NextColumn();

        // ПРАВАЯ КОЛОНКА - История и графики
        ImGui::BeginChild("HistoryData", ImVec2(0, 0), true);

        if (ImGui::BeginTabBar("DataTabs")) {
            
            if (ImGui::BeginTabItem("📋 History")) {
                ImGui::BeginChild("HistoryList", ImVec2(0, 0), false);
                
                lock_guard<mutex> lock(shared->data_mutex);
                
                if (shared->recent_records.empty()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No records yet...");
                } else {
                    ImGui::Columns(5, "HistoryTable");
                    ImGui::SeparatorText("ID");
                    ImGui::NextColumn();
                    ImGui::SeparatorText("Latitude");
                    ImGui::NextColumn();
                    ImGui::SeparatorText("Longitude");
                    ImGui::NextColumn();
                    ImGui::SeparatorText("Altitude");
                    ImGui::NextColumn();
                    ImGui::SeparatorText("Time");
                    ImGui::NextColumn();
                    ImGui::Separator();
                    
                    int idx = 0;
                    for (auto it = shared->recent_records.rbegin(); it != shared->recent_records.rend(); ++it) {
                        ImGui::Text("%d", shared->counter - idx);
                        ImGui::NextColumn();
                        ImGui::Text("%.4f", (*it)["latitude"].get<float>());
                        ImGui::NextColumn();
                        ImGui::Text("%.4f", (*it)["longitude"].get<float>());
                        ImGui::NextColumn();
                        ImGui::Text("%.1f", (*it).value("altitude", 0.0f));
                        ImGui::NextColumn();
                        ImGui::Text("%s", format_timestamp((*it).value("time", 0LL)).c_str());
                        ImGui::NextColumn();
                        idx++;
                        if (idx >= 20) break;
                    }
                    ImGui::Columns(1);
                }
                
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("📊 Statistics")) {
                lock_guard<mutex> lock(shared->data_mutex);
                
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "SERVER STATISTICS");
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::Text("Total records processed:"); 
                ImGui::SameLine(200); 
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%d", shared->counter);
                
                ImGui::Text("Records in history:"); 
                ImGui::SameLine(200); 
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%d", (int)shared->recent_records.size());
                
                if (shared->recent_records.size() > 1) {
                    ImGui::Spacing();
                    ImGui::SeparatorText("📈 Altitude Trend");
                    
                    vector<float> altitudes;
                    for (const auto& record : shared->recent_records) {
                        altitudes.push_back(record.value("altitude", 0.0f));
                    }
                    
                    ImGui::PlotLines("Altitude", altitudes.data(), altitudes.size(),
                        0, NULL, 0.0f, 100.0f, ImVec2(0, 80));
                }
                
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("ℹ️ Server Info")) {
                ImGui::Spacing();
                
                ImGui::SeparatorText("🔧 CONFIGURATION");
                ImGui::Text("ZeroMQ endpoint:"); 
                ImGui::SameLine(150); 
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "tcp://*:8080");
                
                ImGui::Text("Data files:"); 
                ImGui::SameLine(150); 
                ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "location_data.json");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "location_log.txt");
                
                ImGui::Spacing();
                ImGui::SeparatorText("📝 STATUS");
                ImGui::TextWrapped("Server is running and accepting connections. "
                    "GPS data is being saved to both JSON and TXT formats.");
                
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }

        ImGui::EndChild();
        ImGui::Columns(1);
        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        this_thread::sleep_for(chrono::milliseconds(16));
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
}