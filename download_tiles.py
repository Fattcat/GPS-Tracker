"""
Jednorazove stiahnutie OSM dlazdic (tiles) pre oblast, kde planujes merat,
aby sa vysledna mapa (make_map.py) dala otvorit aj BEZ internetu.

VYZADUJE internet pri spusteni tohto skriptu (len raz, vopred, doma).
Respektuj OSM Tile Usage Policy — skript zamerne stahuje pomaly
(throttling) a pouziva vlastny User-Agent. Nestahuj velke oblasti na
vysokom zoom leveli naraz, rychlo to budu tisicky dlazdic a mozes
byt docasne blokovany.

Install: pip install requests

Pouzitie:
  python download_tiles.py <min_lat> <min_lon> <max_lat> <max_lon> <min_zoom> <max_zoom>

Priklad (male okolie domu, zoom 16-19 — staci na detail ulice):
  python download_tiles.py 49.2230 18.7390 49.2255 18.7430 16 19
"""

import sys
import os
import math
import time

from wardriver_common import ensure_package, status

requests = ensure_package("requests")

USER_AGENT = "CHANGEME-WdProject/1.0 kontakt: ferdinand.email@emlemd.com"
TILE_SERVER = "https://tile.openstreetmap.org/{z}/{x}/{y}.png"
DELAY_SECONDS = 2  # slusnost voci OSM serveru


def deg2num(lat, lon, zoom):
    lat_rad = math.radians(lat)
    n = 2 ** zoom
    x = int((lon + 180.0) / 360.0 * n)
    y = int((1.0 - math.log(math.tan(lat_rad) + 1 / math.cos(lat_rad)) / math.pi) / 2.0 * n)
    return x, y


def main():
    if len(sys.argv) != 7:
        print(__doc__)
        sys.exit(1)

    min_lat, min_lon, max_lat, max_lon = (float(v) for v in sys.argv[1:5])
    min_zoom, max_zoom = int(sys.argv[5]), int(sys.argv[6])

    status("Spustam download_tiles.py")
    headers = {"User-Agent": USER_AGENT}
    total = 0

    for zoom in range(min_zoom, max_zoom + 1):
        x1, y1 = deg2num(max_lat, min_lon, zoom)
        x2, y2 = deg2num(min_lat, max_lon, zoom)
        x_min, x_max = sorted((x1, x2))
        y_min, y_max = sorted((y1, y2))

        n_tiles = (x_max - x_min + 1) * (y_max - y_min + 1)
        print(f"Zoom {zoom}: {n_tiles} dlazdic...")

        for x in range(x_min, x_max + 1):
            for y in range(y_min, y_max + 1):
                out_dir = f"tiles/{zoom}/{x}"
                out_path = f"{out_dir}/{y}.png"
                if os.path.exists(out_path):
                    continue
                os.makedirs(out_dir, exist_ok=True)
                url = TILE_SERVER.format(z=zoom, x=x, y=y)
                try:
                    resp = requests.get(url, headers=headers, timeout=10)
                    if resp.status_code == 200:
                        with open(out_path, "wb") as f:
                            f.write(resp.content)
                        total += 1
                    else:
                        print(f"  chyba {resp.status_code}: {url}")
                except requests.RequestException as e:
                    print(f"  chyba pri stahovani {url}: {e}")
                time.sleep(DELAY_SECONDS)

    status(f"Hotovo. Stiahnutych {total} novych dlazdic do priecinka tiles/.", "+")


if __name__ == "__main__":
    main()
