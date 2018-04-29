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

Two optocouplers PC817C or any equivalent can be used. See the image below for schematic.

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
RX = GPIO.30 pad 25
TX = GPIO.31 pad 24
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

This code is tested with Nordic NRF SDK version 12.2.0. Apply the code on top of relevant Nordic NRF SDK files,
build the application, create DFU packet and upload it to Ruuvitag. For more information read Ruuvitag documentation.

If and when you get the app running in Ruuvitag then you can use Nordic nRF UART app in Android phone to connect
to Ruuvitag, which now should show as 'ECU' when scanning.

https://play.google.com/store/apps/details?id=com.nordicsemi.nrfUARTv2

If ECU communication is ok, then you should see messages coming from the Ruuvitag. Then you can check my Dash repo
for a custom motorcycle dash Android app.

## Other projects / information

Lot of useful information in this ECU interfacing project. Some of the Honda ECU data tables are explained.

http://projects.gonzos.net/ctx-obd/

Some clues about the ECU protocol.

http://forum.pgmfi.org/viewtopic.php?f=40&t=23654&start=15

OpenOCD with Raspberry PI

https://chisight.wordpress.com/2016/03/22/openocd-for-programming-nrf51822-via-nothing-but-wires-and-a-raspberry-pi/

## Future development

Replace Ruuvitag with cheap and widely available ESP32 based board. Porting ECU communication to ESP32 should not be a
big task.

Other alternative is to use NRF51822 modules that have antenna and related components assembled on the module board.
These modules can be directly programmed with OpenOCD and Raspberry PI just by wiring four GPIO pins. No modification
to DashBle should be required. Flash Nordic softdevice S132 and DashBle application and it shuold work.

### Ebay NRF51822 modules

Received cheap(est) NRF51 module from Ebay and some initial testing is now complete. The module contans older chip
verson and Nordic softdevice S130 must be used. Also the chip has only 16kB RAM so in the Nordic SDK linker file
the linker script memory description must be changed. Otherwise the code compiles and runs without any issues!
I'll add relevant files to this repo after some more testing and cleanup. This alternative will cost about $5 for
the BLE hardware.

Compared to Ruuvitag the module does not have GPIO31 available in headers so for the UART TX some other pin needs
to be used. For example GPIO3 is available near GPIO30.

These modules can be programmed with OpenOCD, only four wires are needed. From Raspberry PI connect VCC, GND and
GPIO11 -> SWCLK, GPIO25 -> SWDIO to the module board.

### Flashing with OpenOCD

After wiring the module (and compiling OpenOCD with Raspberry native support...) run the following to connect
OpenOCD to the target chip. The output should be like below.

```
$ sudo openocd -f interface/raspberrypi2-native.cfg -c "transport select swd; set WORKAREASIZE 0" -f target/nrf51.cfg

Open On-Chip Debugger 0.10.0+dev-00376-g3d3b45af (2018-04-04-21:11)
Licensed under GNU GPL v2
For bug reports, read
        http://openocd.org/doc/doxygen/bugs.html
BCM2835 GPIO config: tck = 11, tms = 25, tdi = 10, tdo = 9
BCM2835 GPIO nums: swclk = 11, swdio = 25
0
cortex_m reset_config sysresetreq
adapter speed: 1000 kHz
Info : Listening on port 6666 for tcl connections
Info : Listening on port 4444 for telnet connections
Info : BCM2835 GPIO JTAG/SWD bitbang driver
Info : JTAG and SWD modes enabled
Info : clock speed 1001 kHz
Info : SWD DPIDR 0x0bb11477
Info : nrf51.cpu: hardware has 4 breakpoints, 2 watchpoints
Info : Listening on port 3333 for gdb connections
```

In second terminal run telnet and give commands to flash the chip.

```
$ telnet localhost 4444
Trying ::1...
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
Open On-Chip Debugger
> halt
target halted due to debug-request, current mode: Thread
xPSR: 0x61000000 pc: 0x00011434 msp: 0x20003f28
> nrf51 mass_erase
Unknown device (HWID 0x0000008f)
> flash write_image s130_nrf51_2.0.1_softdevice.hex
Padding image section 0 with 2112 bytes
not enough working area available(requested 32)
no working area available, falling back to slow memory writes
wrote 110560 bytes from file s130_nrf51_2.0.1_softdevice.hex in 4.930790s (21.897 KiB/s)
> flash write_image nrf51422_xxac.hex
not enough working area available(requested 32)
no working area available, falling back to slow memory writes
wrote 19988 bytes from file nrf51422_xxac.hex in 0.967458s (20.176 KiB/s)
> reset run
>
```

Now the chip is flashed, up and running. You should see "ECU" device appear in Nordic nRF Connect or nRF UART tool.

## Images

![Interface schematic](images/schema.png?raw=true "Interface schematic")

![Testing with CB500F](images/honda_ruuvi.jpeg?raw=true "Honda and Ruuvitag")

![Ebay NRF51822 module](images/ebay_module.jpg?raw=true "Ebay module")

![Ebay module pinout](images/ebay_pinout.jpg?raw=true "Ebay module pinout")

![Android app](images/android_app.png?raw=true "Android app")


