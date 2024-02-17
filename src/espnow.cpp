/*
  Task writeToEspNow:
    Dequeue data from ESP-NOW queue, forward to ESP-NOW:
      send_to_EspNow_Queue --> writeToEspNow() --> Outgoing ESP-NOW packet

  Task espnowHeartbeat:
     Send uptime and stack data to send_to_EspNow_Queue
*/
#include "espnow.h"
#include <Arduino.h>

extern void readSeeLevelTank(int);

// MAC Address of ESP_NOW receiver (broadcast address)
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

TaskHandle_t xhandleEspNowReadHandle = NULL;
TaskHandle_t xhandleEspNowWriteHandle = NULL;
TaskHandle_t xhandleHeartbeat = NULL;

static QueueHandle_t recieve_from_EspNow_Queue;
static QueueHandle_t send_to_EspNow_queue;

char uptimeBuffer[12];  // scratch space for storing formatted 'uptime' string

// Calculate uptime & populate uptime buffer for future use
void uptime() {
  // Constants for uptime calculations
  static const uint32_t millis_in_day = 1000 * 60 * 60 * 24;
  static const uint32_t millis_in_hour = 1000 * 60 * 60;
  static const uint32_t millis_in_minute = 1000 * 60;

  uint8_t days = millis() / (millis_in_day);
  uint8_t hours = (millis() - (days * millis_in_day)) / millis_in_hour;
  uint8_t minutes = (millis() - (days * millis_in_day) - (hours * millis_in_hour)) / millis_in_minute;
  snprintf(uptimeBuffer, sizeof(uptimeBuffer), "%2dd%2dh%2dm", days, hours, minutes);
}


// Callback function for messaged recieved from ESP-NOW
// Copy message to serial queue
// xQueueSend copys ESP_BUFFER_SIZE bytes to queue
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len <= ESP_BUFFER_SIZE) {
    if (xQueueSend(recieve_from_EspNow_Queue, (void *)incomingData, 0) != pdTRUE) {
      Serial.println("Error sending to queue");
    }
  }
}

// Tasks
// Dequeue message from ESP_NOW receive queue and process command
void readfromEspNow(void *parameter) {
  char receiveBuffer[ESP_BUFFER_SIZE];
  StaticJsonDocument<ESP_BUFFER_SIZE + 32> doc;

  for (;;) {
    vTaskDelay(10 / portTICK_PERIOD_MS);
    // Dequeue
    if (xQueueReceive(recieve_from_EspNow_Queue, receiveBuffer, portMAX_DELAY) == pdTRUE) {
      // Stuff ESP_NOW data into JSON doc
      // 'doc' automatically cleared by deserializeJson function
      DeserializationError err = deserializeJson(doc, receiveBuffer);
      if (err) {
        _SerialOut.print("deserializeJson() failed: ");
        _SerialOut.println(err.c_str());
      } else {
        // Valid JSON doc, now test if relevent to us
        if (doc.containsKey("D") && doc.containsKey("CMD"))
          if (doc["D"] == DEVICE_NAME) {
            String command = doc["CMD"];
            int tank = doc["Param"];
            // _SerialOut.printf("%s %d\n", command, tank);
            readSeeLevelTank(tank);
          }
      }
      for (size_t _i = 0; _i < ESP_BUFFER_SIZE; _i++) {
        receiveBuffer[_i] = 0;
      }
    }
  }
}

// Read from ESP Send queue and forward to ESP-NOW
// ToDo check string length and send only length bytes
// Check and print ESP error message
void writeToEspNow(void *parameter) {
  char sendBuffer[ESP_BUFFER_SIZE];
  for (;;) {
    vTaskDelay(10 / portTICK_PERIOD_MS);
    // Dequeue
    if (xQueueReceive(send_to_EspNow_queue, sendBuffer, portMAX_DELAY) == pdTRUE) {
      // _SerialOut.print("writeToEspNow");
      // _SerialOut.println(sendBuffer);
      esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)sendBuffer, ESP_BUFFER_SIZE);
    }
  }
}

// Send heartbeat out to ESP_NOW broadcastAddress
void espnowHeartbeat(void *parameter) {
  for (;;) {
    uptime();
    {
      char buffer[ESP_BUFFER_SIZE] = {0};
      StaticJsonDocument<ESP_BUFFER_SIZE> doc;
      doc["D"] = DEVICE_NAME;
      doc["T"] = uptimeBuffer;
      doc["R"] = uxTaskGetStackHighWaterMark(xhandleHeartbeat);
      doc["W"] = uxTaskGetStackHighWaterMark(xhandleEspNowWriteHandle);
      doc["S"] = uxTaskGetStackHighWaterMark(xhandleEspNowReadHandle);
      doc["Q"] = uxQueueMessagesWaiting(send_to_EspNow_queue);
      doc["H"] = esp_get_free_heap_size();
      doc["M"] = esp_get_minimum_free_heap_size();

      serializeJson(doc, buffer);  // Convert JsonDoc to JSON string
      if (!espNowSend(buffer)) {
        _SerialOut.print("Error sending data: ");
      }
    }
    vTaskDelay(HEARTBEAT / portTICK_PERIOD_MS);
  }
}

// Functions

// Queue message to send_to_EspNow_queue
bool espNowSend(const char *charMessage) {
  char buffer[ESP_BUFFER_SIZE] = {0};
  if (strlen(charMessage) <= ESP_BUFFER_SIZE - 1) {
    strncpy(buffer, charMessage, ESP_BUFFER_SIZE);  // copy incoming char[] into cleared buffer
    if (xQueueSend(send_to_EspNow_queue, buffer, 0) == pdTRUE) {
      return true;
    } else {
      _SerialOut.println("Error sending to queue");
      return false;
    }
  }
  return false;
}

bool espNowSend(const std::string &stringMessage) {
  char charMessage[ESP_BUFFER_SIZE] = {0};
  if (stringMessage.size() <= ESP_BUFFER_SIZE) {
    std::copy(stringMessage.begin(), stringMessage.end(), charMessage);
    if (xQueueSend(send_to_EspNow_queue, charMessage, 0) == pdTRUE) {
      return true;
    } else {
      _SerialOut.println("Error sending to queue");
      return false;
    }
  }
  return false;
}

bool espNowSend(const JsonDocument &doc) {
  char jsonMessage[ESP_BUFFER_SIZE] = {0};
  if (serializeJson(doc, jsonMessage, ESP_BUFFER_SIZE) <= ESP_BUFFER_SIZE) {
    if (xQueueSend(send_to_EspNow_queue, jsonMessage, 0) == pdTRUE) {
      return true;
    } else {
      _SerialOut.println("Error sending to queue");
      return false;
    }
  }
  return false;
}

// Initialize ESP_NOW interface. Call once from setup()
bool initEspNow() {
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    _SerialOut.println("Error initializing ESP-NOW");
    return false;
  } else {
    _SerialOut.println("ESP-NOW initialized");
  }

  // Register Callbacks
  esp_now_register_recv_cb(OnDataRecv);

  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    _SerialOut.println("Failed to add peer");
    return false;
  }
  // Set up queues and tasks
  recieve_from_EspNow_Queue = xQueueCreate(ESPNOW_QUEUE_SIZE, ESP_BUFFER_SIZE);
  if (recieve_from_EspNow_Queue == NULL) {
    _SerialOut.println("Create Queue failed");
    return false;
  }
  send_to_EspNow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, ESP_BUFFER_SIZE);
  if (send_to_EspNow_queue == NULL) {
    _SerialOut.println("Create Queue failed");
    return false;
  }
  xTaskCreate(espnowHeartbeat, "Heartbeat Handler", 2400, NULL, 4, &xhandleHeartbeat);
  xTaskCreate(readfromEspNow, "ESP_NOW Read Handler", 4800, NULL, 4, &xhandleEspNowReadHandle);
  xTaskCreate(writeToEspNow, "ESP_NOW Write Handler", 2000, NULL, 4, &xhandleEspNowWriteHandle);

  return true;
}
