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
    
    root = ET.fromstring(doc)
    coords = []

    coordinates_elements = root.findall('.//kml:coordinates', ns)
    
    if not coordinates_elements:
        print("❌ Neboli nájdené žiadne súradnice v KML.")
        return coords

    for coordinates in coordinates_elements:
        coord_text = coordinates.text.strip()
        if coord_text:
            print(f"🔍 Nájdeme súradnice: {coord_text}")  # Debugging
            coord_pairs = coord_text.split()
            for pair in coord_pairs:
                lon, lat, _ = pair.split(',')  # POZOR: v KML sú súradnice v poradí lon, lat
                coords.append((float(lat), float(lon)))

    return coords

# Funkcia na vytvorenie mapy z extrahovaných súradníc
def create_map(coords):
    if not coords:
        print("❌ Žiadne súradnice sa nenašli.")
        return

    # Použijeme "CartoDB Dark Matter" ako dlaždice (čierne pozadie s bielou mriežkou)
    m = folium.Map(
        location=coords[0],
        zoom_start=18,
        tiles="CartoDB dark_matter"
    )

    # Pridáme čiaru a značky
    folium.PolyLine(coords, color="blue", weight=2).add_to(m)
    folium.Marker(coords[0], tooltip="Štart", icon=folium.Icon(color="green")).add_to(m)
    folium.Marker(coords[-1], tooltip="Koniec", icon=folium.Icon(color="red")).add_to(m)

    # Uložíme mapu
    map_path = os.path.abspath("gps_map.html")
    m.save(map_path)
    webbrowser.open(f"file://{map_path}")
    print("✅ Mapa bola otvorená v prehliadači.")

# Spustenie
kml_file = "track.kml" #
coords = extract_coordinates_from_kml(kml_file)
create_map(coords)
