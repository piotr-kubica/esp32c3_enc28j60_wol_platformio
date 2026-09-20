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

Edit `src/main.cpp` and replace:

    AA:BB:CC:DD:EE:FF

with the Ethernet MAC address of the Z97 server.

On OMV/Linux, find it with:

    ip link

Look for the MAC (`link/ether`) of the Ethernet interface.

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
5. Send `w`.
6. The ESP32 should report `WoL packet sent.`
7. With the Z97 shut down, verify that it powers on.

This first version intentionally uses the serial monitor instead of a physical
button, so the Ethernet/WoL path can be tested independently.

## Notes

The ESP32-C3 SuperMini has no built-in Ethernet PHY, so the ENC28J60 provides
the Ethernet interface.

WoL depends on the Z97 Ethernet controller remaining powered in S5 and being
configured to wake on a magic packet.
