ESP32 Firmware
==============

This repository contains a custom-maintained variant of the Tinkerforge
ESP32 firmware with project-specific adaptations for WARP operation.

Repository Overview
-------------------

Upstream base:

- `Tinkerforge esp32-firmware <https://github.com/Tinkerforge/esp32-firmware>`__

Custom changes so far compared to upstream/master
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- Day-ahead pricing support for WARP 1, including cost calculation.
- Charge tracker extensions:
  implement a dynamic pricing history and allow negative prices for price calculation.
- Web UI improvements:
  add a charge history chart based on uPlot showing dynamic price, charge power and charge cost
  per time unit for a given charge.
- Charging API and MQTT extensions:
  add the ability to start a charge by user name/id.
- NFC usability improvements:
  add names to the saved NFC tags to be able to distinguish them in the UI.
- NTP and time-sync robustness improvements:
  NTP server rotation was added to speed up an initial time sync. In the original firmware,
  time synchronization hung when the NTP server with the highest priority
  had connectivity problems. This was problematic especially when using dynamic prices.
- Remote Syslog implementation.
- Minor Windows build setup fixes.


Repository Content
------------------

provisioning/:
 * Scripts for mass provisioning of ESP32 modules

software/:
 * build/: Compiled files will be put here
 * src/: Source of the firmware, including modules
 * web/: Source of the web interface, including modules

Software
--------

See software/README.txt for build instructions.
More documentation on how the software and the build process work
will follow in the future. For now, in brief:

* The software is built with https://platformio.org/
* For each variation of the firmware (warp, warp2, esp32, esp32_ethernet, ...)
  there is a corresponding *.ini file specifying the PlatformIO environment used
  to build that variation. The environments mostly differ in the backend (i.e.
  firmware) and frontend (i.e. web interface) modules selected to be compiled
  into the firmware.
* Custom hooks compile the web interface from TypeScript and Sass into JavaScript
  and CSS, place everything in one HTML file, zip it and create a C header that
  is then compiled in the firmware.
* After the firmware is built in the software/build folder, the custom hooks
  merge the firmware, bootloader, partition table, etc. into one bin file that
  can be flashed on the ESP32 Brick at offset 0x1000 or can be uploaded to a
  running WARP Charger.
