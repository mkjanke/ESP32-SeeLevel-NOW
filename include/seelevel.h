#ifndef SEELEVEL_H
#define SEELEVEL_H

#include "settings.h"
#include <Arduino.h>
#include "driver/rmt.h"
#include "esp_err.h"

// Clock divisor (base clock is 80MHz)
#define CLK_DIV 80
// Number of clock ticks that represent 10us.  10 us = 1/100th msec.
#define TICK_10_US (80000000 / CLK_DIV / 100000)

class SeelevelInterface {
  private:
  // ESP32 Write Pin. Set HIGH to power sensors, pulsed to initiate sensor read
  uint8_t _SeeLevelWritePIN;
  // ESP32 Read pin. Will be pulled low by sensors
  gpio_num_t _SeeLevelReadPIN;

  // Scratch pad for storing bytes read from sensor
  byte _SeeLevelData[12];

  // RMT configuration
  RingbufHandle_t rb = NULL;  
  rmt_config_t rmt_rx_config = {
        .rmt_mode = RMT_MODE_RX,
        .channel = RMT_CHANNEL_4,  // S3 must use 0-3 for TX, 4-7 for RX
        .gpio_num = _SeeLevelReadPIN,
        .clk_div = CLK_DIV,  // 1 us?
        .mem_block_num = 2,  // 64 * 2 32-bit items.
        .flags = RMT_CHANNEL_FLAGS_INVERT_SIG,
        .rx_config = {
            .idle_threshold = TICK_10_US * 100 * 10,  // 10ms?
            .filter_ticks_thresh = 100,
            .filter_en = true,
            .rm_carrier = false,
            .carrier_freq_hz = 0,                   // ?
            .carrier_duty_percent = 0,              // ?
            .carrier_level = RMT_CARRIER_LEVEL_MAX  // ?
        }};

  public:
    SeelevelInterface(uint8_t readPin, uint8_t writePin) {
        // Set up ESP32 pins
        _SeeLevelReadPIN = (gpio_num_t)readPin;
        _SeeLevelWritePIN = writePin;
        pinMode(_SeeLevelWritePIN, OUTPUT);
        pinMode(_SeeLevelReadPIN, INPUT);
        digitalWrite(_SeeLevelWritePIN, LOW);

        _SerialOut.println("rmt_config");
        ESP_ERROR_CHECK(rmt_config(&rmt_rx_config));

        _SerialOut.println("rmt_driver_install");
        ESP_ERROR_CHECK(rmt_driver_install(rmt_rx_config.channel, 1000, 0));
    }

    int init(){
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

      // Sensor should respond in approx 7ms. Do nothing for 5ms.
      delay(5);

      // Read 12 bytes from sensor, store in array
      // Disable interrupts during sensor read.
      // Should take approx 13.5ms. for normal sensor read.
      // Note that this may not be reliable in all cases.

      const TickType_t readTimeout = 150 / portTICK_PERIOD_MS;  // 15ms?

      // Wait for HIGH on read pin
      while (!digitalRead(_SeeLevelReadPIN) == HIGH) {
        delay(1);
      }
      _SerialOut.println("Ring Buffer");
      rmt_get_ringbuf_handle(rmt_rx_config.channel, &rb);

      _SerialOut.println("rmt_rx_start");
      ESP_ERROR_CHECK(rmt_rx_start(rmt_rx_config.channel, false));

      while (rb) {
        size_t rx_size = 0;
        _SerialOut.println("xRingbufferReceive");
      
        rmt_item32_t* item = (rmt_item32_t*)xRingbufferReceive(rb, &rx_size, readTimeout);
        if (item) {
          byte bitCount = 0;
          byte byteCount = 0;

          for (int i = 0; i < rx_size >> 2; i++) {
            _SerialOut.printf("%d %d:%dus %d:%dus\n", i, (item+i)->level0, (item+i)->duration0, (item+i)->level1,
            (item+i)->duration1);

            // Inverted pulses, so .level0 == 1 is low pulse, either binary '1' or '0'
            // depending on length of pulse.
            // .duration0 approx. 15ms -> binary '0'
            // .duration0 approx. 50ms. -> binary '1'
            //
            if (((item + i)->level0 == 1) && ((item + i)->duration0 > 5) && ((item + i)->duration0 < 20)) {
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
              // printf(" byte %d\n", byteCount);
              bitCount = 0;
              byteCount++;
            }
          }
          vRingbufferReturnItem(rb, (void*)item);
        } else {
          _SerialOut.println("xRingbufferReceive no Items");
          break;
        }
      }

      ESP_ERROR_CHECK(rmt_rx_stop(rmt_rx_config.channel));

      // portDISABLE_INTERRUPTS();
      // for (auto byteLoop = 0; byteLoop < 12; byteLoop++) {
      //   _SeeLevelData[byteLoop] = readByte();
      // }
      // portENABLE_INTERRUPTS();
      delay(1);
      digitalWrite(_SeeLevelWritePIN, LOW);  // Turn power off until next poll


      // Copy data into caller's array
      memcpy(buffer, _SeeLevelData, 12);


      // Verify checksum
      int byteSum = 0;
      for (auto i = 2; i <= 10; i++) {  // Sum data bytes
        // byteSum = byteSum + SeeLevelData[t][i];
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


    //
    // Read individual bytes from SeeLevel sensor
    //
    // Sensor pulses are interpreted as approx:
    //       48 microseconds for digital '1',
    //       13 microseconds for digital '0'
    //
    // Each byte will roughly approximate the fill level of a segment of the sensor 0 <> 255
    //
    // A 'full' sensor segment will show somewhere between 126 and 255, depending on tank wall
    // thickness, other factors.
    //
    // Testing can be done by temporarily attaching sensor to water jug or by simply touching
    // sensor segments.
    //
    byte readByte() {
      int result = 0;
      int thisBit = 0;
      for (auto bitLoop = 0; bitLoop < 8; bitLoop++) {  // Populate byte from right to left,
        result = result << 1;                           // Shift right for each incoming bit

        // Wait for low pulse, timeout if no pulse (I.E. no tank present)
        unsigned long pulseTime = (pulseIn(_SeeLevelReadPIN, LOW, SEELEVEL_PULSE_TIMEOUT_MICROSECONDS));

        // Decide if pulse is logical '0', '1' or invalid
        if ((pulseTime >= 5) && (pulseTime <= 20)) {
          thisBit = 0;
        } else if ((pulseTime >= 30) && (pulseTime <= 60)) {
          thisBit = 1;
        } else {
          return 0;  // Pulse width out of range, Return zero
        }
        // Bitwise OR and shift pulses into single byte
        result |= thisBit;
      }
      return result;
    }


};

#endif