#include "server.hpp"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <sstream>
#include <filesystem>

#ifndef _WIN32
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#endif

using namespace std;
using namespace zmq;
namespace fs = std::filesystem;

string get_local_ip() {
    string ip = "127.0.0.1";
    
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    struct hostent* host = gethostbyname(hostname);
    if (host) {
        for (int i = 0; host->h_addr_list[i] != nullptr; i++) {
            struct in_addr addr;
            addr.s_addr = *(u_long*)host->h_addr_list[i];
            char* ip_str = inet_ntoa(addr);
            if (strcmp(ip_str, "127.0.0.1") != 0) {
                ip = ip_str;
                break;
            }
        }
    }
    WSACleanup();
#else
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
                if (strcmp(ifa->ifa_name, "lo") == 0) continue;
                if (strstr(ifa->ifa_name, "docker") != nullptr) continue;
                if (strstr(ifa->ifa_name, "veth") != nullptr) continue;
                
                void* addr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
                char host[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, addr, host, sizeof(host));

                if (strcmp(host, "127.0.0.1") != 0) {
                    ip = host;
                    break;
                }
            }
        }
        freeifaddrs(ifaddr);
    }
#endif
    
    return ip;
}

void run_server(SharedData* shared) {
    context_t context(1);
    socket_t socket(context, socket_type::rep);
    socket.bind("tcp://*:8080");

    string server_ip = get_local_ip();
    
    cout << "=====================================" << endl;
    cout << "Сервер запущен на " << server_ip << ":8080" << endl;
    cout << "=====================================" << endl;
 
    bool phone_connected = false;
    
    fs::create_directory("data");

    while (true) {
        message_t msg;
        auto recv_result = socket.recv(msg, recv_flags::none);
        (void)recv_result;
        
        string raw_text(static_cast<char*>(msg.data()), msg.size());
        
        if (raw_text == "ping") {
            socket.send(zmq::buffer("pong"), zmq::send_flags::none);
            continue;
        }
        
        if (raw_text.find("filter") != string::npos) {
            try {
                json cmd = json::parse(raw_text);
                if (cmd["type"] == "filter") {
                    string filter_name = cmd["filter"];
                    bool value = cmd["value"];
                    
                    if (filter_name == "location") shared->filter_location = value;
                    else if (filter_name == "telephony") shared->filter_telephony = value;
                    else if (filter_name == "traffic") shared->filter_traffic = value;
                    else if (filter_name == "lte") shared->filter_lte = value;
                    else if (filter_name == "gsm") shared->filter_gsm = value;
                    else if (filter_name == "wcdma") shared->filter_wcdma = value;
                    
                    cout << "Filter updated: " << filter_name << " = " << value << endl;
                    socket.send(zmq::buffer("OK"), zmq::send_flags::none);
                    continue;
                }
            } catch (...) {
                socket.send(zmq::buffer("ERROR"), zmq::send_flags::none);
                continue;
            }
        }
        
        if (raw_text == "show") {
            stringstream response_stream;
            response_stream << "Последние данные:\n";
            
            {
                lock_guard<mutex> lock(shared->data_mutex);
                if (shared->recent_records.empty()) {
                    response_stream << "Нет данных\n";
                } else {
                    int count = 0;
                    for (auto it = shared->recent_records.rbegin(); it != shared->recent_records.rend() && count < 10; ++it, ++count) {
                        response_stream << count + 1 << ". " << it->dump() << "\n";
                    }
                }
            }
            
            socket.send(zmq::buffer(response_stream.str()), zmq::send_flags::none);
            continue;
        }
        
        if (!phone_connected) {
            cout << "ТЕЛЕФОН ПОДКЛЮЧЕН" << endl;
            phone_connected = true;
        }
        
        cout << "Получено: " << raw_text << endl;
        
        try {
            json received_data = json::parse(raw_text);
            
            {
                lock_guard<mutex> lock(shared->data_mutex);
                shared->recent_records.push_back(received_data);
                if (shared->recent_records.size() > shared->max_history) {
                    shared->recent_records.pop_front();
                }
                shared->counter++;
            }

            // Сохраняем все данные в один файл
            json all_data;
            string all_data_file = "data/all_data.json";
            ifstream all_file(all_data_file);
            if (all_file.is_open()) {
                try {
                    all_data = json::parse(all_file);
                } catch (...) {
                    all_data = json::array();
                }
            } else {
                all_data = json::array();
            }
            
            all_data.push_back(received_data);
            
            ofstream all_file_out(all_data_file);
            all_file_out << all_data.dump(4);
            
            // Сохраняем каждый пакет в отдельный файл с таймстемпом
            long long timestamp = received_data.value("timestamp", 0LL);
            string filename = "data/data_" + to_string(timestamp) + ".json";
            ofstream single_file_out(filename);
            single_file_out << received_data.dump(4);
            
            // Сохраняем только локации
            if (received_data.contains("location")) {
                json location_only;
                location_only["timestamp"] = timestamp;
                location_only["location"] = received_data["location"];
                
                json all_locations;
                string loc_file = "data/locations.json";
                ifstream loc_file_in(loc_file);
                if (loc_file_in.is_open()) {
                    try {
                        all_locations = json::parse(loc_file_in);
                    } catch (...) {
                        all_locations = json::array();
                    }
                } else {
                    all_locations = json::array();
                }
                
                all_locations.push_back(location_only);
                
                ofstream loc_file_out(loc_file);
                loc_file_out << all_locations.dump(4);
            }
            
            // Сохраняем только данные сотовых вышек
            if (received_data.contains("telephony")) {
                json telephony_only;
                telephony_only["timestamp"] = timestamp;
                telephony_only["telephony"] = received_data["telephony"];
                
                json all_telephony;
                string tele_file = "data/telephony.json";
                ifstream tele_file_in(tele_file);
                if (tele_file_in.is_open()) {
                    try {
                        all_telephony = json::parse(tele_file_in);
                    } catch (...) {
                        all_telephony = json::array();
                    }
                } else {
                    all_telephony = json::array();
                }
                
                all_telephony.push_back(telephony_only);
                
                ofstream tele_file_out(tele_file);
                tele_file_out << all_telephony.dump(4);
            }
            
            // Сохраняем только данные трафика
            if (received_data.contains("traffic")) {
                json traffic_only;
                traffic_only["timestamp"] = timestamp;
                traffic_only["traffic"] = received_data["traffic"];
                
                json all_traffic;
                string traffic_file = "data/traffic.json";
                ifstream traffic_file_in(traffic_file);
                if (traffic_file_in.is_open()) {
                    try {
                        all_traffic = json::parse(traffic_file_in);
                    } catch (...) {
                        all_traffic = json::array();
                    }
                } else {
                    all_traffic = json::array();
                }
                
                all_traffic.push_back(traffic_only);
                
                ofstream traffic_file_out(traffic_file);
                traffic_file_out << all_traffic.dump(4);
            }

            string response = "OK:" + to_string(shared->counter);
            socket.send(zmq::buffer(response), zmq::send_flags::none);
            cout << "Данные #" << shared->counter << " сохранены в папку data/" << endl;
            
        } catch (const exception& e) {
            cerr << "ERROR: " << e.what() << endl;
            socket.send(zmq::buffer("ERROR"), zmq::send_flags::none);
        }
        
        cout << "=====================================" << endl;
    }
}