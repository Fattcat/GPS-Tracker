"""
KmlToBrowserMap.py  —  Verzia 3.0
=============================================================
Číta CSV súbory z GPS trackeru (track0001.csv atď.)
Generuje interaktívnu HTML mapu s 3 pohľadmi:
  🗺  Terénna mapa (OpenTopoMap) — hory, vrstevnice, cesty
  🛤  Čistá trasa — len GPS cesta bez podkladu
  🏔  3D model — Three.js orbit vizualizácia

Automaticky detekuje šumové záznamy (GPS drift bez pohybu).

POUŽITIE:
  python KmlToBrowserMap.py                  # autodetekcia
  python KmlToBrowserMap.py track0001.csv    # konkrétny súbor

INŠTALÁCIA: (žiadne extra balíky — používa len stdlib + html)
=============================================================
"""

import csv
import os
import sys
import json
import webbrowser
from math import radians, sin, cos, sqrt, atan2, isnan
from datetime import datetime
from statistics import mean, stdev


# ─────────────────────────────────────────────
#  HAVERSINE
# ─────────────────────────────────────────────
def haversine(c1, c2):
    R = 6371000.0
    la1, lo1 = radians(c1[0]), radians(c1[1])
    la2, lo2 = radians(c2[0]), radians(c2[1])
    a = sin((la2-la1)/2)**2 + cos(la1)*cos(la2)*sin((lo2-lo1)/2)**2
    return R * 2.0 * atan2(sqrt(a), sqrt(1-a))


# ─────────────────────────────────────────────
#  PARSOVANIE CSV
# ─────────────────────────────────────────────
def parse_csv(path):
    """
    Číta CSV zo GPS trackeru.
    Formát: lat,lon,alt_m,time,speed_kmh,hdop,satellites
    Vracia list dict-ov.
    """
    points = []
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for i, row in enumerate(reader):
            try:
                lat  = float(row.get("lat", 0))
                lon  = float(row.get("lon", 0))
                alt  = float(row.get("alt_m", 0))
                t    = row.get("time", "").strip()
                spd  = float(row.get("speed_kmh", 0))
                hdop = float(row.get("hdop", 99))
                sats = int(row.get("satellites", 0))

                # Validácia
                if not (-90 <= lat <= 90 and -180 <= lon <= 180):
                    continue
                if lat == 0.0 and lon == 0.0:
                    continue
                if isnan(lat) or isnan(lon):
                    continue

                points.append({
                    "lat":  lat,
                    "lon":  lon,
                    "alt":  round(alt, 1),
                    "time": t,
                    "spd":  round(spd, 2),
                    "hdop": round(hdop, 2),
                    "sats": sats,
                })
            except (ValueError, KeyError):
                continue
    return points


# ─────────────────────────────────────────────
#  DETEKCIA ŠUMU (GPS drift bez pohybu)
# ─────────────────────────────────────────────
def detect_noise(points):
    """
    Analyzuje trasu a zistí či ide o GPS šum (stáť na mieste).

    Vracia:
      is_noise   — True ak trasa je pravdepodobne len GPS drift
      reason     — textový popis prečo
      spread_m   — rozptyl bodov v metroch
    """
    if len(points) < 2:
        return False, "", 0.0

    lats = [p["lat"] for p in points]
    lons = [p["lon"] for p in points]

    center_lat = mean(lats)
    center_lon = mean(lons)

    # Vzdialenosti všetkých bodov od stredu
    dists = [haversine((p["lat"], p["lon"]), (center_lat, center_lon))
             for p in points]
    spread_m    = max(dists)
    avg_dist    = mean(dists)

    # Celková dĺžka trasy
    total = sum(haversine(
                    (points[i-1]["lat"], points[i-1]["lon"]),
                    (points[i]["lat"],   points[i]["lon"])
                ) for i in range(1, len(points)))

    # Priemerná rýchlosť zo záznamu
    speeds = [p["spd"] for p in points if p["spd"] > 0]
    avg_spd = mean(speeds) if speeds else 0.0

    # Priemerný HDOP
    hdops = [p["hdop"] for p in points if p["hdop"] < 50]
    avg_hdop = mean(hdops) if hdops else 99.0

    reasons = []
    is_noise = False

    # Podmienka 1: Všetky body sú v kruhu < 30 m
    if spread_m < 30.0:
        is_noise = True
        reasons.append(f"Všetky body sú v kruhu {spread_m:.1f} m (< 30 m)")

    # Podmienka 2: Celková dĺžka / priama vzdialenosť štart-koniec < 1.5
    direct = haversine((points[0]["lat"], points[0]["lon"]),
                       (points[-1]["lat"], points[-1]["lon"]))
    if total > 0 and direct / total < 0.05 and total < 100:
        is_noise = True
        reasons.append(f"Cesta ({total:.0f} m) sa vracia na štart (priama vzd. {direct:.1f} m)")

    # Podmienka 3: Priemerná rýchlosť veľmi nízka
    if avg_spd < 0.5 and len(speeds) > 3:
        reasons.append(f"Priemerná rýchlosť {avg_spd:.2f} km/h (typický GPS šum)")

    # Podmienka 4: Zlý HDOP
    if avg_hdop > 3.0:
        reasons.append(f"Priemerný HDOP {avg_hdop:.1f} (> 3.0 = nepresný GPS)")

    return is_noise, " | ".join(reasons), spread_m


# ─────────────────────────────────────────────
#  ŠTATISTIKY
# ─────────────────────────────────────────────
def calc_stats(points):
    total   = 0.0
    cumDist = [0.0]
    gain    = loss = 0.0

    for i in range(1, len(points)):
        d = haversine((points[i-1]["lat"], points[i-1]["lon"]),
                      (points[i]["lat"],   points[i]["lon"]))
        total += d
        cumDist.append(total)
        delta = points[i]["alt"] - points[i-1]["alt"]
        if delta > 0: gain += delta
        else:         loss += abs(delta)

    alts   = [p["alt"]  for p in points]
    speeds = [p["spd"]  for p in points if p["spd"] > 0]

    return {
        "total_dist": total,
        "cumDist":    cumDist,
        "min_alt":    min(alts),
        "max_alt":    max(alts),
        "elev_gain":  gain,
        "elev_loss":  loss,
        "max_speed":  max(speeds) if speeds else 0.0,
        "avg_speed":  mean(speeds) if speeds else 0.0,
    }


def fmt_dist(m):
    return f"{m:.0f} m" if m < 1000 else f"{m/1000:.3f} km"

def fmt_dur(points):
    times = [p["time"] for p in points if p["time"]]
    if len(times) < 2:
        return "N/A"
    try:
        t1 = datetime.strptime(times[0],  "%H:%M:%S")
        t2 = datetime.strptime(times[-1], "%H:%M:%S")
        s  = (t2 - t1).total_seconds()
        if s < 0: s += 86400
        h, m = int(s//3600), int((s % 3600)//60)
        return f"{h}h {m}m" if h else f"{m}m {int(s%60)}s"
    except ValueError:
        return "N/A"


# ─────────────────────────────────────────────
#  GENEROVANIE HTML
# ─────────────────────────────────────────────
def generate_html(points, stats, csv_file, noise_info):
    N = len(points)

    # Decimácia – max 3000 bodov v JS
    step    = max(1, N // 3000)
    pts_dec = [
        {
            "lat":  points[i]["lat"],
            "lon":  points[i]["lon"],
            "alt":  points[i]["alt"],
            "time": points[i]["time"],
            "spd":  points[i]["spd"],
            "hdop": points[i]["hdop"],
            "sats": points[i]["sats"],
            "dist": round(stats["cumDist"][i], 1),
        }
        for i in range(0, N, step)
    ]
    pts_js = json.dumps(pts_dec)
    n_pts  = len(pts_dec)

    clat = points[N//2]["lat"]
    clon = points[N//2]["lon"]

    dist_str    = fmt_dist(stats["total_dist"])
    dur_str     = fmt_dur(points)
    max_spd_str = f"{stats['max_speed']:.1f} km/h" if stats["max_speed"] > 0 else "N/A"
    avg_spd_str = f"{stats['avg_speed']:.1f} km/h" if stats["avg_speed"] > 0 else "N/A"
    gain_str    = f"+{stats['elev_gain']:.0f} m"
    loss_str    = f"−{stats['elev_loss']:.0f} m"
    alt_min     = stats["min_alt"]
    alt_max     = stats["max_alt"]

    is_noise, noise_reason, spread_m = noise_info
    noise_banner = ""
    if is_noise:
        noise_banner = f"""
        <div id="noiseBanner">
          ⚠️ UPOZORNENIE: Toto vyzerá ako GPS šum, nie reálna cesta!<br>
          <small>{noise_reason}</small>
        </div>"""

    html = f"""<!DOCTYPE html>
<html lang="sk">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GPS Track · {os.path.basename(csv_file)}</title>
<link href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;500;700&family=Syne:wght@700;800&display=swap" rel="stylesheet">
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4/dist/chart.umd.min.js"></script>
<style>
:root {{
  --bg:     #0b0e14;
  --surf:   #12161f;
  --surf2:  #181d28;
  --border: #1e2535;
  --accent: #00e5a0;
  --blue:   #4f9eff;
  --red:    #ff4d6d;
  --warn:   #f59e0b;
  --text:   #dde4f0;
  --muted:  #5a6480;
  --r:      10px;
  --tb:     54px;
  --ch:     130px;
}}
*{{ box-sizing:border-box; margin:0; padding:0; }}
html,body{{ width:100%; height:100%; overflow:hidden;
            background:var(--bg); color:var(--text);
            font-family:'JetBrains Mono',monospace; }}

/* ── TOOLBAR ── */
#tb{{
  position:fixed; top:0; left:0; right:0; height:var(--tb);
  background:var(--surf); border-bottom:1px solid var(--border);
  display:flex; align-items:center; gap:6px; padding:0 12px; z-index:9999;
}}
.brand{{ font-family:'Syne',sans-serif; font-size:14px; font-weight:800;
         color:var(--accent); letter-spacing:.08em; margin-right:8px; white-space:nowrap; }}
.tbtn{{ border:1px solid var(--border); background:transparent; color:var(--muted);
        padding:5px 14px; border-radius:18px; cursor:pointer;
        font-family:'JetBrains Mono',monospace; font-size:11px; font-weight:500;
        transition:all .18s; white-space:nowrap; }}
.tbtn:hover{{ border-color:var(--accent); color:var(--accent); }}
.tbtn.active{{ background:var(--accent); border-color:var(--accent); color:#000; font-weight:700; }}
.vsep{{ width:1px; height:22px; background:var(--border); margin:0 3px; flex-shrink:0; }}
.chip{{ font-size:10px; color:var(--muted); background:var(--bg);
        border:1px solid var(--border); padding:3px 9px; border-radius:18px; white-space:nowrap; }}
.chip b{{ color:var(--text); }}
.chips{{ display:flex; gap:5px; overflow:hidden; }}

/* ── NOISE BANNER ── */
#noiseBanner{{
  position:fixed; top:calc(var(--tb)+6px); left:50%; transform:translateX(-50%);
  z-index:10001; background:rgba(245,158,11,.15); border:1px solid var(--warn);
  border-radius:var(--r); padding:8px 16px; text-align:center;
  color:var(--warn); font-size:11px; max-width:600px; line-height:1.6;
  backdrop-filter:blur(10px);
}}

/* ── VIEWS ── */
#mw, #v3d{{
  position:fixed; top:var(--tb); left:0; right:0; bottom:var(--ch);
}}
#map{{ width:100%; height:100%; }}
#v3d{{ background:#060810; display:none; }}
#cv3{{ width:100%; height:100%; display:block; cursor:grab; }}
#cv3:active{{ cursor:grabbing; }}

/* ── HOVER CARD ── */
#hcard{{
  position:fixed; pointer-events:none; z-index:10000; display:none;
  background:rgba(11,14,20,.97); border:1px solid var(--accent);
  border-radius:var(--r); padding:10px 14px; min-width:180px;
  backdrop-filter:blur(10px); font-size:11px;
  box-shadow:0 6px 28px rgba(0,229,160,.16);
}}
#hcard .ht{{ font-size:15px; font-weight:700; color:var(--accent); margin-bottom:7px; }}
.hr{{ display:flex; justify-content:space-between; gap:14px; color:var(--muted); padding:1.5px 0; }}
.hr b{{ color:var(--text); }}

/* ── INFO PANEL ── */
#info{{
  position:fixed; top:47px; right:200px; z-index:1000;
  background:rgba(11,14,20,.93); border:1px solid var(--border);
  border-radius:var(--r); padding:12px 15px; min-width:200px;
  backdrop-filter:blur(10px); font-size:11px;
  box-shadow:0 6px 28px rgba(0,0,0,.5);
}}
#info h4{{ font-family:'Syne',sans-serif; font-weight:800; font-size:12px;
           color:var(--accent); letter-spacing:.07em; margin-bottom:9px; }}
.ir{{ display:flex; justify-content:space-between; gap:10px; color:var(--muted); padding:2px 0; }}
.ir b{{ color:var(--text); font-weight:500; }}
.ir.warn b{{ color:var(--warn); }}

/* ── 3D OVERLAYS ── */
#hud3{{
  position:fixed; top:0px; left:10px; bottom: 780px; z-index:1000;
  background:rgba(11,14,20,.85); border:1px solid var(--border);
  border-radius:var(--r); padding:80px 12px; padding-top: 55px; font-size:10px; color:var(--muted);
  pointer-events:none; display:none; line-height:1.9;
}}
#hud3 b{{ color:var(--accent); font-size:11px; display:block; margin-bottom:2px; }}
#leg3{{
  position:fixed;
  top:48px;
  right:400px;
  z-index:2000;
  background:rgba(11,14,20,.85);
  border:1px solid var(--border);
  border-radius:var(--r);
  padding:9px 13px; font-size:10px;
  color:var(--muted);
  display:none;
}}
#gbar{{
  width:110px; height:6px; border-radius:3px; margin:5px 0 3px;
  background:linear-gradient(to right,var(--accent),var(--blue),var(--red));
}}
.gl{{ display:flex; justify-content:space-between; font-size:10px; }}

/* ── CHART ── */
#cp{{
  position:fixed; bottom:0; left:0; right:0; height:var(--ch);
  background:var(--surf); border-top:1px solid var(--border);
  z-index:9000; padding:7px 14px 6px;
}}
#cp canvas{{ cursor:crosshair; }}

/* ── CROSSHAIR DOT ── */
.xdot{{
  width:10px; height:10px; border-radius:50%;
  background:white; border:2px solid var(--accent);
  box-shadow:0 0 8px var(--accent);
}}
</style>
</head>
<body>

<div id="tb">
  <div class="brand">⬡ GPS TRACK</div>
  <button class="tbtn active" id="b2d"    onclick="sv('2d')">🗺 Terén</button>
  <button class="tbtn"        id="bclean" onclick="sv('clean')">🛤 Čistá trasa</button>
  <button class="tbtn"        id="b3d"    onclick="sv('3d')">🏔 3D</button>
  <div class="vsep"></div>
  <div class="chips">
    <div class="chip">📏 <b>{dist_str}</b></div>
    <div class="chip">⏱ <b>{dur_str}</b></div>
    <div class="chip">⚡ <b>{max_spd_str}</b></div>
    <div class="chip">📌 <b>{n_pts} bodov</b></div>
    <div class="chip">📈 <b>{gain_str} / {loss_str}</b></div>
  </div>
</div>

{noise_banner}

<div id="mw"><div id="map"></div></div>
<div id="v3d"><canvas id="cv3"></canvas></div>

<div id="hcard">
  <div class="ht" id="hct">--:--:--</div>
  <div class="hr">🏔 Výška        <b id="hca">--</b></div>
  <div class="hr">⚡ Rýchlosť     <b id="hcs">--</b></div>
  <div class="hr">📏 Vzdialenosť  <b id="hcd">--</b></div>
  <div class="hr">📡 HDOP / Sat   <b id="hch">--</b></div>
</div>

<div id="info">
  <h4>📍 TRASA</h4>
  <div class="ir">Celková dĺžka   <b>{dist_str}</b></div>
  <div class="ir">Trvanie         <b>{dur_str}</b></div>
  <div class="ir">Max. rýchlosť   <b>{max_spd_str}</b></div>
  <div class="ir">Priem. rýchl.   <b>{avg_spd_str}</b></div>
  <div class="ir">Max. výška      <b>{alt_max:.1f} m</b></div>
  <div class="ir">Min. výška      <b>{alt_min:.1f} m</b></div>
  <div class="ir">Prevýšenie      <b>{gain_str}</b></div>
  <div class="ir">Zostup          <b>{loss_str}</b></div>
  {'<div class="ir warn">⚠️ GPS šum!  <b>Možno</b></div>' if is_noise else ''}
</div>

<div id="hud3">
  <b>3D OVLÁDANIE</b>
  🖱 Ľavý drag → rotácia<br>
  🖱 Pravý drag → posun<br>
  ⚙ Scroll → zoom
</div>
<div id="leg3">
  Výška n.m.<br>
  <div id="gbar"></div>
  <div class="gl"><span>{alt_min:.0f} m</span><span>{alt_max:.0f} m</span></div>
</div>

<div id="cp"><canvas id="ecv"></canvas></div>

<script>
const PTS = {pts_js};
const N   = PTS.length;
let curV  = '2d';

// ── HOVER CARD ──────────────────────────────
function showCard(pt, mx, my) {{
  document.getElementById('hct').textContent = pt.time || '--:--:--';
  document.getElementById('hca').textContent = pt.alt + ' m n.m.';
  document.getElementById('hcs').textContent = pt.spd > 0 ? pt.spd.toFixed(1)+' km/h' : 'stojím';
  document.getElementById('hcd').textContent = pt.dist < 1000
    ? pt.dist.toFixed(0)+' m' : (pt.dist/1000).toFixed(3)+' km';
  document.getElementById('hch').textContent = pt.hdop.toFixed(1)+' / '+pt.sats+'🛰';
  const card=document.getElementById('hcard');
  const cw=190, ch=118, ww=window.innerWidth, wh=window.innerHeight;
  let cx=mx+14, cy=my-10;
  if(cx+cw>ww) cx=mx-cw-10;
  if(cy+ch>wh-140) cy=my-ch-10;
  card.style.left=cx+'px'; card.style.top=cy+'px';
  card.style.display='block';
}}
function hideCard(){{ document.getElementById('hcard').style.display='none'; }}

// ── VIEW SWITCH ─────────────────────────────
function sv(v) {{
  curV=v;
  ['b2d','bclean','b3d'].forEach(id=>document.getElementById(id).classList.remove('active'));
  const mw=document.getElementById('mw');
  const v3=document.getElementById('v3d');
  const h3=document.getElementById('hud3');
  const l3=document.getElementById('leg3');
  if(v==='3d') {{
    document.getElementById('b3d').classList.add('active');
    mw.style.display='none'; v3.style.display='block';
    h3.style.display='block'; l3.style.display='block';
    init3D(); loop3D();
  }} else {{
    document.getElementById(v==='2d'?'b2d':'bclean').classList.add('active');
    mw.style.display='block'; v3.style.display='none';
    h3.style.display='none';  l3.style.display='none';
    setTiles(v==='2d');
    if(LM) LM.invalidateSize();
  }}
}}

// ── LEAFLET MAP ─────────────────────────────
let LM=null, xdot=null;

function initMap() {{
  LM = L.map('map',{{zoomControl:true,attributionControl:false}}).setView([{clat},{clon}],15);

  // Tile layers — TERÉN ako predvolený
  const terrain   = L.tileLayer('https://{{s}}.tile.opentopomap.org/{{z}}/{{x}}/{{y}}.png',
                      {{maxZoom:19, maxNativeZoom:17, attribution:'© OpenTopoMap'}});
  const osm       = L.tileLayer('https://{{s}}.tile.openstreetmap.org/{{z}}/{{x}}/{{y}}.png',
                      {{maxZoom:19}});
  const satellite = L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{{z}}/{{y}}/{{x}}',
                      {{maxZoom:19}});
  const dark      = L.tileLayer('https://{{s}}.basemaps.cartocdn.com/dark_all/{{z}}/{{x}}/{{y}}{{r}}.png',
                      {{maxZoom:19}});

  terrain.addTo(LM);  // ← predvolený: terén s vrstevnicami
  L.control.layers({{
    '🏔 Terén (vrstevnice)': terrain,
    '🗺 OSM (ulice)':        osm,
    '🛰 Satelit':            satellite,
    '🌙 Tmavá':              dark,
  }},{{}},{{collapsed:false,position:'topright'}}).addTo(LM);

  // Farebná trasa (rýchlosť)
  const maxS = Math.max(...PTS.map(p=>p.spd), 0.1);
  for(let i=1;i<N;i++) {{
    const r=PTS[i].spd/maxS, g=1-r;
    const col=`rgb(${{Math.round(255*r)}},${{Math.round(180*g)}},50)`;
    L.polyline([[PTS[i-1].lat,PTS[i-1].lon],[PTS[i].lat,PTS[i].lon]],
               {{color:col,weight:4,opacity:.9}}).addTo(LM);
  }}

  // Markery štart/koniec
  L.circleMarker([PTS[0].lat,PTS[0].lon],
    {{radius:8,fillColor:'#00e5a0',color:'#fff',weight:2,fillOpacity:1}})
   .bindTooltip('<b>🟢 Štart · '+PTS[0].time+'</b>').addTo(LM);
  L.circleMarker([PTS[N-1].lat,PTS[N-1].lon],
    {{radius:8,fillColor:'#ff4d6d',color:'#fff',weight:2,fillOpacity:1}})
   .bindTooltip('<b>🔴 Koniec · '+PTS[N-1].time+'</b>').addTo(LM);

  // Pohyblivý krúžok pre hover
  xdot = L.marker([PTS[0].lat,PTS[0].lon],{{
    icon:L.divIcon({{className:'xdot',iconSize:[10,10],iconAnchor:[5,5]}})
  }}).addTo(LM);

  LM.on('mousemove', e=>{{
    const ml=e.latlng;
    let best=-1, bd=Infinity;
    for(let i=0;i<N;i++) {{
      const d=(PTS[i].lat-ml.lat)**2+(PTS[i].lon-ml.lng)**2;
      if(d<bd){{bd=d;best=i;}}
    }}
    if(best<0) return;
    showCard(PTS[best],e.originalEvent.clientX,e.originalEvent.clientY);
    xdot.setLatLng([PTS[best].lat,PTS[best].lon]);
  }});
  LM.on('mouseout',hideCard);
}}

function setTiles(on) {{
  if(LM) LM.getPane('tilePane').style.opacity = on?'1':'0';
}}

// ── ELEVATION CHART ─────────────────────────
let EC=null;
function initChart() {{
  const ctx=document.getElementById('ecv').getContext('2d');
  EC=new Chart(ctx,{{
    type:'line',
    data:{{
      labels:PTS.map(p=>p.dist<1000?p.dist.toFixed(0)+'m':(p.dist/1000).toFixed(2)+'km'),
      datasets:[{{
        data:PTS.map(p=>p.alt),
        borderColor:'#4f9eff', backgroundColor:'rgba(79,158,255,.13)',
        borderWidth:1.5, pointRadius:0, fill:true, tension:.35
      }}]
    }},
    options:{{
      responsive:true, maintainAspectRatio:false, animation:false,
      plugins:{{
        legend:{{display:false}},
        tooltip:{{
          mode:'index', intersect:false,
          backgroundColor:'rgba(11,14,20,.96)', borderColor:'#4f9eff', borderWidth:1,
          titleColor:'#4f9eff', bodyColor:'#dde4f0', padding:8,
          callbacks:{{
            title:items=>{{
              const p=PTS[items[0].dataIndex];
              return (p.dist<1000?p.dist.toFixed(0)+'m':(p.dist/1000).toFixed(3)+'km')
                     +' · '+p.time;
            }},
            label:item=>` ${{item.raw}} m n.m.`
          }}
        }}
      }},
      scales:{{
        x:{{display:true,ticks:{{color:'#5a6480',maxTicksLimit:8,font:{{size:9}}}},
           grid:{{color:'rgba(255,255,255,.02)'}}}},
        y:{{display:true,ticks:{{color:'#5a6480',maxTicksLimit:5,font:{{size:9}}}},
           grid:{{color:'rgba(255,255,255,.04)'}}}}
      }},
      onHover:(evt,active)=>{{
        if(active.length && curV!=='3d') {{
          const p=PTS[active[0].index];
          showCard(p,evt.native.clientX,evt.native.clientY);
          if(xdot) xdot.setLatLng([p.lat,p.lon]);
          if(LM) LM.panTo([p.lat,p.lon],{{animate:false}});
        }}
      }}
    }}
  }});
}}

// ── THREE.JS 3D ─────────────────────────────
let T3={{s:null,c:null,r:null,ok:false,run:false}};
let O={{drag:false,rd:false,sx:0,sy:0,az:Math.PI*.3,el:.4,ra:200,px:0,py:0,pz:0}};

function init3D() {{
  if(T3.ok) return; T3.ok=true;
  const cv=document.getElementById('cv3');
  const W=cv.clientWidth, H=cv.clientHeight;
  T3.s=new THREE.Scene(); T3.s.background=new THREE.Color(0x060810);
  T3.s.fog=new THREE.Fog(0x060810,700,2000);
  T3.c=new THREE.PerspectiveCamera(55,W/H,.5,5000);
  T3.r=new THREE.WebGLRenderer({{canvas:cv,antialias:true}});
  T3.r.setPixelRatio(Math.min(window.devicePixelRatio,2));
  T3.r.setSize(W,H,false);
  T3.s.add(new THREE.AmbientLight(0xffffff,.5));
  const sun=new THREE.DirectionalLight(0xffffff,.9);sun.position.set(80,200,80);T3.s.add(sun);
  const fill=new THREE.DirectionalLight(0x4080ff,.3);fill.position.set(-100,-50,-100);T3.s.add(fill);

  const lats=PTS.map(p=>p.lat), lons=PTS.map(p=>p.lon), alts=PTS.map(p=>p.alt);
  const latC=(Math.min(...lats)+Math.max(...lats))/2;
  const lonC=(Math.min(...lons)+Math.max(...lons))/2;
  const altMin=Math.min(...alts), altRange=Math.max(Math.max(...alts)-altMin,1);

  //const LM_=111320, LO_=111320*Math.cos(latC*Math.PI/180);
  const R = 6378137
  const LM_ = Math.PI/180 * R
  const LO_ = LM_ * Math.cos(latC*Math.PI/180)

  const xA=PTS.map(p=>(p.lon-lonC)*LO_);
  const zA=PTS.map(p=>-(p.lat-latC)*LM_);
  const yA=PTS.map(p=>p.alt-altMin);
  const hR=Math.max(Math.max(...xA)-Math.min(...xA),Math.max(...zA)-Math.min(...zA),1);
  
  
  //toto spôsobilo veľmi vysoké body v sekcií 3D v stránke const S=180/hR, AE=Math.max(1,hR/altRange*.3);
  const S=180/hR, AE= 0.4
  
  T3._d={{xA,zA,yA,S,AE}};

  const gs=Math.ceil(hR*S/40)*80;
  T3.s.add(new THREE.GridHelper(gs*2,50,0x0e1520,0x131a24));

  const cL={{low:new THREE.Color(0x00e5a0),mid:new THREE.Color(0x4f9eff),high:new THREE.Color(0xff4d6d)}};
  const pos=[],col=[];
  for(let i=0;i<N;i++) {{
    pos.push(xA[i]*S,yA[i]*AE,zA[i]*S);
    const t=yA[i]/altRange;
    const c=t<.5?cL.low.clone().lerp(cL.mid,t*2):cL.mid.clone().lerp(cL.high,(t-.5)*2);
    col.push(c.r,c.g,c.b);
  }}
  const g=new THREE.BufferGeometry();
  g.setAttribute('position',new THREE.Float32BufferAttribute(pos,3));
  g.setAttribute('color',new THREE.Float32BufferAttribute(col,3));
  T3.s.add(new THREE.Line(g,new THREE.LineBasicMaterial({{vertexColors:true,linewidth:2}})));

  const cP=[];
  for(let i=0;i<N;i++) {{ cP.push(xA[i]*S,yA[i]*AE,zA[i]*S,xA[i]*S,-1,zA[i]*S); }}
  const cG=new THREE.BufferGeometry();
  cG.setAttribute('position',new THREE.Float32BufferAttribute(cP,3));
  T3.s.add(new THREE.LineSegments(cG,new THREE.LineBasicMaterial({{color:0x1a2840,opacity:.3,transparent:true}})));

  const sg=new THREE.SphereGeometry(1.8,16,16);
  const ss=new THREE.Mesh(sg,new THREE.MeshLambertMaterial({{color:0x00e5a0}}));
  ss.position.set(xA[0]*S,yA[0]*AE+2.5,zA[0]*S); T3.s.add(ss);
  const se=new THREE.Mesh(sg,new THREE.MeshLambertMaterial({{color:0xff4d6d}}));
  se.position.set(xA[N-1]*S,yA[N-1]*AE+2.5,zA[N-1]*S); T3.s.add(se);

  const cxW=(Math.max(...xA)*S+Math.min(...xA)*S)/2;
  const czW=(Math.max(...zA)*S+Math.min(...zA)*S)/2;
  const cyW=(Math.max(...yA)*AE)/2;
  O.px=-cxW; O.py=-cyW; O.pz=-czW; O.ra=hR*S*1.3;

  const cv2=document.getElementById('cv3');
  cv2.addEventListener('mousedown',e=>{{O.drag=true;O.rd=e.button===2;O.sx=e.clientX;O.sy=e.clientY;e.preventDefault();}});
  cv2.addEventListener('contextmenu',e=>e.preventDefault());
  window.addEventListener('mousemove',e=>{{
    if(!O.drag) return;
    const dx=(e.clientX-O.sx)*.005,dy=(e.clientY-O.sy)*.005;
    if(O.rd){{ const ca=Math.cos(O.az),sa=Math.sin(O.az);
      O.px+=(-dx*ca-dy*sa)*O.ra*.3; O.pz+=(dx*sa-dy*ca)*O.ra*.3;
    }}else{{ O.az-=dx; O.el=Math.max(-1.3,Math.min(1.3,O.el+dy)); }}
    O.sx=e.clientX; O.sy=e.clientY;
  }});
  window.addEventListener('mouseup',()=>O.drag=false);
  cv2.addEventListener('wheel',e=>{{O.ra=Math.max(8,O.ra*(1+e.deltaY*.0008));e.preventDefault();}},{{passive:false}});
  cv2.addEventListener('mousemove',on3DH);
  cv2.addEventListener('mouseleave',hideCard);
  T3.run=true;
}}

function on3DH(e) {{
  if(!T3.c||!T3._d) return;
  const rect=e.target.getBoundingClientRect();
  const mx=((e.clientX-rect.left)/rect.width)*2-1;
  const my=-((e.clientY-rect.top)/rect.height)*2+1;
  const d=T3._d; const v=new THREE.Vector4();
  let best=-1,bd=0.01;
  for(let i=0;i<N;i+=2) {{
    v.set(d.xA[i]*d.S+O.px,d.yA[i]*d.AE+O.py,d.zA[i]*d.S+O.pz,1);
    v.applyMatrix4(T3.c.matrixWorldInverse);
    v.applyMatrix4(T3.c.projectionMatrix);
    if(Math.abs(v.w)<.001) continue;
    const dd=(v.x/v.w-mx)**2+(v.y/v.w-my)**2;
    if(dd<bd){{bd=dd;best=i;}}
  }}
  if(best>=0) showCard(PTS[best],e.clientX,e.clientY); else hideCard();
}}

function loop3D() {{
  if(!T3.run) return; requestAnimationFrame(loop3D);
  const x=O.ra*Math.cos(O.el)*Math.sin(O.az);
  const y=O.ra*Math.sin(O.el);
  const z=O.ra*Math.cos(O.el)*Math.cos(O.az);
  T3.c.position.set(x+O.px,y+O.py,z+O.pz);
  T3.c.lookAt(new THREE.Vector3(O.px,O.py,O.pz));
  T3.r.render(T3.s,T3.c);
}}

window.addEventListener('resize',()=>{{
  if(T3.r&&T3.c) {{
    const cv=document.getElementById('cv3');
    T3.r.setSize(cv.clientWidth,cv.clientHeight,false);
    T3.c.aspect=cv.clientWidth/cv.clientHeight; T3.c.updateProjectionMatrix();
  }}
  if(LM) LM.invalidateSize();
}});

document.addEventListener('DOMContentLoaded',()=>{{
  initMap(); initChart();
}});
</script>
</body>
</html>"""

    base     = os.path.splitext(os.path.basename(csv_file))[0]
    out_path = os.path.abspath(f"{base}-MAP.html")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(html)
    webbrowser.open(f"file://{out_path}")
    print(f"✅ Mapa otvorená: {out_path}")


# ─────────────────────────────────────────────
#  VÝBER SÚBORU (CSV aj KML)
# ─────────────────────────────────────────────
def select_file():
    if len(sys.argv) > 1:
        p = sys.argv[1]
        if not os.path.isfile(p):
            print(f"❌ Súbor '{p}' neexistuje."); sys.exit(1)
        return p

    # Hľadaj CSV aj KML
    files = sorted(
        f for f in os.listdir(".")
        if f.lower().endswith((".csv", ".kml"))
    )
    if not files:
        print("❌ Žiadny .csv alebo .kml súbor v priečinku.")
        print("   Použi: python KmlToBrowserMap.py track0001.csv")
        sys.exit(1)
    if len(files) == 1:
        print(f"📂 Nájdený súbor: {files[0]}")
        return files[0]

    print("\nDostupné súbory:")
    for i, f in enumerate(files, 1):
        ext = "CSV" if f.lower().endswith(".csv") else "KML"
        print(f"  [{i}] [{ext}] {f}  ({os.path.getsize(f)/1024:.1f} kB)")
    try:
        c = int(input("\nVyber číslo: ")) - 1
        if not 0 <= c < len(files): raise IndexError
        return files[c]
    except (ValueError, IndexError):
        print("❌ Neplatná voľba."); sys.exit(1)


# ─────────────────────────────────────────────
#  MAIN
# ─────────────────────────────────────────────
if __name__ == "__main__":
    path = select_file()
    print(f"📖 Načítavam: {path}")

    # Podpora CSV aj starých KML
    if path.lower().endswith(".csv"):
        points = parse_csv(path)
    else:
        # Fallback KML parser (starý formát)
        print("ℹ️  KML súbor — odporúča sa nový CSV formát pre lepšiu analýzu.")
        import xml.etree.ElementTree as ET
        from math import isnan as _isnan
        points = []
        try:
            root = ET.fromstring(open(path, "rb").read())
        except ET.ParseError as e:
            print(f"❌ KML parse error: {e}"); sys.exit(1)
        last_t = None
        for elem in root.iter():
            if elem.tag.endswith("coordinates") and elem.text:
                for line in elem.text.splitlines():
                    line = line.strip()
                    if line.startswith("<!--") and "UTC" in line:
                        try:
                            c2 = line.replace("<!--","").replace("-->","").replace("UTC","").strip()
                            h,m,s = map(int, c2.split(":"))
                            last_t = f"{h:02}:{m:02}:{s:02}"
                        except (ValueError, AttributeError):
                            last_t = None
                        continue
                    if "," not in line: continue
                    parts = line.split(",")
                    if len(parts) < 3: continue
                    try:
                        lo,la,al = float(parts[0]),float(parts[1]),float(parts[2])
                        if not(-90<=la<=90 and -180<=lo<=180): continue
                        if la==0 and lo==0: continue
                        points.append({"lat":la,"lon":lo,"alt":round(al,1),
                                       "time":last_t or "","spd":0.0,
                                       "hdop":1.0,"sats":6,"dist":0.0})
                    except ValueError:
                        continue

    if len(points) < 2:
        print(f"❌ Len {len(points)} bodov. Minimum sú 2.")
        sys.exit(1)

    print(f"✅ {len(points)} platných bodov.")
    stats      = calc_stats(points)
    noise_info = detect_noise(points)

    is_noise, noise_reason, spread_m = noise_info

    print(f"\n{'─'*42}")
    print(f"  📏  Vzdialenosť  : {fmt_dist(stats['total_dist'])}")
    print(f"  ⏱   Trvanie      : {fmt_dur(points)}")
    print(f"  ⬆   Max. výška   : {stats['max_alt']:.1f} m")
    print(f"  ⬇   Min. výška   : {stats['min_alt']:.1f} m")
    print(f"  📈  Prevýšenie   : +{stats['elev_gain']:.0f} m / −{stats['elev_loss']:.0f} m")
    if stats["max_speed"] > 0:
        print(f"  ⚡  Max. rýchlosť : {stats['max_speed']:.1f} km/h")
        print(f"  🚶  Priem. rýchl. : {stats['avg_speed']:.1f} km/h")
    if is_noise:
        print(f"\n  ⚠️  GPS ŠUM DETEKOVANÝ!")
        print(f"  ⚠️  {noise_reason}")
        print(f"  ⚠️  Rozptyl bodov: {spread_m:.1f} m")
    print(f"{'─'*42}\n")

    generate_html(points, stats, path, noise_info)
