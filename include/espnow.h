#ifndef ESPNOW_H
#define ESPNOW_H

#include <ArduinoJson.h>
#include "settings.h"
#include <WiFi.h>
#include <esp_now.h>

// Initialize ESP_NOW interface. Call once from setup()
bool initEspNow();

// Queue message to send_to_EspNow_queue (const char *)
bool espNowSend(const char *);
// Queue message to send_to_EspNow_queue (std::string&)
bool espNowSend(const std::string &);
// Queue message to send_to_EspNow_queue (JSON formatted message)
bool espNowSend(const JsonDocument &);

#endif