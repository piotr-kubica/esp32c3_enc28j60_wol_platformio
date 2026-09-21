# ESP32-C3 SuperMini + ENC28J60 Wake-on-LAN

PlatformIO project for sending a Wake-on-LAN magic packet from an ESP32-C3
SuperMini through an ENC28J60 Ethernet module.

## Wiring

| ENC28J60 | ESP32-C3 SuperMini |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SCK | GPIO4 |
| SO / MISO | GPIO5 |
| SI / MOSI | GPIO6 |
| CS | GPIO7 |
| INT | Not connected |
| RST | Not connected |

Check the pinout of your particular ESP32-C3 SuperMini before wiring.

## Direct Ethernet connection

This project assumes:

ESP32-C3 + ENC28J60 <-- Ethernet cable --> ASUS Z97 server

No router or switch is required.

The ESP32 uses 192.168.50.10/24. The Z97 can use 192.168.50.20/24
when it is running, but the operating system/IP address is not required
for the WoL packet while the machine is powered off.

## Before uploading

1. Copy `wifi_secrets.example.ini` to `wifi_secrets.ini`.
2. Put your 2.4 GHz Wi-Fi SSID and password in `wifi_secrets.ini`.

The `wifi_secrets.ini` file is gitignored, so credentials are not committed.

Also edit `src/main.cpp` and replace:

    AA:BB:CC:DD:EE:FF

with the Ethernet MAC address of the Z97 server.

On OMV/Linux, find it with:

    ip link

Look for the MAC (`link/ether`) of the Ethernet interface.

On successful Wi-Fi connection, Serial Monitor prints the assigned Wi-Fi IP address.

## BIOS

On the ASUS Z97, enable the setting that allows PCI-E/network wake,
typically:

Advanced -> APM Configuration -> Power On By PCI-E -> Enabled

Also disable ErP if it prevents standby power to the Ethernet NIC.

The exact menu wording can vary by ASUS BIOS version.

## Test

1. Connect the ENC28J60 to the ESP32-C3.
2. Connect the ENC28J60 Ethernet port directly to the Z97 Ethernet port.
3. Upload the project.
4. Open PlatformIO Serial Monitor at 115200 baud.
5. Call the HTTP endpoint:

    curl -X POST "http://<esp32-wifi-ip>/switch"

6. The ESP32 should report WoL frame transmission in Serial Monitor.
7. With the Z97 shut down, verify that it powers on.

Optional custom duration (50-1000 ms):

    curl -X POST -d "duration=300" "http://<esp32-wifi-ip>/switch"

## Notes

The ESP32-C3 SuperMini has no built-in Ethernet PHY, so the ENC28J60 provides
the Ethernet interface.

WoL depends on the Z97 Ethernet controller remaining powered in S5 and being
configured to wake on a magic packet.

## If packet is sent but PC does not wake

1. Verify the target MAC in [src/main.cpp](src/main.cpp) matches the Z97 NIC exactly.
2. In BIOS, keep `Power On By PCI-E` enabled and disable ErP/Deep Sleep modes.
3. In the OS before shutdown:
    - On Windows, disable Fast Startup.
    - In NIC properties, enable `Wake on Magic Packet` and `Shutdown Wake-On-Lan`.
4. Use a full shutdown test:
    - Windows: `shutdown /s /t 0`
    - Linux: `shutdown -h now`
5. Check link LEDs on the Z97 Ethernet port while powered off.
    If LEDs are off, the NIC is not receiving standby power, so WoL cannot work.
6. Keep direct cable connection or use a simple unmanaged switch.

This firmware sends the magic packet multiple times to both UDP ports 7 and 9,
and to both `255.255.255.255` and `192.168.50.255` broadcast addresses.
