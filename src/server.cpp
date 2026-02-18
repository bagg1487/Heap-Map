#include "server.hpp"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

using namespace std;
using namespace zmq;

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
                
                shared->recent_records.push_back(gps_data);
                if (shared->recent_records.size() > shared->max_history) {
                    shared->recent_records.pop_front();
                }
            }
            
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