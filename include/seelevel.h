#ifndef SEELEVEL_H
#define SEELEVEL_H

#include "settings.h"
#include <Arduino.h>
#include "driver/rmt.h"
#include "esp_err.h"

extern bool createAndSendJSON(const std::string&, int, byte *, int );

// Clock divisor (base clock is 80MHz)
// Timing values returned by RMT will be in microseconds
#define CLK_DIV 80

// Number of memory blocks for RMT recieve
// 64 * 2 = 128 32-bit items.
#define RMT_BLOCK_NUM   2

// Time 12V bus is powered before sending pulse(s) to sensor(s)
#define SEELEVEL_POWERON_DELAY_MICROSECONDS 2450
// Width of pulse sent to sensors
#define SEELEVEL_PULSE_LOW_MICROSECONDS 85
// Time between pulses sent to sensors
#define SEELEVEL_PULSE_HIGH_MICROSECONDS 290
// How long to wait for response before timing out
#define SEELEVEL_PULSE_TIMEOUT_MILLISECONDS 30

class SeelevelInterface {
  private:
  // ESP32 Write Pin. Set HIGH to power sensors, pulsed to initiate sensor read
  uint8_t _SeeLevelWritePIN;

  // Scratch pad for storing bytes read from sensor
  byte _SeeLevelData[12];

  // Avoid smashing sensor with overlapping reads and writes
  SemaphoreHandle_t _xTankReadSemaphore =  NULL;

  // RMT configuration
  // Trial RMT config. Some values determined by guesswork. 
  RingbufHandle_t rb = NULL;  
  rmt_config_t rmt_rx_config = {
        .rmt_mode = RMT_MODE_RX,
        .channel = RMT_CHANNEL_4,                     // S3 must use 0-3 for TX, 4-7 for RX
        .gpio_num = SeeLevelReadPIN,
        .clk_div = CLK_DIV,                           // 1 us?
        .mem_block_num = RMT_BLOCK_NUM,
        .flags = RMT_CHANNEL_FLAGS_INVERT_SIG,        // Sending unit pulls to ground for pulses. 
        .rx_config = {
            .idle_threshold = 200,                    // 200 ticks = 200us?
            .filter_ticks_thresh = 255,               // approx 3us?
            .filter_en = true,
            .rm_carrier = false,
            .carrier_freq_hz = 0,                     // ? Not sure if these need to be set or not
            .carrier_duty_percent = 0,                // ?
            .carrier_level = RMT_CARRIER_LEVEL_MAX    // ?
        }};

  public:
    // SeelevelInterface(gpio_num_t readPin, uint8_t writePin) {
    SeelevelInterface() {
        // Set up ESP32 pins
        _SeeLevelWritePIN = SeeLevelWritePIN;
        pinMode(_SeeLevelWritePIN, OUTPUT);
        digitalWrite(_SeeLevelWritePIN, LOW);
    }

    // Set up RMT driver
    int init(){
      ESP_ERROR_CHECK(rmt_config(&rmt_rx_config));
      ESP_ERROR_CHECK(rmt_driver_install(rmt_rx_config.channel, 1000, 0));
          // Initialize semaphores
      _xTankReadSemaphore = xSemaphoreCreateBinary();
      xSemaphoreGive(_xTankReadSemaphore);

      return true;
    }

    // Read a Tank level, calc checksum, conver to JSON, forward to ESP-NOW 
    // Pass parameter 't', where
    //    t = 1 SeeLevel tank 1 (Normally Fresh Tank)
    //    t = 2 SeeLevel tank 2 (Normally Grey Tank)
    //    t = 3 SeeLevel tank 3 (Normally Black Tank)
    //
    // Call readLevel to communicate with sending unit
    //
    void readTank(int tank) {
    if(_xTankReadSemaphore != NULL )
      if (xSemaphoreTake(_xTankReadSemaphore, 100 / portTICK_PERIOD_MS) == pdTRUE) {
        // Store data from sensor read
        byte gaugeReading[12] = {0};

        // // Read each of Tank1, Tank2, Tank3. Pause between each tank read
        int checkSum = readLevel(tank, gaugeReading);
        _SerialOut.printf("Tank %d: ", tank);
        for (auto i = 0; i < 12; i++) {
          // _SerialOut.print(SeeLevelData[t][i]);
          _SerialOut.print(gaugeReading[i]);
          _SerialOut.print(' ');
        }

        if (checkSum != -1) {
          _SerialOut.print("Checksum: ");
          _SerialOut.print(checkSum);
          _SerialOut.println(" OK");
        } else {
          _SerialOut.print(" byteSum % 256 - 1 = ");
          _SerialOut.print(" Checksum: ");
          _SerialOut.print(checkSum);
          _SerialOut.println(" Not OK");
        }
        createAndSendJSON(DEVICE_NAME, tank, gaugeReading, checkSum);
        xSemaphoreGive(_xTankReadSemaphore);
      }

      // Bus must be pulled low for some time before attempting to read another sensor
      delay(1000);
    }
    //
    // Read an individual tank level
    // Passed variable (t) is 1, 2 or 3
    // Store bytes into buffer passed by caller

    int readLevel(int t, byte *buffer) {

      // _SerialOut.println("Ring Buffer");
      rmt_get_ringbuf_handle(rmt_rx_config.channel, &rb);

      // Power the sensor line for 2.4 ms to
      // wake up sending units so tank levels can be read
      digitalWrite(_SeeLevelWritePIN, HIGH);
      delayMicroseconds(SEELEVEL_POWERON_DELAY_MICROSECONDS);

      // 1, 2 or 3 low pulses to select Fresh, Grey, Black tank
      for (int i = 1; i <= t; i++) {
        digitalWrite(_SeeLevelWritePIN, LOW);
        delayMicroseconds(SEELEVEL_PULSE_LOW_MICROSECONDS);
        digitalWrite(_SeeLevelWritePIN, HIGH);
        delayMicroseconds(SEELEVEL_PULSE_HIGH_MICROSECONDS);
      }
      // Sensor should respond in approx 7ms. Do nothing for 5ms.
      vTaskDelay( 5 / portTICK_PERIOD_MS);

      // Start RMT recieve
      ESP_ERROR_CHECK(rmt_rx_start(rmt_rx_config.channel, false));

      // Read 12 bytes from sensor, store in array
      // Should take approx 13.5ms. for normal sensor read.
      while (rb) {

        size_t rx_size = 0;
        rmt_item32_t* item = (rmt_item32_t*)xRingbufferReceive(rb, &rx_size, pdMS_TO_TICKS(SEELEVEL_PULSE_TIMEOUT_MILLISECONDS));

        if (item) {
          byte bitCount = 0;
          byte byteCount = 0;

          for (int i = 0; i < rx_size >> 2; i++) {
            // _SerialOut.printf("%d %d:%dus %d:%dus\n", i, (item+i)->level0, (item+i)->duration0, (item+i)->level1,
            // (item+i)->duration1);

            // Inverted pulses, so .level0 == 1 is low pulse, either binary '1' or '0'
            // depending on length of pulse.
            //    .duration0 approx. 15ms -> binary '0'
            //    .duration0 approx. 50ms. -> binary '1'
            //
            // If valid pulse, shift left & set least bit
            if (((item + i)->level0 == 1) && ((item + i)->duration0 > 10) && ((item + i)->duration0 < 20)) {
              // binary 0
              _SeeLevelData[byteCount] = _SeeLevelData[byteCount] << 1;
              _SeeLevelData[byteCount] |= 0;
              bitCount++;

            } else if (((item + i)->level0 == 1) && ((item + i)->duration0 > 30) && ((item + i)->duration0 < 60)) {
              // binary 1
              _SeeLevelData[byteCount] = _SeeLevelData[byteCount] << 1;
              _SeeLevelData[byteCount] |= 1;
              bitCount++;
            } else {
              // Not a bit - do nothing
              continue;
            }
            if ((bitCount == 8)) {
              // Got a byte
              bitCount = 0;
              byteCount++;
            }
          }
          vRingbufferReturnItem(rb, (void*)item);
        } else {
          break;
        }
      }

      ESP_ERROR_CHECK(rmt_rx_stop(rmt_rx_config.channel));

      vTaskDelay( 1 / portTICK_PERIOD_MS);
      digitalWrite(_SeeLevelWritePIN, LOW);  // Turn power off until next poll

      // Copy data into caller's array
      memcpy(buffer, _SeeLevelData, 12);

      // Verify checksum
      int byteSum = 0;
      for (auto i = 2; i <= 10; i++) {  // Sum data bytes
        byteSum = byteSum + _SeeLevelData[i];
      }
      if (byteSum != 0) {
        // Calculate and compare checksum
        int checkSum = _SeeLevelData[1];
        // Checksum appears to be (sum of bytes % 256) - 1, with special case for checksum 255.
        if (((byteSum % 256) - 1 == checkSum) || (byteSum % 256 == 0 && checkSum == 255)) {
          return checkSum;
        } else {
          return -1;
        }
      } else {
          return -1;
      }
    }

};

#endif