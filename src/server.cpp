#include "server.hpp"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <pqxx/pqxx>

#ifndef _WIN32
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#endif

using namespace std;
using namespace zmq;
namespace fs = std::filesystem;
using json = nlohmann::json;

pqxx::connection* db_conn = nullptr;

string get_local_ip() {
    string ip = "127.0.0.1";
    
#ifndef _WIN32
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

void save_to_db(const json& data) {
    if (!db_conn) return;
    
    try {
        pqxx::work txn(*db_conn);
        
        long long timestamp = data.value("timestamp", 0LL);
        string imei = data.value("imei", "");
        
        pqxx::result res = txn.exec_params(
            "INSERT INTO measurements (timestamp, imei) VALUES ($1, $2) RETURNING id",
            timestamp, imei
        );
        int measurement_id = res[0][0].as<int>();
        
        if (data.contains("location") && !data["location"].is_null()) {
            auto& loc = data["location"];
            txn.exec_params(
                "INSERT INTO locations (measurement_id, latitude, longitude, altitude, accuracy, speed) "
                "VALUES ($1, $2, $3, $4, $5, $6)",
                measurement_id,
                loc.value("latitude", 0.0),
                loc.value("longitude", 0.0),
                loc.value("altitude", 0.0),
                loc.value("accuracy", 0.0),
                loc.value("speed", 0.0)
            );
        }
        
        if (data.contains("telephony") && !data["telephony"].is_null()) {
            for (auto& [key, cell] : data["telephony"].items()) {
                if (cell.is_object()) {
                    txn.exec_params(
                        "INSERT INTO cells (measurement_id, type, dbm, rsrp, pci, tac, mcc, mnc, ci, earfcn) "
                        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)",
                        measurement_id,
                        cell.value("type", ""),
                        cell.value("dbm", 0),
                        cell.value("rsrp", 0),
                        cell.value("pci", 0),
                        cell.value("tac", cell.value("lac", 0)),
                        cell.value("mcc", 0),
                        cell.value("mnc", 0),
                        cell.value("ci", cell.value("cid", 0LL)),
                        cell.value("earfcn", 0)
                    );
                }
            }
        }
        
        if (data.contains("traffic") && !data["traffic"].is_null()) {
            auto& traffic = data["traffic"];
            txn.exec_params(
                "INSERT INTO traffic (measurement_id, mobile_rx, mobile_tx, total_rx, total_tx) "
                "VALUES ($1, $2, $3, $4, $5)",
                measurement_id,
                traffic.value("mobile_rx_bytes", 0LL),
                traffic.value("mobile_tx_bytes", 0LL),
                traffic.value("total_rx_bytes", 0LL),
                traffic.value("total_tx_bytes", 0LL)
            );
        }
        
        txn.commit();
        cout << "Saved to DB, id: " << measurement_id << endl;
        
    } catch (const exception& e) {
        cerr << "DB error: " << e.what() << endl;
    }
}

void run_http_server(SharedData* shared) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return;
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8081);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        cerr << "HTTP server bind failed" << endl;
        close(server_fd);
        return;
    }
    
    if (listen(server_fd, 3) < 0) {
        cerr << "HTTP server listen failed" << endl;
        close(server_fd);
        return;
    }
    
    cout << "HTTP server started on port 8081" << endl;
    
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) continue;
        
        char buffer[4096] = {0};
        read(client_fd, buffer, sizeof(buffer) - 1);
        
        string request(buffer);
        string response;
        string content_type = "text/html";
        
        if (request.find("GET /") != string::npos) {
            string path = "/";
            size_t path_start = request.find("GET ") + 4;
            size_t path_end = request.find(" ", path_start);
            if (path_end != string::npos) {
                path = request.substr(path_start, path_end - path_start);
            }
            
            if (path == "/" || path == "/heatmap.html") {
                ifstream file("heatmap.html");
                if (file.is_open()) {
                    stringstream ss;
                    ss << file.rdbuf();
                    response = ss.str();
                    content_type = "text/html";
                } else {
                    response = "<html><body><h1>heatmap.html not found</h1></body></html>";
                }
            }
            else if (path == "/generate_heatmap") {
                system("python3 generate_heatmap.py");
                response = "{\"status\": \"generated\"}";
                content_type = "application/json";
            }
            else if (path == "/data/all_data.json") {
                ifstream file("data/all_data.json");
                if (file.is_open()) {
                    stringstream ss;
                    ss << file.rdbuf();
                    response = ss.str();
                    content_type = "application/json";
                } else {
                    response = "[]";
                    content_type = "application/json";
                }
            }
            else if (path == "/data/location_danil.json") {
                ifstream file("data/location_danil.json");
                if (file.is_open()) {
                    stringstream ss;
                    ss << file.rdbuf();
                    response = ss.str();
                    content_type = "application/json";
                } else {
                    response = "[]";
                }
            }
            else if (path == "/data/locations.json") {
                ifstream file("data/locations.json");
                if (file.is_open()) {
                    stringstream ss;
                    ss << file.rdbuf();
                    response = ss.str();
                    content_type = "application/json";
                } else {
                    response = "[]";
                }
            }
            else {
                response = "<html><body><h1>404 Not Found</h1></body></html>";
            }
            
            string http_response = "HTTP/1.1 200 OK\r\n";
            http_response += "Content-Type: " + content_type + "\r\n";
            http_response += "Access-Control-Allow-Origin: *\r\n";
            http_response += "Content-Length: " + to_string(response.length()) + "\r\n";
            http_response += "Connection: close\r\n\r\n";
            http_response += response;
            
            send(client_fd, http_response.c_str(), http_response.length(), 0);
        }
        
        close(client_fd);
    }
    
    close(server_fd);
}

void run_server(SharedData* shared) {
    try {
        db_conn = new pqxx::connection("dbname=cellmap user=postgres password=postgres host=localhost port=5434");
        if (db_conn->is_open()) {
            cout << "Connected to PostgreSQL" << endl;
        } else {
            cerr << "Failed to connect to PostgreSQL" << endl;
            delete db_conn;
            db_conn = nullptr;
        }
    } catch (const exception& e) {
        cerr << "DB connection error: " << e.what() << endl;
        db_conn = nullptr;
    }
    
    thread http_thread(run_http_server, shared);
    http_thread.detach();
    
    context_t context(1);
    socket_t socket(context, socket_type::rep);
    socket.bind("tcp://*:8080");

    string server_ip = get_local_ip();
    
    cout << "=====================================" << endl;
    cout << "ZMQ Server started on " << server_ip << ":8080" << endl;
    cout << "HTTP Server started on " << server_ip << ":8081" << endl;
    cout << "Open browser: http://" << server_ip << ":8081/heatmap.html" << endl;
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
            response_stream << "Last 10 records:\n";
            
            {
                lock_guard<mutex> lock(shared->data_mutex);
                if (shared->recent_records.empty()) {
                    response_stream << "No data\n";
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
            cout << "PHONE CONNECTED" << endl;
            phone_connected = true;
        }
        
        cout << "Received: " << raw_text << endl;
        
        try {
            json received_data = json::parse(raw_text);
            
            save_to_db(received_data);
            
            {
                lock_guard<mutex> lock(shared->data_mutex);
                shared->recent_records.push_back(received_data);
                if (shared->recent_records.size() > shared->max_history) {
                    shared->recent_records.pop_front();
                }
                shared->counter++;
            }

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
            
            if (received_data.contains("location")) {
                json location_only;
                location_only["timestamp"] = received_data.value("timestamp", 0LL);
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
            
            if (received_data.contains("telephony")) {
                json telephony_only;
                telephony_only["timestamp"] = received_data.value("timestamp", 0LL);
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
            
            if (received_data.contains("traffic")) {
                json traffic_only;
                traffic_only["timestamp"] = received_data.value("timestamp", 0LL);
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
            cout << "Data #" << shared->counter << " saved" << endl;
            
        } catch (const exception& e) {
            cerr << "ERROR: " << e.what() << endl;
            socket.send(zmq::buffer("ERROR"), zmq::send_flags::none);
        }
        
    }
}