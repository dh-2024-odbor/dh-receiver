# Hopper receiver

The entry point of the firmware is in the `main.c` file.
It includes an `app_main` function which is first function that gets called
when the device boots up.
The `lora.c` file contains initialization code for the LoRa radio and
the deduplication logic to prevent sending duplicate messages.

The firmware is developed using ESP-IDF v5.1-dirty (stable) (Espressif IoT Development Framework)
which is standard for the ESP32 series of microcontrollers that we use.
The source code is well documented, so if you have a burning desire
for more information feel free to read it.
There is a `components` folder which includes the driver code for LoRa radio.

Finally the `sdkconfig` file includes all the configuration for the esp32 device.
Some of this information is included in the bootloader binary to make the microcontroller
function properly, some is used merely as configuration for interfacting with the device
via serial.

Key component description:

- `esp_http_client`: The main component used for sending HTTP requests. It is configured with a URL, an event handler, a root certificate for HTTPS, and other settings.
- `lora_receive_task`: Task which listens for new LoRa packets and adds them to the queue for further processing.
- `http_transmit_task`: This task listens for data in the LoRa packet queue and sends the packets to a defined web server.
- `lora_packet_t`: This is a struct that defines a LoRa packet. It includes a payload and a payload size. Lora packets are sent as a byte array so the data size is as low as possible.
- `s_lora_queue_handler`: This is a queue for storing received LoRa packets.

## Overview

The program includes the following functionalities:

- Initialization of the NVS (Non-Volatile Storage) which allows the WiFi driver to store the WiFi configuration in flash memory.
- Initialization of the LoRa driver with a balanced configuration for range, speed, and power consumption.
- Creation of a queue for received LoRa packets.
- Connection to a WiFi network. This can be configured to connect to any other network.
- Creation of a LoRa receive task that listens for new LoRa packets and adds them to the queue for further processing.
- Creation of an HTTP transmit task that listens for data in the LoRa packet queue and sends the packets to a defined web server.

## Implementation details

Receiver device is constantly listening for incoming LoRa packets. When a packet is received, it is added to a queue.
The HTTP task is constantly checking the queue for new packets. When a packet is found, it is sent to the server for further processing.

When the device boots up it performs all necessary peripheral initialization,
then tries to connect to the wifi network.
When the connection is established successfully, we create the two
tasks (http_transmit and lora_receive) which implement the core logic of our device.

NOTE: More detailed description of how the LoRa radio works
is included in the `dh-sender` repository source code
and its README.md file.

## Usage
To use this program, you need to have the ESP-IDF (Espressif IoT Development Framework) installed and configured on your system. You can then compile and flash the program to your ESP32 device using the idf.py tool. You also need to set environment specific variables like WiFi SSID and password, LoRa frequency, and server URL.
```
