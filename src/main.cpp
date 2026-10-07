#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <EthernetENC.h>
#include <EthernetUdp.h>

// ENC28J60 SPI pins for ESP32-C3 SuperMini.
// Verify these against your particular SuperMini board revision.
#define ETH_SCK   4
#define ETH_MISO  5
#define ETH_MOSI  6
#define ETH_CS    7

#define LED_PIN 1
#define WIFI_LED_PIN 9
#define BUTTON_PIN 10

// Wi-Fi credentials are injected from platformio.ini / wifi_secrets.ini build flags.
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

const char *wifiSsid = WIFI_SSID;
const char *wifiPassword = WIFI_PASSWORD;

// Target Z97 Ethernet MAC.
// TODO: Replace with the MAC address of your server.
byte targetMac[] = {
    0x78, 0x24, 0xAF, 0x45, 0x32, 0x48
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
AsyncWebServer server(80);
unsigned long lastHeartbeatMs = 0;
int pressDurationMs = 200;
bool ledIsOn = true;
const unsigned long buttonDebounceMs = 40;
bool buttonStableState = HIGH;
bool buttonLastReading = HIGH;
unsigned long buttonLastChangeMs = 0;
bool restartArmed = false;
unsigned long restartAtMs = 0;
const unsigned long restartDelayMs = 3000;

IPAddress subnetBroadcast(192, 168, 50, 255);

void updateWifiStatusLed(wl_status_t wifiStatus)
{
    static bool initialized = false;
    static bool lastIsAlertPattern = false;
    static uint8_t disconnectedPhase = 0;
    static bool ledState = LOW;
    static unsigned long phaseStartMs = 0;

    const bool isAlertPattern = (wifiStatus != WL_CONNECTED);

    const unsigned long disconnectedDurationsMs[] = {
        220,  // pulse 1 ON
        220,  // pulse 1 OFF
        220,  // pulse 2 ON
        1200  // short pause (OFF)
    };
    const bool disconnectedStates[] = {HIGH, LOW, HIGH, LOW};

    const unsigned long defaultBlinkOnMs = 200;
    const unsigned long defaultBlinkOffMs = 8000;

    unsigned long now = millis();

    if (!initialized) {
        initialized = true;
        lastIsAlertPattern = isAlertPattern;
        phaseStartMs = now;
        if (isAlertPattern) {
            disconnectedPhase = 0;
            ledState = disconnectedStates[disconnectedPhase];
        } else {
            ledState = HIGH;
        }
        digitalWrite(WIFI_LED_PIN, ledState);
        return;
    }

    if (isAlertPattern != lastIsAlertPattern) {
        lastIsAlertPattern = isAlertPattern;
        phaseStartMs = now;
        if (isAlertPattern) {
            disconnectedPhase = 0;
            ledState = disconnectedStates[disconnectedPhase];
        } else {
            ledState = HIGH;
        }
        digitalWrite(WIFI_LED_PIN, ledState);
        return;
    }

    if (!isAlertPattern) {
        const unsigned long intervalMs = ledState ? defaultBlinkOnMs : defaultBlinkOffMs;
        if (now - phaseStartMs >= intervalMs) {
            phaseStartMs = now;
            ledState = !ledState;
            digitalWrite(WIFI_LED_PIN, ledState);
        }
        return;
    }

    const unsigned long intervalMs = disconnectedDurationsMs[disconnectedPhase];
    if (now - phaseStartMs >= intervalMs) {
        phaseStartMs = now;
        disconnectedPhase = (disconnectedPhase + 1) % 4;
        ledState = disconnectedStates[disconnectedPhase];
        digitalWrite(WIFI_LED_PIN, ledState);
    }
}

void armRestartAfterWol()
{
    restartArmed = true;
    restartAtMs = millis() + restartDelayMs;
    Serial.println("ESP restart scheduled in 3 seconds...");
}

void printMac(const byte *mac)
{
    for (int i = 0; i < 6; ++i) {
        if (i > 0) {
            Serial.print(":");
        }
        if (mac[i] < 16) {
            Serial.print("0");
        }
        Serial.print(mac[i], HEX);
    }
    Serial.println();
}

void connectWiFi()
{
    if (strlen(wifiSsid) == 0 || strlen(wifiPassword) == 0) {
        Serial.println("Wi-Fi credentials are empty. Set [wifi] ssid/password in wifi_secrets.ini.");
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSsid, wifiPassword);

    // Non-blocking: start connection attempt and return immediately.
    Serial.println("Wi-Fi connect started (non-blocking).");
}

int sendWakeOnLan()
{
    uint8_t packet[102];

    // WoL magic packet: FF FF FF FF FF FF
    memset(packet, 0xFF, 6);

    // Followed by the target MAC repeated 16 times.
    for (int i = 0; i < 16; ++i) {
        memcpy(packet + 6 + (i * 6), targetMac, 6);
    }

    Serial.println("Sending Wake-on-LAN packet...");
    Serial.print("Target MAC: ");
    printMac(targetMac);
    Serial.print("Ethernet link: ");
    Serial.println(Ethernet.linkStatus() == LinkON ? "UP" : "DOWN");

    // Some NIC/firmware combinations listen on UDP 7 or 9 and may react only
    // to limited or subnet-directed broadcast packets. Send all combinations.
    const IPAddress destinations[] = {
        IPAddress(255, 255, 255, 255),
        subnetBroadcast
    };
    const uint16_t ports[] = {7, 9};

    int sentCount = 0;
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (size_t i = 0; i < (sizeof(destinations) / sizeof(destinations[0])); ++i) {
            for (size_t j = 0; j < (sizeof(ports) / sizeof(ports[0])); ++j) {
                if (udp.beginPacket(destinations[i], ports[j]) == 1) {
                    udp.write(packet, sizeof(packet));
                    if (udp.endPacket() == 1) {
                        ++sentCount;
                        Serial.print("WoL frame sent to ");
                        Serial.print(destinations[i]);
                        Serial.print(":");
                        Serial.println(ports[j]);
                    } else {
                        Serial.println("udp.endPacket() failed");
                    }
                } else {
                    Serial.println("udp.beginPacket() failed");
                }
            }
        }
        delay(25);
    }

    Serial.print("WoL send sequence completed. Frames sent: ");
    Serial.println(sentCount);

    return sentCount;
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("ESP32-C3 Wi-Fi API + ENC28J60 WoL sender");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    pinMode(WIFI_LED_PIN, OUTPUT);
    digitalWrite(WIFI_LED_PIN, LOW);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    SPI.begin(ETH_SCK, ETH_MISO, ETH_MOSI, ETH_CS);
    Ethernet.init(ETH_CS);
    Serial.println("Starting Ethernet with static IP...");
    Ethernet.begin(localMac, localIp, IPAddress(192, 168, 50, 1), IPAddress(192, 168, 50, 1), subnet);

    delay(500);
    Serial.print("ESP32 IP: ");
    Serial.println(Ethernet.localIP());
    Serial.print("Subnet broadcast: ");
    Serial.println(subnetBroadcast);
    Serial.print("Target MAC configured: ");
    printMac(targetMac);
    Serial.print("Ethernet link: ");
    Serial.println(Ethernet.linkStatus() == LinkON ? "UP" : "DOWN");
    udp.begin(9);

    connectWiFi();

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "OMV-NAS ESP32 WOL API is running.");
    });

    server.on("/switch", HTTP_POST, [](AsyncWebServerRequest *request) {
        int duration = pressDurationMs;
        if (request->hasParam("duration", true)) {
            int arg = request->getParam("duration", true)->value().toInt();
            if (arg >= 50 && arg <= 1000) {
                Serial.print("Setting duration to: ");
                Serial.print(arg);
                Serial.println(" ms");
                duration = arg;
            }
        }

        ledIsOn = false;
        delay(50);
        digitalWrite(LED_PIN, LOW);

        Serial.println("HTTP trigger: /switch -> sending Wake-on-LAN");
        const int sentCount = sendWakeOnLan();
        if (sentCount > 0) {
            armRestartAfterWol();
        }

        delay(duration);
        ledIsOn = true;
        digitalWrite(LED_PIN, HIGH);

        String response = "{\"status\":\"wol_sent\",\"duration\":" + String(duration) +
                          ",\"frames\":" + String(sentCount) + "}";
        request->send(200, "application/json", response);
    });

    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
    });

    server.begin();

    Serial.println();
    Serial.println("Ready.");
    Serial.println("HTTP POST endpoint over Wi-Fi: /switch");
    Serial.print("Button pin (INPUT_PULLUP): ");
    Serial.println(BUTTON_PIN);
}

void loop()
{
    const wl_status_t wifiStatus = WiFi.status();
    const bool wifiConnected = (wifiStatus == WL_CONNECTED);

    if (millis() - lastHeartbeatMs >= 3000) {
        lastHeartbeatMs = millis();
        Serial.print("Alive. Wi-Fi: ");
        if (wifiConnected) {
            Serial.print("CONNECTED, IP=");
            Serial.println(WiFi.localIP());
        } else {
            Serial.println("NOT CONNECTED");
        }
    }

    bool buttonReading = (digitalRead(BUTTON_PIN) == LOW) ? LOW : HIGH;
    if (buttonReading != buttonLastReading) {
        buttonLastChangeMs = millis();
        buttonLastReading = buttonReading;
    }

    if ((millis() - buttonLastChangeMs) >= buttonDebounceMs &&
        buttonReading != buttonStableState) {
        buttonStableState = buttonReading;
        if (buttonStableState == LOW) {
            Serial.println("Button pressed: sending Wake-on-LAN");
            ledIsOn = false;
            digitalWrite(LED_PIN, LOW);
            const int sentCount = sendWakeOnLan();
            if (sentCount > 0) {
                armRestartAfterWol();
            }
            ledIsOn = true;
            digitalWrite(LED_PIN, HIGH);
        }
    }

    if (restartArmed && static_cast<long>(millis() - restartAtMs) >= 0) {
        Serial.println("Restarting ESP32 now.");
        delay(50);
        ESP.restart();
    }

    updateWifiStatusLed(wifiStatus);

    digitalWrite(LED_PIN, ledIsOn ? HIGH : LOW);
    delay(10);
}
