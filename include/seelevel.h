#ifndef SEELEVEL_H
#define SEELEVEL_H

#include "settings.h"
#include <Arduino.h>

class SeelevelInterface {
  private:
  // ESP32 Write Pin. Set HIGH to power sensors, pulsed to initiate sensor read
  uint8_t _SeeLevelWritePIN;
  // ESP32 Read pin. Will be pulled low by sensors
  u_int8_t _SeeLevelReadPIN;

  // Scratch pad for storing bytes read from sensor
  byte _SeeLevelData[12];

  public:
    SeelevelInterface(uint8_t readPin, uint8_t writePin) {
        // Set up ESP32 pins
        _SeeLevelReadPIN = readPin;
        _SeeLevelWritePIN = writePin;
        pinMode(_SeeLevelWritePIN, OUTPUT);
        pinMode(_SeeLevelReadPIN, INPUT);
        digitalWrite(_SeeLevelWritePIN, LOW);
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
      portDISABLE_INTERRUPTS();
      for (auto byteLoop = 0; byteLoop < 12; byteLoop++) {
        _SeeLevelData[byteLoop] = readByte();
      }
      portENABLE_INTERRUPTS();
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