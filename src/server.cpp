#include "server.hpp"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <sstream>

#ifndef _WIN32
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#endif

using namespace std;
using namespace zmq;

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

    while (true) {
        message_t msg;
        auto recv_result = socket.recv(msg, recv_flags::none);
        (void)recv_result;
        
        string raw_text(static_cast<char*>(msg.data()), msg.size());
        
        if (raw_text == "ping") {
            socket.send(zmq::buffer("pong"), zmq::send_flags::none);
            continue;
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

            if (received_data.contains("traffic") && !received_data["traffic"].is_null() && !received_data["traffic"].empty()) {
                json traffic_data = received_data["traffic"];
                
                json all_traffic_data;
                ifstream traffic_file("traffic_data.json");
                if (traffic_file.is_open()) {
                    try {
                        all_traffic_data = json::parse(traffic_file);
                    } catch (...) {
                        all_traffic_data = json::array();
                    }
                } else {
                    all_traffic_data = json::array();
                }
                
                json traffic_record;
                traffic_record["mobile_rx_bytes"] = traffic_data.value("mobile_rx_bytes", 0LL);
                traffic_record["mobile_tx_bytes"] = traffic_data.value("mobile_tx_bytes", 0LL);
                traffic_record["total_rx_bytes"] = traffic_data.value("total_rx_bytes", 0LL);
                traffic_record["total_tx_bytes"] = traffic_data.value("total_tx_bytes", 0LL);
                traffic_record["mobile_total_bytes"] = traffic_data.value("mobile_total_bytes", 0LL);
                traffic_record["total_bytes"] = traffic_data.value("total_bytes", 0LL);
                
                if (traffic_data.contains("top_apps") && !traffic_data["top_apps"].is_null()) {
                    traffic_record["top_apps"] = traffic_data["top_apps"];
                }
                
                traffic_record["timestamp"] = received_data.value("timestamp", 0LL);
                all_traffic_data.push_back(traffic_record);
                
                ofstream traffic_file_out("traffic_data.json");
                traffic_file_out << all_traffic_data.dump(4);
            }

            string response = "OK:" + to_string(shared->counter);
            socket.send(zmq::buffer(response), zmq::send_flags::none);
            cout << "Данные #" << shared->counter << " сохранены" << endl;
            
        } catch (const exception& e) {
            cerr << "ERROR: " << e.what() << endl;
            socket.send(zmq::buffer("ERROR"), zmq::send_flags::none);
        }
        
        cout << "=====================================" << endl;
    }
}