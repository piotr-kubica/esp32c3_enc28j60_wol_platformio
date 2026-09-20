#include <Arduino.h>
#include <SPI.h>
#include <EthernetENC.h>
#include <EthernetUdp.h>

// ENC28J60 SPI pins for ESP32-C3 SuperMini.
// Verify these against your particular SuperMini board revision.
#define ETH_SCK   4
#define ETH_MISO  5
#define ETH_MOSI  6
#define ETH_CS    7

// Target Z97 Ethernet MAC.
// TODO: Replace with the MAC address of your server.
byte targetMac[] = {
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

// Locally administered MAC for the ENC28J60.
// It only needs to be unique on your direct link.
byte localMac[] = {
    0x02, 0x12, 0x34, 0x56, 0x78, 0x9A
};

// Static IPs for a direct ESP32 <-> Z97 Ethernet connection.
// The Z97 does not need an IP while powered off for the WoL frame,
// but the ESP32 needs a local IP for normal Ethernet initialization.
IPAddress localIp(192, 168, 50, 10);
IPAddress z97Ip(192, 168, 50, 20);
IPAddress subnet(255, 255, 255, 0);

EthernetUDP udp;

void sendWakeOnLan()
{
    uint8_t packet[102];

    // WoL magic packet: FF FF FF FF FF FF
    memset(packet, 0xFF, 6);

    // Followed by the target MAC repeated 16 times.
    for (int i = 0; i < 16; ++i) {
        memcpy(packet + 6 + (i * 6), targetMac, 6);
    }

    Serial.println("Sending Wake-on-LAN packet...");

    // For a direct Ethernet link, send the magic packet as a broadcast.
    udp.beginPacket(IPAddress(255, 255, 255, 255), 9);
    udp.write(packet, sizeof(packet));
    udp.endPacket();

    Serial.println("WoL packet sent.");
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("ESP32-C3 + ENC28J60 Wake-on-LAN");

    SPI.begin(ETH_SCK, ETH_MISO, ETH_MOSI, ETH_CS);
    Ethernet.init(ETH_CS);

    Serial.println("Starting Ethernet with static IP...");

    Ethernet.begin(localMac, localIp, IPAddress(192, 168, 50, 1),
                   IPAddress(192, 168, 50, 1), subnet);

    delay(500);

    Serial.print("ESP32 IP: ");
    Serial.println(Ethernet.localIP());

    Serial.print("Ethernet link: ");
    Serial.println(Ethernet.linkStatus() == LinkON ? "UP" : "DOWN");

    udp.begin(9);

    Serial.println();
    Serial.println("Ready.");
    Serial.println("Send 'w' over Serial Monitor to send WoL.");
}

void loop()
{
    if (Serial.available()) {
        char c = Serial.read();

        if (c == 'w' || c == 'W') {
            sendWakeOnLan();
        }
    }

    delay(10);
}
