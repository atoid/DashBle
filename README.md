# DashBle

Ruuvitag BLE beacon based interface to Honda motorcycle ECU. 
See my other github project 'Dash' for Android application to utilize this.

## Honda ECU basics

* typically ECU has 4-pin diagnostics connector under the seat (like in CB500F)
* connector is Sumimoto HM 4-way type, matching male connectors can be found in EBAY
* 3 wires are needed for communication with ECU
    * Ground/GND (GR (green, no tracer))
    * 12V (BL/W (black with a white tracer))
    * bidirectional K-line (O/W (orange with a white tracer))
* K-line serial protocol uses 10400 baud rate and custom messaging
* to connect Ruuvitag UART to the K-line a simple optoisolator circuit can be used

## Interface circuit

Two optocouplers PC817C or any equivalent can be used.

```
Input side

A = anode
K = katode

Output side

C = collector
E = emitter
```

Connecting the wires (OI1 and OI2 are optoisolators)

### ECU side

```

K-line -> OI1 K
12V -> 1 kOhm resistor -> OI1 A
K-line -> OI2 C
GND -> OI2 E
```

### Ruuvitag side

See the pinout from the link below. GPIO.30 and .31 are used for UART (pads 25 and 24).

https://lab.ruuvi.com/pinout/

```
RX = GPIO.30
TX = GPIO.31
3V = pad 17
GND = pad 16

3V -> 100 Ohm resistor -> OI2 A
TX -> OI2 K
3V -> 500 Ohm resistor -> RX
RX -> OI1 C
GND -> OI1 E
```

This will translate Ruuvitag 3V levels to ECU side 12V and also merge TX & RX lines to K-line.

## Software

Code in this repository replaces some of the files under Nordic NRF SDK ble_app_uart example. The original example 
provides simple UART service over BLE that can be used from f.ex. Android phone. New code is added to do the
ECU initialization, basic message handling and sending data upstream over BLE.

## Other projects / information

Lot of useful information from this ECU interfacing project. Some of the Honda ECU data tables are explained.

http://projects.gonzos.net/ctx-obd/

Also some clues about the ECU protocol here:

http://forum.pgmfi.org/viewtopic.php?f=40&t=23654&start=15
