#include <ESP8266WiFi.h>
#include <espnow.h>
#include <EEPROM.h>

#define RELAY_PIN 0
#define LED_PIN 2
#define DEVICE_ID 1

typedef struct {
    uint8_t deviceId;
    uint8_t command;
    uint8_t relayState;
    char message[32];
} esp_now_message;

bool relayState = false;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 30000;

void setup() {
    Serial.begin(115200);

    pinMode(RELAY_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(LED_PIN, LOW);

    EEPROM.begin(512);
    relayState = EEPROM.read(0);
    digitalWrite(RELAY_PIN, relayState);
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != 0) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
    
    esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);

    Serial.println("ESP01 Relay initialized successfully");
    Serial.println("Device ID: ");
    Serial.println(DEVICE_ID);

    sendDiscoveryMessage();

    for(int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        delay(200);
    }
}

void loop() {

    if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
        sendHeartbeat();
        lastHeartbeat = millis();
    }

    delay(100);
}

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
    Serial.print("Sending status: ");
    Serial.println(sendStatus == 0 ? "Success" : "Failed");
}

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
    esp_now_message message;
    memcpy(&message, incomingData, sizeof(message));

    Serial.print("Received message from: ");
    printMacAddress(mac);
    Serial.print(" Device ID: ");
    Serial.print(message.deviceId);
    Serial.print(", Command: ");
    Serial.println(message.command);

    if (message.deviceId == DEVICE_ID || message.deviceId == 0) {
        processCommand(message.command, mac);
    }
}

void processCommand(uint8_t command, uint8_t* senderMac) {
    esp_now_message response;
    response.deviceId = DEVICE_ID;
    response.relayState = relayState;
    strcpy(response.message, "ESP01_RELAY");

    switch (command) {
        case 0:
            relayState = false;
            digitalWrite(RELAY_PIN, LOW);
            digitalWrite(LED_PIN, HIGH);
            saveRelayState();
            response.command = 10;
            Serial.println("Relay OFF");
            break;
        
        case 1:
            relayState = true;
            digitalWrite(RELAY_PIN, HIGH);
            digitalWrite(LED_PIN, LOW);
            saveRelayState();
            response.command = 10;
            Serial.println("Relay ON");
            break;
        
        case 2:
            relayState = !relayState;
            digitalWrite(RELAY_PIN, relayState);
            digitalWrite(LED_PIN, !relayState);
            saveRelayState();
            response.command = 10;
            Serial.println("Relay TOGGLE");
            break;

        case 3:
            response.command = 10;
            Serial.println("Status request received");
            break;
        
        case 4:
            response.command = 11;
            Serial.println("Discovery request received");
            break;

        default:
            return;
    }
    response.relayState = relayState;
    esp_now_send(senderMac, (uint8_t *)&response, sizeof(response));
}

void sendDiscoveryMessage() {
    esp_now_message message;
    message.deviceId = DEVICE_ID;
    message.command = 11;
    message.relayState = relayState;
    strcpy(message.message, "ESP01_RELAY");

    esp_now_send(broadcastAddress, (uint8_t *)&message, sizeof(message));
    Serial.println("Discovery message sent");
}

void sendHeartbeat() {
    esp_now_message message;
    message.deviceId = DEVICE_ID;
    message.command = 12;
    message.relayState = relayState;
    strcpy(message.message, "ESP01_RELAY");

    esp_now_send(broadcastAddress, (uint8_t *) &message, sizeof(message));
    Serial.println("Heartbeat message sent");
}

void saveRelayState() {
    EEPROM.write(0, relayState);
    EEPROM.commit();
}

void printMacAddress(uint8_t *mac) {
    for (int i = 0; i < 6; i++){
        Serial.print(mac[i], HEX);
        if (i < 5) {
            Serial.print(":");
        }
    }
    Serial.println();
}