# GrottoTime
Arduino sketch for my simple network-connected temperature monitor &amp; sunrise calculator.

The sketch runs on an Ethernet-connected Arduino (Mega) and interacts with the web-hosted PHP script.

It does the following:
1. Checks some weather details: current temperature, humidity, pressure, estimates dew point
2. Checks current time via NTP & updates to battery-backed RTC module
3. Calculates estimated sunrise & sunset times based on location (hardcoded, but could've been checked via GPS module)
4. Activates LCD backlight if it detects the light on (via LDR) or it detects movement (PIR sensor)
5. Uploads the data via HTTP POST to a website where a corresponding PHP script resides
6. Stores data to EEPROM so it remembers min/max after a restart

Nothing super cool or that hasn't been done before and much better.
