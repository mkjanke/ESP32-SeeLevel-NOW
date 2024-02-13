/* ESP32 Test/Demo app: Read Garnet SeeLevel Tank Sensor/Sender

Inspired by Jim G.: https://forums.raspberrypi.com/viewtopic.php?t=119614

See: https://github.com/mkjanke/ESP-SeeLevel-Test for more on how this works.

*/
#include "settings.h"
#include <Arduino.h>
#include "seelevel.h"
#include "espnow.h"

SeelevelInterface SeelevelGauges(SeeLevelReadPIN, SeeLevelWritePIN);

// Create JSON doc and forward to ESP-NOW queue
bool createAndSendJSON(const std::string& deviceName, int tank, byte *sensorBuffer) {
  char _ESPbuffer[ESP_BUFFER_SIZE] = {0};
  StaticJsonDocument<ESP_BUFFER_SIZE * 2> doc;

  char buffer[4];
  
  std::string output;
  for(int i=0; i < 12; i++){
    snprintf(buffer, sizeof(buffer), "%d", sensorBuffer[i]);
    output += buffer;
    output += " ";
  }
  Serial.println(output.c_str());

  std::string tankName = "tank" + std::to_string(tank);
  doc["D"] = DEVICE_NAME;
  doc[tankName + "/s"] = output;
  
  if (serializeJson(doc, _ESPbuffer) <= ESP_BUFFER_SIZE) {
    bool result = espNowSend(_ESPbuffer);
    doc.clear();
    return result;
  }
  doc.clear();
  return false;
}


void setup() {
  delay(5000);
  _SerialOut.begin(115200);
  _SerialOut.println("\n\nTank Level is Woke!");
  // Start esp-now
  WiFi.mode(WIFI_STA);
  if (!initEspNow()) {
    _SerialOut.println("\nESP-NOW Init Failed");
    return;
  };
  if (!SeelevelGauges.init()){
    _SerialOut.println("\nSeelevel Init Failed");
    return;
  };
}

void loop() {
  
  // Store data from sensor read
  byte gaugeReading[12];

  for (auto t = 0; t < 3; t++) {
    _SerialOut.print("Tank " + (String)t + ": ");
    int retval = SeelevelGauges.readLevel(t, gaugeReading);

    for (auto i = 0; i < 12; i++) {
      // _SerialOut.print(SeeLevelData[t][i]);
      _SerialOut.print(gaugeReading[i]);
      _SerialOut.print(' ');
    }

    if (retval != -1) {
        _SerialOut.print("Checksum: ");
        _SerialOut.print(retval);
        _SerialOut.println(" OK");
      } else {
        _SerialOut.print(" byteSum % 256 - 1 = ");
        _SerialOut.print(" Checksum: ");
        _SerialOut.print(retval);
        _SerialOut.println(" Not OK");
      }
      createAndSendJSON(DEVICE_NAME, t, gaugeReading);

    // Bus must be pulled low for some time before attempting to read another sensor
    delay(1000);
    // Clear the sensor data array
    for (auto i = 0; i < 12; i++) {
      gaugeReading[i] = 0;
    }

  }

  delay(10000);  // wait between read cycles
  
}
