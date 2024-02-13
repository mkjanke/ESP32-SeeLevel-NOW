#ifndef SEELEVEL_H
#define SEELEVEL_H

#include "settings.h"
#include <Arduino.h>
#include "driver/rmt.h"
#include "esp_err.h"

// Clock divisor (base clock is 80MHz)
#define CLK_DIV 80

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

  // RMT configuration
  RingbufHandle_t rb = NULL;  
  rmt_config_t rmt_rx_config = {
        .rmt_mode = RMT_MODE_RX,
        .channel = RMT_CHANNEL_4,                     // S3 must use 0-3 for TX, 4-7 for RX
        .gpio_num = SeeLevelReadPIN,
        .clk_div = CLK_DIV,                           // 1 us?
        .mem_block_num = 2,                           // 64 * 2 32-bit items.
        .flags = RMT_CHANNEL_FLAGS_INVERT_SIG,
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
      return true;
    }

    //
    // Read an individual tank level
    // Pass parameter 't', where
    //    t = 0 SeeLevel tank 1 (Normally Fresh Tank)
    //    t = 1 SeeLevel tank 2 (Normally Grey Tank)
    //    t = 2 SeeLevel tank 3 (Normally Black Tank)
    //
    // Passed variable (t) is 0, 1 or 2
    // Store bytes into buffer passed by caller

    int readLevel(int t, byte *buffer) {

      // _SerialOut.println("Ring Buffer");
      rmt_get_ringbuf_handle(rmt_rx_config.channel, &rb);

      // Power the sensor line for 2.4 ms so tank levels can be read
      digitalWrite(_SeeLevelWritePIN, HIGH);
      delayMicroseconds(SEELEVEL_POWERON_DELAY_MICROSECONDS);

      // 1, 2 or 3 low pulses to select Fresh, Grey, Black tank
      for (int i = 0; i <= t; i++) {
        digitalWrite(_SeeLevelWritePIN, LOW);
        delayMicroseconds(SEELEVEL_PULSE_LOW_MICROSECONDS);
        digitalWrite(_SeeLevelWritePIN, HIGH);
        delayMicroseconds(SEELEVEL_PULSE_HIGH_MICROSECONDS);
      }
      // _SerialOut.println("rmt_rx_start");
      ESP_ERROR_CHECK(rmt_rx_start(rmt_rx_config.channel, false));


      // Sensor should respond in approx 7ms. Do nothing for 5ms.
      vTaskDelay( 5 / portTICK_PERIOD_MS);

      // Read 12 bytes from sensor, store in array
      // Disable interrupts during sensor read.
      // Should take approx 13.5ms. for normal sensor read.
      // Note that this may not be reliable in all cases.

      // Wait for HIGH on read pin
      while (!digitalRead(SeeLevelReadPIN) == HIGH) {
        vTaskDelay( 1 / portTICK_PERIOD_MS);

      }
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