# GPS-Tracker
GPS-Tracker - Simple device for track some stuff when u forgot it somewhere xD.
## Updated
# Setup
- Download map (in some codes it works without just with uploading .kml file to google earth) from ```OpenStreetMap``` or ```Google Maps Static API``` of your AREA and resize it to 240x320px with 24Bit without compression
- save .bmp file of your map to SD Card
- change Latitude, Lontitude GPS coordinates as is in your map (it is LAT_MIN, LAT_MAX and LON_MIN, LON_MAX)
# Necesarry devices to use
- Arduino nano or esp32
- Buzzer, SD card module, GPS neo6m
- Oled 0.96" display (in some case)
- 2 Buttons and DHT11 sensor
  - (I recommend esp32 because its 3.3V logic on all pins and arduino nano or uno is 5V logic so use 10 kOhm resistors to power it, and connect pins through arduino pins)
- so please use esp32 for saving time and without ugly wiring.
- Jumper Wires
- TFT st7789 240x320px 2.4 inch display (no touch)
- SD Card (8 GB no higher capacity because it cpuld show you errors)

## I highly reccommend to use this
- ```esp32-SD-KML-GPS-V3.ino```
- or ```esp32-SD-GPS-KML2.ino```
- Then go on a trip and after that unplug your SD card & add this to the ***bottom*** in your .kml file:
```
      </coordinates>
    </LineString>
</Placemark>
</Document>
</kml>
```
- now open ```KmlToBrowserMap.py``` in VS Code
- Install necessary modules with
```
pip install folium kml lxml folium webbrowser
``` 
- and write down ```your``` exact file name of .kml file
- now start ```KmlToBrowserMap.py``` and it will show your way on web map
## Please support my work with Github Star :D
