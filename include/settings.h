#ifndef SETTINGS_H
#define SETTINGS_H

#define HEARTBEAT 10000L  // Sensor and WiFi loop delay (ms)
#define DEVICE_NAME "ESP-SEELEVEL"

// ESP32 Write Pin. Set HIGH to power sensors, pulsed to initiate sensor read
const int SeeLevelWritePIN = 7;
// ESP32 Read pin. Will be pulled low by sensors
#define SeeLevelReadPIN GPIO_NUM_9

// Which Serial to use for debug output
#define _SerialOut USBSerial
// #define _SerialOut Serial

#define ESPNOW_QUEUE_SIZE 10
#define ESP_BUFFER_SIZE 200

#endif  // SETTINGS_H