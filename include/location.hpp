#pragma once
#include <deque>
#include <mutex>
#include <nlohmann/json.hpp>
using namespace std;

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
    deque<json> recent_records; 
    const size_t max_history = 100;
};

string format_timestamp(long long timestamp);