#!/usr/bin/env python3
import json
import os
import folium
from folium.plugins import HeatMap

def load_data_from_json():
    points = []

    if os.path.exists("data/all_data.json"):
        try:
            with open("data/all_data.json", "r") as f:
                all_data = json.load(f)
                if isinstance(all_data, list):
                    for item in all_data:
                        lat = None
                        lon = None
                        signal = -200

                        if "location" in item:
                            loc = item["location"]
                            if "latitude" in loc and "longitude" in loc:
                                lat = loc["latitude"]
                                lon = loc["longitude"]

                        if "telephony" in item:
                            for key, cell in item["telephony"].items():
                                if key.startswith("cell_"):
                                    if "rsrp" in cell:
                                        signal = max(signal, cell["rsrp"])
                                    if "dbm" in cell:
                                        signal = max(signal, cell["dbm"])

                        if lat is not None and lon is not None:
                            points.append((lat, lon, signal))
        except Exception as e:
            print(f"Error loading all_data.json: {e}")

    if os.path.exists("data/location_danil.json"):
        try:
            with open("data/location_danil.json", "r") as f:
                loc_data = json.load(f)

                def parse_item(item):
                    lat = item.get("latitude")
                    lon = item.get("longitude")
                    signal = -200

                    if "cellInfo" in item:
                        ci = item["cellInfo"]
                        pos = ci.find("rssi=")
                        if pos != -1:
                            val = ""
                            for ch in ci[pos + 5:]:
                                if ch.isdigit() or ch == "-":
                                    val += ch
                                else:
                                    break
                            if val:
                                signal = int(val)

                    if lat and lon:
                        points.append((lat, lon, signal))

                if isinstance(loc_data, list):
                    for item in loc_data:
                        parse_item(item)
                elif isinstance(loc_data, dict):
                    parse_item(loc_data)

        except Exception as e:
            print(f"Error loading location_danil.json: {e}")

    return points


def get_color(signal):
    if signal >= -80:
        return "green"
    elif signal >= -90:
        return "lightgreen"
    elif signal >= -100:
        return "orange"
    elif signal >= -110:
        return "red"
    else:
        return "darkred"


def generate_map():
    points = load_data_from_json()

    if not points:
        print("No data")
        return

    # центр карты
    avg_lat = sum(p[0] for p in points) / len(points)
    avg_lon = sum(p[1] for p in points) / len(points)

    m = folium.Map(location=[avg_lat, avg_lon], zoom_start=15)

    # heatmap данные
    heat_data = []
    for lat, lon, signal in points:
        if signal > -140:
            weight = max(0, (signal + 140) / 60)  # нормализация
            heat_data.append([lat, lon, weight])

    HeatMap(heat_data, radius=15).add_to(m)

    # точки с тултипами
    for lat, lon, signal in points:
        folium.CircleMarker(
            location=[lat, lon],
            radius=5,
            color=get_color(signal),
            fill=True,
            fill_opacity=0.7,
            popup=f"Signal: {signal} dBm"
        ).add_to(m)

    output_file = "heatmap_map.html"
    m.save(output_file)
    print(f"Map saved to {output_file}")


if __name__ == "__main__":
    generate_map()