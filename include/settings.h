#ifndef SETTINGS_H
#define SETTINGS_H
#include "hal/gpio_types.h"

#define HEARTBEAT 10000L  // Sensor and WiFi loop delay (ms)
#define DEVICE_NAME "ESP-SEELEVEL"

// ESP32 Write Pin. Set HIGH to power sensors, pulsed to initiate sensor read
const int SeeLevelWritePIN = 7;
// ESP32 Read pin. Will be pulled low by sensors
const gpio_num_t SeeLevelReadPIN = GPIO_NUM_9;

// Which Serial to use for debug output
#define _SerialOut USBSerial
// #define _SerialOut Serial

// Time 12V bus is powered before sending pulse(s) to sensor(s)
#define SEELEVEL_POWERON_DELAY_MICROSECONDS 2450
// Width of pulse sent to sensors
#define SEELEVEL_PULSE_LOW_MICROSECONDS 85
// Time between pulses sent to sensors
#define SEELEVEL_PULSE_HIGH_MICROSECONDS 290
// How long to wait for response before timing out
#define SEELEVEL_PULSE_TIMEOUT_MICROSECONDS 3000

#define ESPNOW_QUEUE_SIZE 10
#define ESP_BUFFER_SIZE 200

#endif  // SETTINGS_H