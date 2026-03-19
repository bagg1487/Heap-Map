#pragma once
#include <mutex>
#include <deque>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

struct SharedData {
    std::mutex data_mutex;
    std::deque<json> recent_records;
    int counter = 0;
    int max_history = 1000;
    
    bool filter_location = true;
    bool filter_telephony = true;
    bool filter_traffic = true;
    bool filter_lte = true;
    bool filter_gsm = true;
    bool filter_wcdma = true;
};

void run_server(SharedData* shared);
void run_gui(SharedData* shared);
std::string get_local_ip();