# Snapcast client on a micro controller

This project is based of [pschatzmann/arduino-snapclient](https://github.com/pschatzmann/arduino-snapclient) and prior work of others.

# The plan
- [x] wifi network loudspeaker which receives data from a [Snapcast server](https://github.com/badaix/snapcast)
- [x] ESP32 use ethernet instead of WiFi (WT32-eth01)
- [x] interface with low power I2S amplifier (MAX98357A)
- [x] OTA via ethernet (only possible shortly after boot)
- [ ] interface with high power I2S amplifier (TAS5713)
- [ ] synchronize with other speakers
- [ ] create PCB to amp-board powering everything from one power supply 12-20V

# optional
- [ ] RP2040 use ethernet (W5500-EVB-PICO, RP2040-ETH Mini)
- [ ] allow OTA at all times (see [RTOS example](https://github.com/SensorsIot/ESP32-OTA/issues/8#issuecomment-689524162))