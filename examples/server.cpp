#include <zmq.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <vector>
#include <deque>
#include <chrono>
#include <ctime>

// Dear ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

using namespace std;
using namespace zmq;
using json = nlohmann::json;

struct Location {
    float latitude = 0;
    float longitude = 0;
    float altitude = 0;
    long long timestamp = 0;
};

struct SharedData {
    Location current_location;
    mutex data_mutex;
    int counter = 0;
    deque<json> recent_records; // для истории
    const size_t max_history = 100;
};

// Функция для форматирования timestamp
string format_timestamp(long long timestamp) {
    if (timestamp == 0) return "N/A";
    time_t time = timestamp;
    char buffer[30];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&time));
    return string(buffer);
}

// GUI поток
void run_gui(SharedData* shared) {
    // Инициализация GLFW
    if (!glfwInit()) {
        cerr << "Failed to initialize GLFW" << endl;
        return;
    }

    // Создание окна
    GLFWwindow* window = glfwCreateWindow(1280, 720, "GPS Server Monitor", NULL, NULL);
    if (!window) {
        cerr << "Failed to create window" << endl;
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // V-Sync

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

    // Цвета для графиков
    ImVec4 color_good = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
    ImVec4 color_warning = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);

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
            
            // Метрики в стиле дашборда
            ImGui::Text("Total Records: "); 
            ImGui::SameLine(); 
            ImGui::TextColored(color_good, "%d", shared->counter);

            ImGui::Spacing();
            
            // Карточка с координатами
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

            // Индикатор активности
            ImGui::Spacing();
            ImGui::SeparatorText("⚡ ACTIVITY");
            
            float activity_indicator = (shared->counter > 0) ? 1.0f : 0.0f;
            ImGui::ProgressBar(activity_indicator, ImVec2(-1, 20), 
                shared->counter > 0 ? "Active" : "Waiting for data");
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Кнопки управления
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "🛠️ CONTROL");
        ImGui::Spacing();
        
        if (ImGui::Button("Clear Log Files", ImVec2(150, 30))) {
            // Очистка лог-файлов
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

        // Табы для разных представлений
        if (ImGui::BeginTabBar("DataTabs")) {
            
            // Вкладка истории
            if (ImGui::BeginTabItem("📋 History")) {
                ImGui::BeginChild("HistoryList", ImVec2(0, 0), false);
                
                lock_guard<mutex> lock(shared->data_mutex);
                
                if (shared->recent_records.empty()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No records yet...");
                } else {
                    // Заголовки таблицы
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
                    
                    // Данные
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
                        if (idx >= 20) break; // Показываем последние 20 записей
                    }
                    ImGui::Columns(1);
                }
                
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // Вкладка статистики
            if (ImGui::BeginTabItem("📊 Statistics")) {
                lock_guard<mutex> lock(shared->data_mutex);
                
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "SERVER STATISTICS");
                ImGui::Separator();
                ImGui::Spacing();

                // Метрики
                ImGui::Text("Total records processed:"); 
                ImGui::SameLine(200); 
                ImGui::TextColored(color_good, "%d", shared->counter);
                
                ImGui::Text("Records in history:"); 
                ImGui::SameLine(200); 
                ImGui::TextColored(color_good, "%d", (int)shared->recent_records.size());
                
                // График высоты (псевдо-график)
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

            // Вкладка информации о сервере
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

        // Рендеринг ImGui
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        this_thread::sleep_for(chrono::milliseconds(16)); // ~60 FPS
    }

    // Очистка ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}

// Модифицированный сервер для сохранения истории
void run_server(SharedData* shared) {
    context_t context(1);
    socket_t socket(context, socket_type::rep);
    socket.bind("tcp://*:8080");
    
    string json_filename = "location_data.json";
    string txt_filename = "location_log.txt";
    
    cout << "Server started on port 8080" << endl;
    
    while (true) {
        message_t msg;
        (void)socket.recv(msg, recv_flags::none);
        string raw_text((char*)msg.data(), msg.size());
        
        try {
            json gps_data = json::parse(raw_text);
            
            if (!gps_data.contains("latitude") || !gps_data.contains("longitude")) {
                string error_msg = "ERROR:No lat/lon";
                message_t reply(error_msg.size());
                memcpy(reply.data(), error_msg.data(), error_msg.size());
                socket.send(reply, send_flags::none);
                continue;
            }
            
            {
                lock_guard<mutex> lock(shared->data_mutex);
                shared->current_location.latitude = gps_data["latitude"];
                shared->current_location.longitude = gps_data["longitude"];
                shared->current_location.altitude = gps_data.value("altitude", 0.0f);
                shared->current_location.timestamp = gps_data.value("time", 0LL);
                shared->counter++;
                
                // Сохраняем в историю
                shared->recent_records.push_back(gps_data);
                if (shared->recent_records.size() > shared->max_history) {
                    shared->recent_records.pop_front();
                }
            }
            
            // Сохраняем в файлы
            json all_data;
            ifstream json_file(json_filename);
            if (json_file.is_open()) {
                try {
                    all_data = json::parse(json_file);
                } catch (...) {
                    all_data = json::array();
                }
            } else {
                all_data = json::array();
            }
            
            all_data.push_back(gps_data);
            
            ofstream json_file_out(json_filename);
            json_file_out << all_data.dump(4);
            
            ofstream txt_file(txt_filename, ios::app);
            txt_file << "Record #" << shared->counter << ":\n";
            txt_file << "  Latitude: " << gps_data["latitude"] << "\n";
            txt_file << "  Longitude: " << gps_data["longitude"] << "\n";
            txt_file << "  Altitude: " << gps_data.value("altitude", 0.0) << "\n";
            txt_file << "  Time: " << gps_data.value("time", 0) << "\n";
            txt_file << "---\n";

            string response = "OK:" + to_string(shared->counter);
            message_t reply(response.size());
            memcpy(reply.data(), response.data(), response.size());
            socket.send(reply, send_flags::none);
            
        } catch (const json::parse_error& e) {
            string error_msg = "ERROR:Invalid JSON";
            message_t reply(error_msg.size());
            memcpy(reply.data(), error_msg.data(), error_msg.size());
            socket.send(reply, send_flags::none);
            
        } catch (const exception& e) {
            string error_msg = "ERROR:" + string(e.what());
            message_t reply(error_msg.size());
            memcpy(reply.data(), error_msg.data(), error_msg.size());
            socket.send(reply, send_flags::none);
        }
    }
}

int main() {
    SharedData shared;
    
    thread gui_thread(run_gui, &shared);
    thread server_thread(run_server, &shared);
    
    gui_thread.join();
    server_thread.join();
    
    return 0;
}