# <p align="center">Simple tracking GPS with kml & esp32</p>
## <p align="center">Setup</p>
- Download map (in some codes it works without just with uploading .kml file to google earth) from ```OpenStreetMap``` or ```Google Maps Static API``` of your AREA and resize it to 240x320px with 24Bit without compression
- save .bmp file of your map to SD Card
- change Latitude, Lontitude GPS coordinates as is in your map (it is LAT_MIN, LAT_MAX and LON_MIN, LON_MAX)
## <p align="center">Necesarry devices to use</p>
-  esp32 or Mega2560
- Buzzer, SD card module, GPS neo6m
- Oled 0.96" display (in esp32-SD-KML-GPS-OledV3.ino)
- DHT11 sensor (in Mega2560 case because of more reliable power delivery)
  - (I recommend ```esp32 because its 3.3V logic``` on all pins and ```arduino Mega2560``` or ```uno``` is 5V logic
  - so please use esp32 for saving time and without ugly wiring.
- Jumper wires, beardboard
- TFT st7789 240x320px 2.4 inch display (no touch)
  - This isnt used because it is hard to be power to save
- SD Card (8 GB no higher capacity because it cpuld show you errors)

## <p align="center">connection</p>
- for ```esp32-SD-KML-GPS-OledV3.ino```
  - ### Oled display
    - VCC --> 3.3V
    - GND --> GND
    - SCL --> D22
    - SDA --> D21
 
  - ### SD Card
    - VCC -->  3.3V
    - GND -->  GND
    - MOSI --> D23
    - MISO --> D19
    - SCK -->  D18
    - CS -->   D5

  - ### Buzzer
    - Buzzer pin --> D4

  - ### GPS neo6m
    - VCC -->  3.3V
    - GND -->  GND
    - TX --> RX2
    - RX --> TX2
   
## I highly reccommend to use this
- for esp32 ```esp32-SD-KML-GPS-OledV3.ino```
  - or WITHOUT Oled ```esp32-SD-GPS-KML2.ino```
- for Mega2560 ```Mega2560-SD-KML-GPS-DHT-Oled.ino```
- Then go on a trip and after that unplug your SD card & add this to the ```bottom``` in your .kml file:
```
      </coordinates>
    </LineString>
</Placemark>
</Document>
</kml>
```
## <p align="center">Python code</p>
- now open ```KmlToBrowserMap.py``` in VS Code
- Install necessary modules with
```
pip install folium kml lxml webbrowser
``` 
- and write down ```your``` exact file name of .kml file
- now start ```KmlToBrowserMap.py``` and it will show your way on web map
## Please support my work with Github Star :D
