/* ESP32 Test/Demo app: Read Garnet SeeLevel Tank Sensor/Sender

Broadcast tank reading via ESP-NOW as JSON formatted document.

Inspired by Jim G.: https://forums.raspberrypi.com/viewtopic.php?t=119614

See: https://github.com/mkjanke/ESP-SeeLevel-Test for more on how this works.

*/
#include "settings.h"
#include <Arduino.h>
#include "seelevel.h"
#include "espnow.h"

// 
SeelevelInterface SeelevelGauges;

// Create JSON doc and forward to ESP-NOW queue
bool createAndSendJSON(const std::string& deviceName, int tank, byte *sensorBuffer, int checkSum) {
  char _ESPbuffer[ESP_BUFFER_SIZE] = {0};
  StaticJsonDocument<ESP_BUFFER_SIZE * 2> doc;

  char buffer[4];
  
  std::string output;
  for(int i=0; i < 12; i++){
    snprintf(buffer, sizeof(buffer), "%d", sensorBuffer[i]);
    output += buffer;
    output += " ";
  }

  // Publish to ESP-NOW with topic tank0/s
  // Sender values as string of numbers.
  // 180 159 11 0 0 6 45 214 230 220 202 255
  // Send calculated checksum along, so reciever can decide to keep reading or not.
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

void readSeeLevelTank(int tank){
  SeelevelGauges.readTank(tank);
}

void setup() {
  delay(5000);
  _SerialOut.begin(115200);
  _SerialOut.println("\n\nTank Level is Woke!");
  // Start esp-now
  WiFi.mode(WIFI_STA);
  if (!initEspNow()) {
    return;
  };
  if (!SeelevelGauges.init()){
    _SerialOut.println("\nSeelevel Init Failed");
    return;
  };
}

void loop() {
  
  // SeelevelGauges.readTank(1);
  // readSeeLevelTank(1);

  delay(10000);  // wait between read cycles
  
}
