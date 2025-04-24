import xml.etree.ElementTree as ET
import folium
import os
import webbrowser
from time import sleep
from math import radians, sin, cos, sqrt, atan2

# Výpočet vzdialenosti medzi dvoma bodmi (v metroch)
def haversine(coord1, coord2):
    R = 6371000
    lat1, lon1 = map(radians, coord1[:2])
    lat2, lon2 = map(radians, coord2[:2])
    dlat = lat2 - lat1
    dlon = lon2 - lon1
    a = sin(dlat / 2)**2 + cos(lat1) * cos(lat2) * sin(dlon / 2)**2
    return R * 2 * atan2(sqrt(a), sqrt(1 - a))

# Extrakcia súradníc a časov z KML
def extract_coordinates_from_kml(kml_file):
    with open(kml_file, 'rb') as f:
        doc = f.read()
    ns = {'kml': 'http://www.opengis.net/kml/2.2'}
    root = ET.fromstring(doc)

    coords = []
    timestamps = []

    for elem in root.iter():
        if elem.tag.endswith("coordinates"):
            lines = elem.text.strip().splitlines()
            last_comment_time = None
            for line in lines:
                line = line.strip()
                if line.startswith("<!--") and "UTC" in line:
                    try:
                        time_str = line.strip(" <!-->").replace(" UTC", "").strip()
                        h, m, s = map(int, time_str.split(":"))
                        last_comment_time = f"{h:02}:{m:02}:{s:02}"
                    except:
                        last_comment_time = None
                elif ',' in line:
                    parts = line.split(',')
                    if len(parts) >= 3:
                        lon = float(parts[0])
                        lat = float(parts[1])
                        alt = float(parts[2])
                        coords.append((lat, lon, alt))
                        timestamps.append(last_comment_time)
    return coords, timestamps

# Spočíta celkovú vzdialenosť
def calculate_total_distance(coords):
    total_distance = 0.0
    for i in range(1, len(coords)):
        total_distance += haversine(coords[i - 1], coords[i])
    return total_distance

# Vytvorenie mapy
def create_map(coords, total_distance, min_alt, max_alt, start_time, end_time, kml_file):
    m = folium.Map(location=coords[0][:2], zoom_start=18, tiles="OpenStreetMap") # tu bolo CartoDB dark_matter 

    folium.PolyLine([c[:2] for c in coords], color="blue", weight=2).add_to(m)
    folium.Marker(coords[0][:2], tooltip="Štart", icon=folium.Icon(color="green")).add_to(m)
    folium.Marker(coords[-1][:2], tooltip="Koniec", icon=folium.Icon(color="red")).add_to(m)

    info_html = f"""
    <div id="Tabulka">
        <h4>📍 Info o trase</h4>
        <b>Celková dĺžka:</b> {total_distance:.1f} m<br>
        <b>Max. výška:</b> {max_alt:.1f} m<br>
        <b>Min. výška:</b> {min_alt:.1f} m<br>
        <b>Začiatok:</b> {start_time or '-'} UTC<br>
        <b>Koniec:</b> {end_time or '-'} UTC
    </div>
    <style>
        #Tabulka {{
            position: fixed;
            top: 20px;
            right: 20px;
            width: 300px;
            background-color: white;
            color: black;
            border: 2px solid green;
            border-radius: 10px;
            padding: 10px;
            z-index: 9999;
            font-family: Arial, sans-serif;
            box-shadow: 0 0 10px rgba(0,0,0,0.3);
        }}
    </style>
    """
    m.get_root().html.add_child(folium.Element(info_html))

    map_path = os.path.abspath(f"{kml_file}-MAP.html")
    m.save(map_path)
    sleep(1)
    webbrowser.open(f"file://{map_path}")
    print("✅ Mapa bola otvorená.")

# --- Spustenie ---
kml_file = "trackExample.kml"
coords, timestamps = extract_coordinates_from_kml(kml_file)

if coords and len(coords) > 1:
    total_distance = calculate_total_distance(coords)
    altitudes = [c[2] for c in coords]
    min_alt = min(altitudes)
    max_alt = max(altitudes)
    start_time = timestamps[0] if timestamps else None
    end_time = timestamps[-1] if timestamps else None
    create_map(coords, total_distance, min_alt, max_alt, start_time, end_time, kml_file)
else:
    print("❌ Nedostatok bodov pre výpočet trasy.")
