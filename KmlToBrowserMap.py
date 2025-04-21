import xml.etree.ElementTree as ET
import folium
import os
import webbrowser
from time import sleep
from math import radians, sin, cos, sqrt, atan2

# Výpočet vzdialenosti medzi dvoma bodmi (v metroch)
def haversine(coord1, coord2):
    R = 6371000  # polomer Zeme v metroch
    lat1, lon1 = map(radians, coord1)
    lat2, lon2 = map(radians, coord2)
    dlat = lat2 - lat1
    dlon = lon2 - lon1
    a = sin(dlat / 2)**2 + cos(lat1) * cos(lat2) * sin(dlon / 2)**2
    c = 2 * atan2(sqrt(a), sqrt(1 - a))
    return R * c

# Načíta súradnice z KML
def extract_coordinates_from_kml(kml_file):
    with open(kml_file, 'rb') as f:
        doc = f.read()
    ns = {'kml': 'http://www.opengis.net/kml/2.2'}
    root = ET.fromstring(doc)
    coords = []

    coordinates_elements = root.findall('.//kml:coordinates', ns)
    if not coordinates_elements:
        print("❌ Žiadne súradnice v KML.")
        return coords

    for coordinates in coordinates_elements:
        coord_text = coordinates.text.strip()
        if coord_text:
            coord_pairs = coord_text.split()
            for pair in coord_pairs:
                values = pair.split(',')
                if len(values) >= 2:
                    lon = float(values[0])
                    lat = float(values[1])
                    coords.append((lat, lon))
                else:
                    print(f"⚠️ Preskočený neplatný záznam: '{pair}'")
    return coords


# Spočíta celkovú prejdenú vzdialenosť medzi všetkými bodmi
def calculate_total_distance(coords):
    total_distance = 0.0
    for i in range(1, len(coords)):
        total_distance += haversine(coords[i - 1], coords[i])
    return total_distance

# Vytvorí mapu a zobrazí celkovú dĺžku trasy
def create_map(coords, total_distance):
    if not coords:
        print("❌ Žiadne súradnice sa nenašli.")
        return

    m = folium.Map(location=coords[0], zoom_start=18, tiles="CartoDB dark_matter")
    folium.PolyLine(coords, color="blue", weight=2).add_to(m)
    folium.Marker(coords[0], tooltip="Štart", icon=folium.Icon(color="green")).add_to(m)
    folium.Marker(coords[-1], tooltip="Koniec", icon=folium.Icon(color="red")).add_to(m)

    # Pridaj značku do stredu trasy s dĺžkou
    mid_index = len(coords) // 2
    folium.Marker(
        location=coords[mid_index],
        popup=f"Celková dĺžka trasy: {total_distance:.1f} m",
        icon=folium.Icon(color="blue", icon="info-sign")
    ).add_to(m)

    # Ulož mapu a otvor ju v prehliadači
    map_path = os.path.abspath(f"{kml_file}-MAP.html")
    m.save(map_path)
    sleep(1)
    webbrowser.open(f"file://{map_path}")
    print(f"✅ Mapa otvorená. Celková dĺžka trasy: {total_distance:.1f} metrov.")

# Hlavné spustenie
kml_file = "track0002.kml"
coords = extract_coordinates_from_kml(kml_file)

if coords and len(coords) > 1:
    total_distance = calculate_total_distance(coords)
    create_map(coords, total_distance)
else:
    print("❌ Nedostatok bodov pre výpočet trasy.")
