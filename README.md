# Mongoose OS stepper motor control for ESP32 board

The reason for this existing is to have a control for a stepper motor
that would test the hardware of a Shelly product by continuously pressing
a device button.

Out of this some adjustable form of control for the whole contraption was needed.

## Hardware

- LoLin ESP32 board
- EasyDriver stepper motor cotrol dev board
- 3D printed mechanical devices

## Firmware

- Mongoose OS based ESP32 firmware
- EasyDriver control library
  - PWM control of output, square wave, controllable frequency
  - Steps control control, using a counter and an ISR to count number of steps
- Admin web page that exposed control over these variables

## .env file

In order to provide a way to have secrets replaced in mos.yml and not reveal in public repository create a `.env` properties
file with the following keys. Replace values with what you need.

```
DEVICE_ID=my_device_id
WIFI1SSID=main_wifi_ap_ssid
WIFI1PASS=main_wifi_password
WIFI2SSID=backup_wifi_ap_ssid
WIFI2PASS=backup_wifi_password
APSSID=my_device_ap_ssid
APPASS=my_device_ap_password
```

## justfile

For the sake of trying something new makefile was dropped in favor of justfile.

## References

- [ESP IDF Rotary Encoder](https://github.com/espressif/esp-idf/blob/v4.4.6/examples/peripherals/pcnt/rotary_encoder/components/rotary_encoder/src/rotary_encoder_pcnt_ec11.c)
- [LEDC control](https://docs.espressif.com/projects/esp-idf/en/v4.4.6/esp32/api-reference/peripherals/ledc.html)
- [Pulse counter](https://docs.espressif.com/projects/esp-idf/en/v4.4.6/esp32/api-reference/peripherals/pcnt.html)
- [Rotary encoder example](https://github.com/espressif/esp-idf/tree/v4.4.6/examples/peripherals/pcnt/rotary_encoder)
- [just|justfile](https://github.com/casey/just)
- [yq](https://github.com/mikefarah/yq)
