"""
KmlToBrowserMap.py  — Verzia 2.0  |  Opravená & Vylepšená
=============================================================
OPRAVY:
  - bare except → except (ValueError, AttributeError)
  - Validácia GPS súradníc (ochrana pred 0,0 / neplatnými bodmi)
  - Namespace-safe parsovanie KML (endswith namiesto fixného ns)
  - Odstránený zbytočný sleep(1)
  - Lepšie čistenie komentárov s časom

NOVÉ FUNKCIE:
  - Automatická detekcia .kml súborov v priečinku
  - Podpora argumentu z príkazového riadku: python script.py track001.kml
  - Farebne kódovaná trasa podľa rýchlosti (zelená=pomalý → červená=rýchly)
  - Výškový profil ako interaktívny graf (Chart.js) priamo v mape
  - Výpočet rýchlosti medzi bodmi
  - Rozšírený info panel (max/priemer rýchlosť, počet bodov, trvanie)
  - Tooltip s rýchlosťou a časom pri hover nad trasom
  - Prepínanie mapových podkladov (OSM / Svetlá / Tmavá)
  - Validácia vstupných súradníc
  - Výpis štatistík aj do konzoly

POUŽITIE:
  python KmlToBrowserMap.py                   # autodetekcia
  python KmlToBrowserMap.py track001.kml      # konkrétny súbor

INŠTALÁCIA:
  pip install folium
=============================================================
"""

import xml.etree.ElementTree as ET
import folium
import os
import sys
import webbrowser
from math import radians, sin, cos, sqrt, atan2
from datetime import datetime


# =============================================================
#  VÝPOČET VZDIALENOSTI (Haversine)
# =============================================================
def haversine(coord1, coord2):
    """Vráti vzdialenosť v metroch medzi dvoma (lat, lon) bodmi."""
    R = 6371000.0
    lat1, lon1 = radians(coord1[0]), radians(coord1[1])
    lat2, lon2 = radians(coord2[0]), radians(coord2[1])
    dlat = lat2 - lat1
    dlon = lon2 - lon1
    a = sin(dlat / 2) ** 2 + cos(lat1) * cos(lat2) * sin(dlon / 2) ** 2
    return R * 2.0 * atan2(sqrt(a), sqrt(1.0 - a))


# =============================================================
#  PARSOVANIE KML
# =============================================================
def extract_coordinates_from_kml(kml_file):
    """
    Extrahuje GPS súradnice a časové značky z KML súboru.
    Vracia: (coords, timestamps)
      coords     = list of (lat, lon, alt)
      timestamps = list of "HH:MM:SS" alebo None
    """
    with open(kml_file, "rb") as f:
        raw = f.read()

    try:
        root = ET.fromstring(raw)
    except ET.ParseError as e:
        print(f"❌ Chyba pri parsovaní KML: {e}")
        sys.exit(1)

    coords = []
    timestamps = []

    for elem in root.iter():
        if not elem.tag.endswith("coordinates"):
            continue
        if not elem.text:
            continue

        last_time = None
        for raw_line in elem.text.splitlines():
            line = raw_line.strip()
            if not line:
                continue

            # Parsovanie časového komentára <!-- HH:MM:SS UTC -->
            if line.startswith("<!--") and "UTC" in line:
                try:
                    # Odstráni všetky XML komentárové znaky a whitespace
                    cleaned = (
                        line.replace("<!--", "")
                            .replace("-->", "")
                            .replace("UTC", "")
                            .strip()
                    )
                    h, m, s = map(int, cleaned.split(":"))
                    last_time = f"{h:02}:{m:02}:{s:02}"
                except (ValueError, AttributeError):
                    last_time = None
                continue

            # Parsovanie súradníc lon,lat,alt
            if "," not in line:
                continue
            parts = line.split(",")
            if len(parts) < 3:
                continue
            try:
                lon = float(parts[0])
                lat = float(parts[1])
                alt = float(parts[2])

                # Validácia — odmietni nulové a mimo-rozsahové súradnice
                if not (-90.0 <= lat <= 90.0 and -180.0 <= lon <= 180.0):
                    continue
                if lat == 0.0 and lon == 0.0:
                    continue

                coords.append((lat, lon, alt))
                timestamps.append(last_time)
            except ValueError:
                continue

    return coords, timestamps


# =============================================================
#  VÝPOČET ŠTATISTÍK
# =============================================================
def calculate_stats(coords, timestamps):
    """Vypočíta vzdialenosť, rýchlosti, výšky."""
    total_dist = 0.0
    speeds = [0.0]  # Prvý bod nemá rýchlosť

    for i in range(1, len(coords)):
        d = haversine(coords[i - 1], coords[i])
        total_dist += d

        # Rýchlosť z časových značiek
        speed = 0.0
        t_prev = timestamps[i - 1]
        t_curr = timestamps[i]
        if t_prev and t_curr:
            try:
                dt_prev = datetime.strptime(t_prev, "%H:%M:%S")
                dt_curr = datetime.strptime(t_curr, "%H:%M:%S")
                dt_sec = (dt_curr - dt_prev).total_seconds()
                # Ošetrenie prechodu cez polnoc
                if dt_sec < 0:
                    dt_sec += 86400
                if dt_sec > 0:
                    speed = (d / dt_sec) * 3.6  # m/s → km/h
            except (ValueError, AttributeError):
                pass
        speeds.append(round(speed, 2))

    altitudes = [c[2] for c in coords]
    valid_speeds = [s for s in speeds if s > 0]

    return {
        "total_dist":  total_dist,
        "min_alt":     min(altitudes),
        "max_alt":     max(altitudes),
        "avg_alt":     sum(altitudes) / len(altitudes),
        "speeds":      speeds,
        "max_speed":   max(speeds),
        "avg_speed":   sum(valid_speeds) / len(valid_speeds) if valid_speeds else 0.0,
    }


# =============================================================
#  POMOCNÉ FUNKCIE
# =============================================================
def speed_to_color(speed, max_speed):
    """Premení rýchlosť na hex farbu: zelená (pomalý) → červená (rýchly)."""
    if max_speed <= 0.5:
        return "#2563eb"  # Modrá ak nemáme rýchlostné dáta
    ratio = min(speed / max_speed, 1.0)
    r = int(255 * ratio)
    g = int(255 * (1.0 - ratio))
    return f"#{r:02x}{g:02x}00"


def format_dist(meters):
    """Formátuje vzdialenosť v m alebo km."""
    if meters < 1000:
        return f"{meters:.0f} m"
    return f"{meters / 1000:.3f} km"


def format_duration(timestamps):
    """Vypočíta trvanie z prvého a posledného časového pečiatku."""
    first = next((t for t in timestamps if t), None)
    last  = next((t for t in reversed(timestamps) if t), None)
    if not first or not last:
        return "N/A"
    try:
        t1 = datetime.strptime(first, "%H:%M:%S")
        t2 = datetime.strptime(last,  "%H:%M:%S")
        sec = (t2 - t1).total_seconds()
        if sec < 0:
            sec += 86400  # Prechod cez polnoc
        h = int(sec // 3600)
        m = int((sec % 3600) // 60)
        s = int(sec % 60)
        if h > 0:
            return f"{h}h {m}m {s}s"
        return f"{m}m {s}s"
    except ValueError:
        return "N/A"


# =============================================================
#  TVORBA MAPY
# =============================================================
def create_map(coords, timestamps, stats, kml_file):
    """Vytvorí interaktívnu HTML mapu a otvorí ju v prehliadači."""

    m = folium.Map(
        location=coords[0][:2],
        zoom_start=17,
        tiles="OpenStreetMap"
    )

    # Alternatívne mapové podklady
    folium.TileLayer("CartoDB positron",    name="💡 Svetlá mapa").add_to(m)
    folium.TileLayer("CartoDB dark_matter", name="🌙 Tmavá mapa").add_to(m)
    folium.LayerControl(collapsed=False).add_to(m)

    # --- Farebne kódovaná trasa podľa rýchlosti ---
    speeds    = stats["speeds"]
    max_speed = stats["max_speed"]

    for i in range(1, len(coords)):
        color   = speed_to_color(speeds[i], max_speed)
        tooltip = f"{speeds[i]:.1f} km/h"
        if timestamps[i]:
            tooltip += f" | {timestamps[i]} UTC"

        folium.PolyLine(
            locations=[coords[i - 1][:2], coords[i][:2]],
            color=color,
            weight=4,
            opacity=0.85,
            tooltip=tooltip,
        ).add_to(m)

    # --- Štart marker ---
    popup_start = f"<b>🟢 Štart</b><br>{timestamps[0] or 'N/A'} UTC"
    folium.Marker(
        location=coords[0][:2],
        tooltip="🟢 Štart",
        popup=folium.Popup(popup_start, max_width=200),
        icon=folium.Icon(color="green", icon="play", prefix="fa"),
    ).add_to(m)

    # --- Koniec marker ---
    popup_end = (
        f"<b>🔴 Koniec</b><br>"
        f"{timestamps[-1] or 'N/A'} UTC<br>"
        f"Body: {len(coords)}"
    )
    folium.Marker(
        location=coords[-1][:2],
        tooltip="🔴 Koniec",
        popup=folium.Popup(popup_end, max_width=200),
        icon=folium.Icon(color="red", icon="stop", prefix="fa"),
    ).add_to(m)

    # --- Info panel (vpravo hore) ---
    duration_str  = format_duration(timestamps)
    dist_str      = format_dist(stats["total_dist"])
    max_spd_str   = f"{stats['max_speed']:.1f} km/h" if stats["max_speed"] > 0 else "N/A"
    avg_spd_str   = f"{stats['avg_speed']:.1f} km/h" if stats["avg_speed"] > 0 else "N/A"

    info_html = f"""
    <div id="InfoPanel">
        <h4>📍 Info o trase</h4>
        <table>
          <tr><td>📏 Dĺžka</td><td><b>{dist_str}</b></td></tr>
          <tr><td>⏱ Trvanie</td><td><b>{duration_str}</b></td></tr>
          <tr><td>📌 Body</td><td><b>{len(coords)}</b></td></tr>
          <tr><td>⬆ Max. výška</td><td><b>{stats['max_alt']:.1f} m</b></td></tr>
          <tr><td>⬇ Min. výška</td><td><b>{stats['min_alt']:.1f} m</b></td></tr>
          <tr><td>⚡ Max. rýchlosť</td><td><b>{max_spd_str}</b></td></tr>
          <tr><td>🚶 Priem. rýchlosť</td><td><b>{avg_spd_str}</b></td></tr>
          <tr><td>🕐 Začiatok</td><td><b>{timestamps[0] or '-'} UTC</b></td></tr>
          <tr><td>🕐 Koniec</td><td><b>{timestamps[-1] or '-'} UTC</b></td></tr>
        </table>
        <div class="legend">Farba trasy: 🟢 pomalý → 🔴 rýchly</div>
    </div>
    <style>
    #InfoPanel {{
        position: fixed; top: 20px; right: 20px;
        width: 270px; background: white; color: #222;
        border: 2px solid #22c55e; border-radius: 12px;
        padding: 14px; z-index: 9999;
        font-family: Arial, sans-serif;
        box-shadow: 0 4px 20px rgba(0,0,0,0.25);
    }}
    #InfoPanel h4 {{ margin: 0 0 8px 0; color: #15803d; font-size: 14px; }}
    #InfoPanel table {{ width: 100%; border-collapse: collapse; font-size: 12px; }}
    #InfoPanel td {{ padding: 2px 5px; }}
    #InfoPanel td:first-child {{ color: #555; white-space: nowrap; }}
    #InfoPanel .legend {{ margin-top: 8px; font-size: 11px; color: #666; text-align: center; }}
    </style>
    """
    m.get_root().html.add_child(folium.Element(info_html))

    # --- Výškový profil (vpravo dole) — Chart.js ---
    alt_data    = [round(c[2], 1) for c in coords]
    time_labels = [t or "" for t in timestamps]
    # Pre výkon: zobraz max 500 bodov v grafe (decimácia)
    step = max(1, len(alt_data) // 500)
    alt_sampled  = alt_data[::step]
    time_sampled = time_labels[::step]

    altitude_chart_html = f"""
    <div id="AltChart">
        <b style="font-size:12px">📈 Výškový profil</b>
        <canvas id="altCanvas" width="240" height="90"></canvas>
    </div>
    <style>
    #AltChart {{
        position: fixed; bottom: 30px; right: 20px;
        width: 270px; background: white;
        border: 2px solid #3b82f6; border-radius: 12px;
        padding: 10px; z-index: 9999;
        font-family: Arial, sans-serif;
        box-shadow: 0 4px 16px rgba(0,0,0,0.2);
    }}
    </style>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4/dist/chart.umd.min.js"></script>
    <script>
    (function() {{
        var ctx = document.getElementById('altCanvas').getContext('2d');
        new Chart(ctx, {{
            type: 'line',
            data: {{
                labels: {time_sampled},
                datasets: [{{
                    label: 'Výška (m)',
                    data: {alt_sampled},
                    borderColor: '#3b82f6',
                    backgroundColor: 'rgba(59,130,246,0.12)',
                    borderWidth: 1.5,
                    pointRadius: 0,
                    fill: true,
                    tension: 0.3
                }}]
            }},
            options: {{
                responsive: false,
                plugins: {{ legend: {{ display: false }}, tooltip: {{ mode: 'index' }} }},
                scales: {{
                    x: {{ display: false }},
                    y: {{
                        ticks: {{ font: {{ size: 10 }}, maxTicksLimit: 5 }},
                        grid: {{ color: 'rgba(0,0,0,0.05)' }}
                    }}
                }}
            }}
        }});
    }})();
    </script>
    """
    m.get_root().html.add_child(folium.Element(altitude_chart_html))

    # Ulož a otvor
    base_name = os.path.splitext(os.path.basename(kml_file))[0]
    map_path  = os.path.abspath(f"{base_name}-MAP.html")
    m.save(map_path)
    webbrowser.open(f"file://{map_path}")
    print(f"✅ Mapa uložená: {map_path}")


# =============================================================
#  VÝBER KML SÚBORU
# =============================================================
def select_kml_file():
    """Vyberie KML súbor — argument, autodetekcia, alebo interaktívne."""

    # 1) Argument z príkazového riadku
    if len(sys.argv) > 1:
        path = sys.argv[1]
        if not os.path.isfile(path):
            print(f"❌ Súbor '{path}' neexistuje.")
            sys.exit(1)
        return path

    # 2) Autodetekcia v aktuálnom priečinku
    kml_files = sorted(
        f for f in os.listdir(".")
        if f.lower().endswith(".kml") and os.path.isfile(f)
    )

    if not kml_files:
        print("❌ Nenašiel sa žiadny .kml súbor v aktuálnom priečinku.")
        print("   Použi: python KmlToBrowserMap.py <subor.kml>")
        sys.exit(1)

    if len(kml_files) == 1:
        print(f"📂 Nájdený súbor: {kml_files[0]}")
        return kml_files[0]

    # 3) Interaktívny výber
    print("\nDostupné KML súbory:")
    for i, f in enumerate(kml_files, 1):
        size_kb = os.path.getsize(f) / 1024
        print(f"  [{i}] {f}  ({size_kb:.1f} kB)")

    try:
        choice = int(input("\nVyber číslo súboru: ")) - 1
        if not (0 <= choice < len(kml_files)):
            raise IndexError
        return kml_files[choice]
    except (ValueError, IndexError):
        print("❌ Neplatná voľba.")
        sys.exit(1)


# =============================================================
#  HLAVNÉ SPUSTENIE
# =============================================================
if __name__ == "__main__":
    kml_file = select_kml_file()

    print(f"📖 Načítavam: {kml_file}")
    coords, timestamps = extract_coordinates_from_kml(kml_file)

    if len(coords) < 2:
        print(f"❌ Nedostatok platných GPS bodov ({len(coords)}). Minimum sú 2.")
        sys.exit(1)

    print(f"✅ Načítaných {len(coords)} platných bodov.")
    stats = calculate_stats(coords, timestamps)

    # Výpis štatistík do konzoly
    print(f"\n{'─' * 35}")
    print(f"  📏  Vzdialenosť : {format_dist(stats['total_dist'])}")
    print(f"  ⏱   Trvanie     : {format_duration(timestamps)}")
    print(f"  ⬆   Max. výška  : {stats['max_alt']:.1f} m")
    print(f"  ⬇   Min. výška  : {stats['min_alt']:.1f} m")
    if stats["max_speed"] > 0:
        print(f"  ⚡  Max. rýchlosť: {stats['max_speed']:.1f} km/h")
        print(f"  🚶  Priem. rýchl.: {stats['avg_speed']:.1f} km/h")
    print(f"{'─' * 35}\n")

    create_map(coords, timestamps, stats, kml_file)
