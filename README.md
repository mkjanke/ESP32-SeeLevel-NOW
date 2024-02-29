## ESP32 app: Read Garnet SeeLevel Tank Sensor/Sender and forward sensor data to ESP-NOW

Inspired by Jim G., who did all the heavy lifting: https://forums.raspberrypi.com/viewtopic.php?t=119614

### Summary of information discovered by Jim G. and documented in the above post:

To trigger the tank sender/sensor, one needs to power the sender with 12V then pull the 12V line to ground in a particular pattern. The sender will respond by pulling the 12V line to ground with a series of pulses that can be interpreted as bytes.

#### Triggering the sender:

Each Garnet SeeLevel tank sender is configured as sensor 1, 2, or 3 by snipping a tab on the sender. A sender will respond when it sees a sequence of pulses to ground equal to its sensor number. Each pulse needs to be approx 85µs wide. Pulse spacing needs to be approximately 300µs.

The sender will respond by pulling the 12V line to ground in a series of pulses. Pulses will either be approximately 13µs wide or 48µs wide. In this application I'm treating the short pulses as '0', long pulses as '1'. (JIm G. did the opposite)

This version uses the ESP32 'RMT' library to detect pulses from the sending units. A simpler version that uses polling and doesn't forward to ESP-NOW is: https://github.com/mkjanke/ESP-SeeLevel-Test

Bytes returned from sender:

    0:      Unknown
    1:      Checksum
    2 - 10: Fill level from each segment of sender (0 - -255)
    11:     Appears to be 255 in all cases

For the segment fill values, a 'full' value will likely be less than 255, apparently depending on tank wall thickness, tank size, and perhaps how well the sender is attached. In my testing, using a 710AR Rev E taped to a water jug, I see 'full' segments  anywhere from 126 to 200. Pressing on a segment with my thumb will cause the sensor to read a higher value. Capacitance, perhaps?

This version uses an ESP32 RMT periphrial (library) to capture pulses sent by sending unit. The 'test' version uses polling instead of RMT, and can be used as a base for building an app that runs on other platforms (I.E. arduino).

https://github.com/mkjanke/ESP-SeeLevel-Test

Testing can be done by temporarily attaching sensor to water jug or by simply touching sensor segments.

Output to Serial port upon successful read:

    Tank 0: 147 121 0 0 0 0 14 108 149 179 184 255 Checksum: 121 OK

### Interfacing 12V sensor with 3.3V ESP32:

A description of the circuit necessary to interface the 3.3V ESP32 with the 12V SeeLevel sensor is [here](./docs/LevelShifter.md). 

The interface, designed by Jim G. of the Raspberry Pi forum, uses a high-side P-channel MOSFET controlled by an ESP32 3.3V pin. Data is read on a second pin via a voltage divider.

A cheap 12V-tolerant logic analyzer (LA1010) was used to assist in debugging.

### Notes:

 * This app doesn't yet attempt to accommodate a trimmed sender or any sender other than the 710AR Rev E.

 * No attempt is made to process the returned data into an actual liquid level. I'm intending that to be done in some other app (perhaps Node-Red).

 * Uses Arduino framework but is only tested on an ESP32 annd uses ESP32 specific code and libraries (RMT).

### TBD

This is not a complete solution. To make this usable, one would have to make sure the interface circuit adequately protects both the ESP32 and sending unit.
