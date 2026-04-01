#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <cmath>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include "heatmap.hpp"

using namespace std;
using json = nlohmann::json;
namespace fs = std::filesystem;

vector<json> load_json_file(const string& filename) {
    vector<json> result;
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Cannot open file: " << filename << endl;
        return result;
    }
    
    try {
        json data = json::parse(file);
        if (data.is_array()) {
            for (const auto& item : data) {
                result.push_back(item);
            }
        } else {
            result.push_back(data);
        }
    } catch (const exception& e) {
        cerr << "Error parsing " << filename << ": " << e.what() << endl;
    }
    return result;
}

vector<DataPoint> load_all_data() {
    vector<DataPoint> points;
    
    vector<json> all_data = load_json_file("data/all_data.json");
    vector<json> location_data = load_json_file("data/location_danil.json");
    
    for (const auto& item : all_data) {
        DataPoint point;
        point.has_location = false;
        point.has_signal = false;
        point.rsrp = -200;
        point.dbm = -200;
        
        if (item.contains("location")) {
            auto loc = item["location"];
            if (loc.contains("latitude") && loc.contains("longitude")) {
                point.lat = loc["latitude"];
                point.lon = loc["longitude"];
                point.has_location = true;
            }
        }
        
        if (item.contains("telephony")) {
            for (auto& [key, cell] : item["telephony"].items()) {
                if (cell.contains("rsrp")) {
                    int rsrp = cell["rsrp"];
                    if (rsrp > point.rsrp) {
                        point.rsrp = rsrp;
                        point.has_signal = true;
                        if (cell.contains("type")) point.type = cell["type"];
                    }
                }
                if (cell.contains("dbm")) {
                    int dbm = cell["dbm"];
                    if (dbm > point.dbm) {
                        point.dbm = dbm;
                        point.has_signal = true;
                    }
                }
            }
        }
        
        point.timestamp = item.value("timestamp", 0LL);
        
        if (point.has_location) {
            points.push_back(point);
        }
    }
    
    for (const auto& item : location_data) {
        DataPoint point;
        point.has_location = false;
        point.has_signal = false;
        point.rsrp = -200;
        point.dbm = -200;
        
        if (item.contains("latitude") && item.contains("longitude")) {
            point.lat = item["latitude"];
            point.lon = item["longitude"];
            point.has_location = true;
        }
        
        if (item.contains("cellInfo")) {
            string cellInfo = item["cellInfo"];
            size_t rssi_pos = cellInfo.find("rssi=");
            if (rssi_pos != string::npos) {
                string rssi_str = cellInfo.substr(rssi_pos + 5);
                int rssi_val = 0;
                bool negative = false;
                for (char ch : rssi_str) {
                    if (ch == '-') {
                        negative = true;
                    } else if (isdigit(ch)) {
                        rssi_val = rssi_val * 10 + (ch - '0');
                    } else {
                        break;
                    }
                }
                if (negative) rssi_val = -rssi_val;
                point.dbm = rssi_val;
                point.has_signal = true;
                point.type = "GSM";
            }
        }
        
        point.timestamp = item.value("timestamp", 0LL);
        
        if (point.has_location) {
            points.push_back(point);
        }
    }
    
    return points;
}

void generate_python_heatmap_script(const vector<DataPoint>& points, const string& output_file) {
    ofstream script("generate_heatmap.py");
    
    script << "#!/usr/bin/env python3\n";
    script << "import json\n";
    script << "from PIL import Image, ImageDraw, ImageFont\n";
    script << "import math\n\n";
    
    script << "def get_color(value, max_val):\n";
    script << "    if max_val <= 0:\n";
    script << "        return (0, 0, 255)\n";
    script << "    ratio = value / max_val\n";
    script << "    if ratio < 0.2: return (0, 0, 255)\n";
    script << "    if ratio < 0.4: return (0, 255, 255)\n";
    script << "    if ratio < 0.6: return (0, 255, 0)\n";
    script << "    if ratio < 0.8: return (255, 255, 0)\n";
    script << "    return (255, 0, 0)\n\n";
    
    script << "points = [\n";
    int point_count = 0;
    for (const auto& p : points) {
        if (p.has_location) {
            int signal = (p.rsrp > -140) ? p.rsrp : p.dbm;
            script << "    (" << p.lat << ", " << p.lon << ", " << signal << "),\n";
            point_count++;
        }
    }
    script << "]\n\n";
    
    script << "if not points:\n";
    script << "    print('No points to plot')\n";
    script << "    exit()\n\n";
    
    script << "lats = [p[0] for p in points]\n";
    script << "lons = [p[1] for p in points]\n";
    script << "signals = [p[2] for p in points]\n\n";
    
    script << "min_lat, max_lat = min(lats), max(lats)\n";
    script << "min_lon, max_lon = min(lons), max(lons)\n";
    script << "min_signal, max_signal = min(signals), max(signals)\n\n";
    
    script << "print(f'Points: {len(points)}')\n";
    script << "print(f'Lat range: {min_lat:.6f} to {max_lat:.6f}')\n";
    script << "print(f'Lon range: {min_lon:.6f} to {max_lon:.6f}')\n";
    script << "print(f'Signal range: {min_signal} to {max_signal} dBm')\n\n";
    
    script << "width, height = 1200, 800\n";
    script << "img = Image.new('RGB', (width, height), (30, 30, 40))\n";
    script << "draw = ImageDraw.Draw(img)\n\n";
    
    script << "def lat_to_y(lat):\n";
    script << "    if max_lat == min_lat:\n";
    script << "        return height // 2\n";
    script << "    return int(height - (lat - min_lat) / (max_lat - min_lat) * height)\n\n";
    
    script << "def lon_to_x(lon):\n";
    script << "    if max_lon == min_lon:\n";
    script << "        return width // 2\n";
    script << "    return int((lon - min_lon) / (max_lon - min_lon) * width)\n\n";
    
    script << "radius = 6\n";
    script << "good_signals = 0\n";
    script << "poor_signals = 0\n\n";
    
    script << "for lat, lon, signal in points:\n";
    script << "    x = lon_to_x(lon)\n";
    script << "    y = lat_to_y(lat)\n";
    script << "    if signal > -140:\n";
    script << "        color_val = (signal - min_signal) / (max_signal - min_signal) if max_signal > min_signal else 0.5\n";
    script << "        color = get_color(color_val, 1.0)\n";
    script << "        if signal >= -80:\n";
    script << "            good_signals += 1\n";
    script << "        else:\n";
    script << "            poor_signals += 1\n";
    script << "    else:\n";
    script << "        color = (0, 100, 200)\n";
    script << "    draw.ellipse([x - radius, y - radius, x + radius, y + radius], fill=color, outline=(255, 255, 255))\n\n";
    
    script << "draw.rectangle([0, 0, width, 35], fill=(0, 0, 0))\n";
    script << "draw.rectangle([0, height - 25, width, height], fill=(0, 0, 0))\n\n";
    
    script << "try:\n";
    script << "    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 14)\n";
    script << "except:\n";
    script << "    font = ImageFont.load_default()\n\n";
    
    script << "stats_text = f'Points: {len(points)} | Signal range: {min_signal} to {max_signal} dBm | Good: {good_signals} | Poor: {poor_signals}'\n";
    script << "draw.text((10, 10), stats_text, fill=(255, 255, 255), font=font)\n\n";
    
    script << "draw.text((10, height - 22), 'Excellent (-80)', fill=(0, 255, 0), font=font)\n";
    script << "draw.text((180, height - 22), 'Good (-90)', fill=(0, 255, 255), font=font)\n";
    script << "draw.text((330, height - 22), 'Fair (-100)', fill=(0, 255, 0), font=font)\n";
    script << "draw.text((480, height - 22), 'Poor (-110)', fill=(255, 255, 0), font=font)\n";
    script << "draw.text((630, height - 22), 'Very Poor', fill=(255, 0, 0), font=font)\n";
    script << "draw.text((780, height - 22), 'No Signal', fill=(0, 100, 200), font=font)\n\n";
    
    script << "img.save('" << output_file << "')\n";
    script << "print(f'Heatmap saved to " << output_file << "')\n";
    
    script.close();
    
    cout << "Python script generated: generate_heatmap.py" << endl;
    int ret = system("python3 generate_heatmap.py");
    if (ret != 0) {
        cerr << "Failed to run python script. Make sure Pillow is installed: pip install pillow" << endl;
    }
}

void generate_signal_graphs(const vector<DataPoint>& points, const string& output_dir) {
    vector<long long> timestamps;
    vector<int> signal_values;
    
    for (const auto& p : points) {
        if (p.timestamp > 0 && (p.rsrp > -140 || p.dbm > -140)) {
            timestamps.push_back(p.timestamp);
            int signal = (p.rsrp > -140) ? p.rsrp : p.dbm;
            signal_values.push_back(signal);
        }
    }
    
    if (timestamps.empty()) {
        cout << "No signal data found" << endl;
        return;
    }
    
    fs::create_directory(output_dir);
    
    ofstream html_file(output_dir + "/signal_graph.html");
    
    html_file << "<!DOCTYPE html>\n";
    html_file << "<html>\n";
    html_file << "<head>\n";
    html_file << "    <title>Signal Graphs</title>\n";
    html_file << "    <script src=\"https://cdn.plot.ly/plotly-latest.min.js\"></script>\n";
    html_file << "    <style>\n";
    html_file << "        body { font-family: monospace; background: #1e1e2e; color: #fff; margin: 0; padding: 20px; }\n";
    html_file << "        .graph { margin-bottom: 30px; }\n";
    html_file << "        h2 { color: #0f0; }\n";
    html_file << "    </style>\n";
    html_file << "</head>\n";
    html_file << "<body>\n";
    html_file << "    <h2>Signal Strength Over Time</h2>\n";
    html_file << "    <div id=\"signal-graph\" class=\"graph\"></div>\n";
    html_file << "    <script>\n";
    html_file << "        var timestamps = [";
    
    for (size_t i = 0; i < timestamps.size(); i++) {
        html_file << timestamps[i];
        if (i < timestamps.size() - 1) html_file << ",";
    }
    
    html_file << "];\n";
    html_file << "        var signalValues = [";
    
    for (size_t i = 0; i < signal_values.size(); i++) {
        html_file << signal_values[i];
        if (i < signal_values.size() - 1) html_file << ",";
    }
    
    html_file << "];\n";
    html_file << "        var timeStrings = timestamps.map(ts => new Date(ts).toLocaleString());\n";
    html_file << "        var trace = {\n";
    html_file << "            x: timeStrings,\n";
    html_file << "            y: signalValues,\n";
    html_file << "            mode: 'lines+markers',\n";
    html_file << "            name: 'Signal (dBm)',\n";
    html_file << "            line: { color: '#00FF00', width: 2 },\n";
    html_file << "            marker: { size: 4, color: '#00FF00' }\n";
    html_file << "        };\n";
    html_file << "        var layout = {\n";
    html_file << "            title: 'Signal Strength Over Time',\n";
    html_file << "            xaxis: { title: 'Time', tickangle: -45 },\n";
    html_file << "            yaxis: { title: 'dBm', range: [-120, -40] },\n";
    html_file << "            plot_bgcolor: '#2d2d3a',\n";
    html_file << "            paper_bgcolor: '#1e1e2e',\n";
    html_file << "            font: { color: '#fff' }\n";
    html_file << "        };\n";
    html_file << "        Plotly.newPlot('signal-graph', [trace], layout);\n";
    html_file << "    </script>\n";
    html_file << "</body>\n";
    html_file << "</html>";
    
    html_file.close();
    cout << "Signal graph generated: " << output_dir << "/signal_graph.html" << endl;
}

void generate_traffic_graphs(const string& output_dir) {
    vector<json> traffic_data = load_json_file("data/traffic.json");
    
    vector<long long> timestamps;
    vector<double> rx_mb;
    vector<double> tx_mb;
    
    for (const auto& item : traffic_data) {
        long long ts = item.value("timestamp", 0LL);
        if (ts > 0) {
            long long rx = 0, tx = 0;
            if (item.contains("traffic")) {
                auto traffic = item["traffic"];
                rx = traffic.value("total_rx_bytes", traffic.value("total_rx", 0LL));
                tx = traffic.value("total_tx_bytes", traffic.value("total_tx", 0LL));
            } else {
                rx = item.value("total_rx_bytes", item.value("total_rx", 0LL));
                tx = item.value("total_tx_bytes", item.value("total_tx", 0LL));
            }
            
            timestamps.push_back(ts);
            rx_mb.push_back(rx / 1024.0 / 1024.0);
            tx_mb.push_back(tx / 1024.0 / 1024.0);
        }
    }
    
    if (timestamps.empty()) {
        cout << "No traffic data found" << endl;
        return;
    }
    
    fs::create_directory(output_dir);
    
    ofstream html_file(output_dir + "/traffic_graph.html");
    
    html_file << "<!DOCTYPE html>\n";
    html_file << "<html>\n";
    html_file << "<head>\n";
    html_file << "    <title>Traffic Graphs</title>\n";
    html_file << "    <script src=\"https://cdn.plot.ly/plotly-latest.min.js\"></script>\n";
    html_file << "    <style>\n";
    html_file << "        body { font-family: monospace; background: #1e1e2e; color: #fff; margin: 0; padding: 20px; }\n";
    html_file << "        .graph { margin-bottom: 30px; }\n";
    html_file << "        h2 { color: #0f0; }\n";
    html_file << "    </style>\n";
    html_file << "</head>\n";
    html_file << "<body>\n";
    html_file << "    <h2>Network Traffic Over Time</h2>\n";
    html_file << "    <div id=\"traffic-graph\" class=\"graph\"></div>\n";
    html_file << "    <script>\n";
    html_file << "        var timestamps = [";
    
    for (size_t i = 0; i < timestamps.size(); i++) {
        html_file << timestamps[i];
        if (i < timestamps.size() - 1) html_file << ",";
    }
    
    html_file << "];\n";
    html_file << "        var rxValues = [";
    
    for (size_t i = 0; i < rx_mb.size(); i++) {
        html_file << rx_mb[i];
        if (i < rx_mb.size() - 1) html_file << ",";
    }
    
    html_file << "];\n";
    html_file << "        var txValues = [";
    
    for (size_t i = 0; i < tx_mb.size(); i++) {
        html_file << tx_mb[i];
        if (i < tx_mb.size() - 1) html_file << ",";
    }
    
    html_file << "];\n";
    html_file << "        var timeStrings = timestamps.map(ts => new Date(ts).toLocaleString());\n";
    html_file << "        var trace1 = {\n";
    html_file << "            x: timeStrings,\n";
    html_file << "            y: rxValues,\n";
    html_file << "            mode: 'lines+markers',\n";
    html_file << "            name: 'RX (MB)',\n";
    html_file << "            line: { color: '#00FF00', width: 2 },\n";
    html_file << "            marker: { size: 4, color: '#00FF00' }\n";
    html_file << "        };\n";
    html_file << "        var trace2 = {\n";
    html_file << "            x: timeStrings,\n";
    html_file << "            y: txValues,\n";
    html_file << "            mode: 'lines+markers',\n";
    html_file << "            name: 'TX (MB)',\n";
    html_file << "            line: { color: '#FFA500', width: 2 },\n";
    html_file << "            marker: { size: 4, color: '#FFA500' }\n";
    html_file << "        };\n";
    html_file << "        var layout = {\n";
    html_file << "            title: 'Network Traffic Over Time',\n";
    html_file << "            xaxis: { title: 'Time', tickangle: -45 },\n";
    html_file << "            yaxis: { title: 'MB' },\n";
    html_file << "            plot_bgcolor: '#2d2d3a',\n";
    html_file << "            paper_bgcolor: '#1e1e2e',\n";
    html_file << "            font: { color: '#fff' }\n";
    html_file << "        };\n";
    html_file << "        Plotly.newPlot('traffic-graph', [trace1, trace2], layout);\n";
    html_file << "    </script>\n";
    html_file << "</body>\n";
    html_file << "</html>";
    
    html_file.close();
    cout << "Traffic graph generated: " << output_dir << "/traffic_graph.html" << endl;
}

void run_heatmap_generator() {
    cout << "Loading data from JSON files..." << endl;
    vector<DataPoint> points = load_all_data();
    cout << "Loaded " << points.size() << " total records" << endl;
    
    vector<DataPoint> points_with_location;
    for (const auto& p : points) {
        if (p.has_location) {
            points_with_location.push_back(p);
        }
    }
    cout << "Points with location: " << points_with_location.size() << endl;
    
    fs::create_directory("graphs");
    
    generate_python_heatmap_script(points_with_location, "heatmap_pillow.png");
    
    cout << "Generating signal graphs..." << endl;
    generate_signal_graphs(points, "graphs");
    
    cout << "Generating traffic graphs..." << endl;
    generate_traffic_graphs("graphs");
    
    cout << "Done! Check heatmap_pillow.png and graphs/ folder" << endl;
}