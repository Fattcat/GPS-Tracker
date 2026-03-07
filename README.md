# <p align="center">Simple tracking GPS with kml & esp32</p>
## <p align="center">Setup</p>

## <p align="center">Necesarry devices to use</p>
-  esp32
- Buzzer, SD card module, SD card, GPS neo6m
- Oled 0.96" display
- 20pcs of jumper wires, 1x beardboard

## <p align="center">connection</p>
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
    - TX --> RX2 (on esp wroom 32U it is pin 16)
    - RX --> TX2 (on esp wroom 32U it is pin 17)
   
## if your GPS does not works, try to check this most common issues:
  - try to switch jumper wires on TX, RX pins,
  - make sure voltage is stable,
  - 

- Then go on a trip and after that unplug your SD card & add this to the ```bottom``` in your .kml file:
- (Now this is fixed -> No need to add at the botton, sometimes it can crash, so be prepared)
```
      </coordinates>
    </LineString>
</Placemark>
</Document>
</kml>
```
## <p align="center">Python code</p>
- in VS Code install necessary modules with ```pip install folium kml lxml webbrowser```
- now open ```KmlToBrowserMap.py``` in VS Code
- start that ```KmlToBrowserMap.py``` (press F5 in VS Code)
- sellect file by number (it will ask you which .csv or .kml file to sellect)
- it will auto-open default seellected browser with MAP

- ! ALWAYS CHECK YOUR WIRING CONNECTION !
- if something wrong hapened, open issue

## Whats fiexed ?
  - STABLE & AUTOMATED GPS connection,
  - added so good webGUI
  - added buzzer for info
