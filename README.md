# GPS-Tracker
GPS-Tracker - Simple device for track some stuff when u forgot it somewhere xD.
## Updated
# Setup
- Download map from ```OpenStreetMap``` or ```Google Maps Static API``` of your AREA and resize it to 240x320px with 24Bit without compression
- save .bmp file of your map to SD Card
- change Latitude, Lontitude GPS coordinates as is in your map (it is LAT_MIN, LAT_MAX and LON_MIN, LON_MAX)
# Necesarry devices to use
- Arduino nano or esp32 
- (I recommend esp32 because its 3.3V logic on all pins and arduino nano or uno is 5V logic so use 10 kOhm resistors to power it, and connect pins through arduino pins)
- so please use esp32 for saving time and without ugly wiring.
- arduino neo 6m GPS MODULE
- Jumper Wires
- TFT st7789 240x320px 2.4 inch display (no touch)
- SD Card (8 GB no higher capacity because it cpuld show you errors)
- 3 buttons for future usage