# ZWUtil
ZWUtil is a Linux utility for controllers/gateways that communicates with a Z-Wave device via SerialAPI.

Present functionality is to query the Z-Wave device for version info and print the information to the console. It also has the ability to send a proprietary/custom command and receive a response. An example is provided to implement a proprietary command handler on the serialAPI firmware side to retrieve version information. 

# Setup
   1. Make sure the target device is flashed with a SerialAPI firmware image. Note that the UZB dongles (UZB, UZB-7, etc.) should be compatible.
   2. Clone the repo on your target or in your build environment.
   3. Use the makefile to build the project. If you are using a Raspberry Pi, you should just be able to go to the folder and type "make". You can also "make debug" which generates more console output for debugging purposes.
   4. Run the program, targeting the port for the SerialAPI device. If directly connected to the Raspberry Pi hardware unit, the port will be "/dev/ttyAMA0". If you are connected via a USB device (UZB, Wireless Starter Kit, etc.) it will probably be "/dev/ttyACM0".
   5. To implement the proprietary/custom commands, refer to cmd_handler.c in this repo, specifically the implementations of FUNC_ID_PROPRIETARY_0 (cmd F0) and FUNC_ID_PROPRIETARY_1 (cmd F1). Tested with SerialAPI xxxxx, SSDK 2024.12.2.

#Examples
## Help Message
```
$./exe/ZWUtil -h                                        

./exe/ZWUtil
OPTIONS
 -h        Print this help message.
 -v        Print the software version defined in the application.
 -u <uart port, e.g. /dev/ttyACM0>
 --info    Query the SerialAPI device and provide details to the console.
 --cmd <ASCII hex command string> Allows verification/running Z-Wave commands.
```
## Info
```
$./exe/ZWUtil -u /dev/ttyACM0 --info
Querying SerialAPI device on /dev/tty.usbmodem0004401457991...
SerialAPI protocol version=10
Primary Controller     
Nodes: 001
chip_type=0x8, chip_version=0x0
SerialAPI Ver=7.22
Product Type/Product ID: 0x4/0x4
```
## Proprietary Command (F0 - ApplicationProperties.app.version )
In this case, it's returning major version 7, minor version 22, patch version 1.
```
Send proprietary cmd and receive response. CMD: 0xf0 
Querying SerialAPI device on /dev/ttyACM0...
0x01 0x16 0x07 0x00 
```
## Proprietary Command (F1 - bootloaderInfo.version )
In this case, it's returning major version 2, minor version 5, customer main version 0.1.
```
Send proprietary cmd and receive response. CMD: 0xf0 
Querying SerialAPI device on /dev/ACM0...
0x01 0x00 0x05 0x02 
```
# Credits
This code was adapted from Dr. ZWave's [BasicOnOff project](https://github.com/drzwave/BasicOnOff).

# Contacts
- Kris Young - kris.young@silabs.com

# License
This is an AS-IS example project. The code in ZWUtil.c is licensed under an MIT license (see the [LICENSE](LICENSE) file for details. Code in cmd_handlers.c is licensed under a ZLIB license by Silicon Labs, Inc.
