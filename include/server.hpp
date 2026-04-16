#pragma once
#include <deque>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct SharedData {
    std::deque<json> recent_records;
    std::mutex data_mutex;
    int counter = 0;
    int max_history = 1000;
    
    // Filters
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