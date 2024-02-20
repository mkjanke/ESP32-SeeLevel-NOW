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

// Create JSON doc from sensor buffer and forward to ESP-NOW queue
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

  // Publish to ESP-NOW with topic tank0/s
  //
  // Sending unit values as string of numbers.
  // 180 159 11 0 0 6 45 214 230 220 202 255
  //
  // Send calculated checksum along, so receiver can decide to keep reading or not.
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
