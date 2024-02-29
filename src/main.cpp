/* ESP32 Test/Demo app: Read Garnet SeeLevel Tank Sensor/Sender

When sent command via ESP_NOW, read tank level
Send result via ESP-NOW as JSON formatted document.

Inspired by Jim G.: https://forums.raspberrypi.com/viewtopic.php?t=119614

See: https://github.com/mkjanke/ESP-SeeLevel-Test for more on how this works.

*/
#include <Arduino.h>

#include "espnow.h"
#include "seelevel.h"
#include "settings.h"

// Create object to manage SeeLevel communications
SeelevelInterface SeelevelGauges;

/*
  Create JSON doc from sensor buffer and forward to ESP-NOW queue

  For 'tank1' on device named ESP-SEELEVEL JSON document will be sent as:
  {
    "D":"ESP-SEELEVEL",
    "tank1/s":"147 3 0 0 0 0 0 10 95 156 255 255 ",
    "tank1/checkSum":3
  }

  D: Device name (in settings.h)
  s: raw data returned from sending unit. Each byte is expressed as int 0<>255, 
     formatted as ASCII chars (not binary bytes)
  checkSum: ASCI representation of integer checksum.

*/
bool createAndSendJSON(const std::string& deviceName, int tank, byte* sensorBuffer, int checkSum) {
  char _ESPbuffer[ESP_BUFFER_SIZE] = {0};
  StaticJsonDocument<ESP_BUFFER_SIZE * 2> doc;

  char buffer[4];

  std::string output;
  for (int i = 0; i < 12; i++) {
    snprintf(buffer, sizeof(buffer), "%d", sensorBuffer[i]);
    output += buffer;
    output += " ";
  }

  // Publish to ESP-NOW
  std::string tankName = "tank" + std::to_string(tank);
  doc["D"] = DEVICE_NAME;
  doc[tankName + "/s"] = output;
  doc[tankName + "/checkSum"] = checkSum;

  if (serializeJson(doc, _ESPbuffer) <= ESP_BUFFER_SIZE) {
    bool result = espNowSend(_ESPbuffer);
    doc.clear();
    return result;
  }
  doc.clear();
  return false;
}

//  Call SeeLevel read method to read single tank
void readSeeLevelTank(int tank) { SeelevelGauges.readTank(tank); }

void setup() {
  delay(5000);
  _SerialOut.begin(115200);
  _SerialOut.println("\n\nTank Level is Woke!");

  // Start esp-now
  WiFi.mode(WIFI_STA);
  if (!initEspNow()) {
    return;
  };

  // Init SeeLevel object
  if (!SeelevelGauges.init()) {
    _SerialOut.println("\nSeelevel Init Failed");
    return;
  };
}

// Do nothing every 10 seconds
void loop() { delay(10000); }
