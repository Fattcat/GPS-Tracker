import xml.etree.ElementTree as ET
import folium
import os
import webbrowser

# Funkcia na extrahovanie súradníc z KML
def extract_coordinates_from_kml(kml_file):
    with open(kml_file, 'rb') as f:
        doc = f.read()

    # KML namespace
    ns = {'kml': 'http://www.opengis.net/kml/2.2'}
    
    # Načítame XML z KML súboru
    root = ET.fromstring(doc)

    coords = []

    # Prejdeme všetky elementy <coordinates>
    coordinates_elements = root.findall('.//kml:coordinates', ns)
    
    if not coordinates_elements:
        print("❌ Neboli nájdené žiadne súradnice v KML.")
        return coords

    for coordinates in coordinates_elements:
        coord_text = coordinates.text.strip()
        if coord_text:
            print(f"🔍 Nájdeme súradnice: {coord_text}")  # Debugging
            # Splitovanie súradníc
            coord_pairs = coord_text.split()
            for pair in coord_pairs:
                lat, lon, _ = pair.split(',')
                coords.append((float(lat), float(lon)))

    return coords

# Funkcia na vytvorenie mapy z extrahovaných súradníc
def create_map(coords):
    if not coords:
        print("❌ Žiadne súradnice sa nenašli.")
        return

    # Vytvoríme mapu s počiatočnou polohou na prvých súradniciach
    m = folium.Map(location=coords[0], zoom_start=30)

    # Pridáme čiaru, ktorá spája všetky súradnice
    folium.PolyLine(coords, color="blue", weight=1).add_to(m)

    # Pridáme markery na začiatok a koniec trasy
    folium.Marker(coords[0], tooltip="Štart").add_to(m)
    folium.Marker(coords[-1], tooltip="Koniec").add_to(m)

    # Uložíme mapu do HTML súboru a otvoríme ju v prehliadači
    map_path = os.path.abspath("gps_map.html")
    m.save(map_path)
    webbrowser.open(f"file://{map_path}")
    print("✅ Mapa bola otvorená v prehliadači.")

# Testovanie - získame súradnice z KML a vytvoríme mapu
kml_file = "track.kml" # --------------------------- > Change to YOUR .kml File < ---------------------------
coords = extract_coordinates_from_kml(kml_file)

# Ak boli nájdené súradnice, vytvoríme mapu
create_map(coords)
