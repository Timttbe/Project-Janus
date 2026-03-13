#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <espnow.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

const char* ssid = "HANKO 2.4_BACK";
const char* password = "Tr@pical8888";

ESP8266WebServer server(80);

typedef struct {
    uint8_t deviceId;
    uint8_t command;
    uint8_t relayState;
    char message[32];
} esp_now_message;

typedef struct {
    uint8_t deviceId;
    uint8_t macAddress[6];
    bool relayState;
    unsigned long lastSeen;
    bool isOnline;
    char deviceType[32];
} discovered_device;

discovered_device devices[10];
int deviceCount = 0;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

unsigned long lastDiscovery = 0;
const unsigned long DISCOVERY_INTERVAL = 60000;
const unsigned long DEVICE_TIEMOUT 120000;

void setup() {
    Serial.begin(115200);
    EEPROM.begin(512);
    
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid, password);
    
    Serial.print("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.println(".");
    }
    Serial.println();
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
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
    
    setupWebServer();

    server.begin();
    Serial.println("Web server started");

    performDiscovery();
}

void loop() {
    server.handleClient();
    
    if (millis() - lastDiscovery > DISCOVERY_INTERVAL) {
        performDiscovery();
        lastDiscovery = millis();
    }

    checkDeviceStatus();

    delay(10);
}

void handleControlAll() {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Body not found");
        return;
    }

    DynamicJsonDocument doc(128);
    deserializeJson(doc, server.arg("plain"));
    String action = doc["action"];

    uint8_t command = 0;
    if (action == "on") command = 1;
    else if (action == "off") command = 0;
    else if (action == "toggle") command = 2;
    else {
        server.send(400, "text/plain", "Invalid action");
        return;
    }

    for (int i = 0; i < deviceCount; i++) {
        esp_now_message message;
        message.deviceId = devices[i].deviceId;
        message.command = command;
        message.relayState = 0;
        strcpy(message.message, "COMMAND_ALL");

        esp_now_send(devices[i].macAddress, (uint8_t *)&message, sizeof(message));
        delay(100);
    }

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Command sent to all devices");
}

void setupWebServer() {
    server.on("/", handleRoot);

    server.on("/api/devices", HTTP_GET, handleGetDevices);
    server.on("/api/device/control", HTTP_POST, handleAddDeviceControl);
    server.on("/api/device/controlAll", HTTP_POST, handleControlAll);

    server.on("/api/device/discover", HTTP_POST, handleDiscovery);
    server.on("/api/sync", HTTP_POST, handleSync);

    server.onNotFound(handleNotFound);
}

void handleRoot() {
    String html = getWebInterface();
    server.send(200, "text/html", html);
}

void handleGetDevices() {
    DynamicJsonDocument doc(1024);
    JsonArray devicesArray = doc.createNestedArray("devices");

    for (int i = 0; i < deviceCount; i++) {
        JsonObject device = devicesArray.createNestedObject();
        device["id"] = devices[i].deviceId;
        device["type"] = devices[i].deviceType;
        device["relayState"] = devices[i].relayState;
        device["isOnline"] = devices[i].isOnline;
        device["lastSeen"] = devices[i].lastSeen;

        String macStr = "";
        for (int j = 0; j < 6; j++) {
            if (j > 0) macStr += ":";
            macStr += String(devices[i].macAddress[j], HEX);
        }
        device["macAddress"] = macStr;
    }
    
    String response;
    serializeJson(doc, response);

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", response);
}

void handleDeviceControl() {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Body not found");
        return;
    }

    DynamicJsonDocument doc(254);
    deserializeJson(doc, server.arg("plain"));

    uint8_t deviceId = doc["deviceId"];
    String action = doc["action"];

    uint8_t command = 0;
    if (action == "on") command = 1;
    else if (action == "off") command = 0;
    else if (action == "toggle") command = 2;
    else if (action == "status") command = 3;
    
    int deviceIndex = findDeviceById(deviceId);
    if (deviceIndex == -1) {
        server.send(404, "text/plain", "Device not found");
        return;
    }
    esp_now_message message;
    message.deviceId = deviceId;
    message.command = command;
    message.relayState = 0;
    strcpy(message.message, "COMMAND");

    esp_now_send(devices[deviceIndex].macAddress, (uint8_t *)&message; sizeof(message));

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Command sent");
}

void handleDiscovery() {
    performDiscovery();
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Discovery initiated");
}

void handleSync() {
    for (int i = 0; i < deviceCount; i++) {
        esp_now_message message;
        message.deviceId = devices[i].deviceId;
        message.command = 3;
        message.relayState = 0;
        strcpy(message.message, "STATUS_REQUEST");

        esp_now_send(devices[i].macAddress, (uint8_t *)&message, sizeof(message));
        delay(100);
    }

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Sync initiated");
}

void handleNotFound() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");

    if (server.method() == HTTP_OPTIONS) {
        server.send(204, "text/plain", "");
    } else {
        server.send(404, "text/plain", "Not found");
    }
}

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
    Serial.print("Send status to: ");
    printMacAddress(mac_addr);
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

    if (message.command == 10 || message.command == 11 || message.command == 12) {
        updateDeviceInfo(message.deviceId, mac, message.relayState, message.message);
    }
}

void performDiscovery() {
    Serial.println("Performing discovery...");

    esp_now_message message;
    message.deviceId = 0;
    message.command = 4;
    message.relayState = 0;
    strcpy(message.message, "DISCOVERY");

    esp_now_send(broadcastAddress, (uint8_t *) &message, sizeof(message));
}

void updateDeviceInfo(uint8_t deviceId, uint8_t *mac, bool relayState, const char *deviceType) {
    int index = findDeviceById(deviceId);

    if (index == -1 && deviceCount < 10) {
        index = deviceCount++;
        devices[index].deviceId = deviceId;
    }

    if (index != -1) {
        memcpy(devices[index].macAddress, macAddress, 6);
        devices[index].relayState = relayState;
        devices[index].lastSeen = millis();
        devices[index].isOnline = true;
        strcpy(devices[index].deviceType, deviceType);

        Serial.print("Device updated: ID=");
        Serial.print(deviceId);
        Serial.print(", Relay=");
        Serial.println(relayState ? "ON" : "OFF");
    }
}

int findDeviceById(uint8_t deviceId) {
    for (int i = 0; i < deviceCount; i++) {
        if (devices[i].deviceId == deviceId) {
            return i;
        }
    }
    return -1;
}

void checkDeviceStatus() {
    unsigned long currentTime = millis();
    
    for (int i = 0; i < deviceCount; i++) {
        if (currentTime - devices[i].lastSeen > DEVICE_TIMEOUT) {
            devices[i].isOnline = false;
        }
    }
}

void printMacAddress(uint8_t *mac) {
    for (int i = 0; i < 6; i++) {
        Serial.print(mac[i], HEX);
        if (i < 5) Serial.print(":");
    }
    Serial.println();
}

String getWebInterface() {
    return R"(
        <!DOCTYPE html>
        <html>
        <head>
            <title>ESP-NOW Controller</title>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <style>
                * { margin: 0; padding: 0; box-sizing: border-box; }
                body { 
                    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
                    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                    min-height: 100vh;
                    padding: 20px;
                }
                .container { 
                    max-width: 1200px; 
                    margin: 0 auto;
                    background: rgba(255,255,255,0.95);
                    border-radius: 20px;
                    padding: 30px;
                    box-shadow: 0 20px 40px rgba(0,0,0,0.1);
                }
                h1 { 
                    text-align: center; 
                    color: #333;
                    margin-bottom: 30px;
                    font-size: 2.5em;
                    text-shadow: 2px 2px 4px rgba(0,0,0,0.1);
                }
                .controls { 
                    display: flex; 
                    justify-content: center; 
                    gap: 15px; 
                    margin-bottom: 30px;
                    flex-wrap: wrap;
                }
                button { 
                    padding: 12px 24px; 
                    border: none; 
                    border-radius: 25px; 
                    cursor: pointer;
                    font-size: 16px;
                    font-weight: bold;
                    transition: all 0.3s ease;
                    text-transform: uppercase;
                    letter-spacing: 1px;
                    box-shadow: 0 4px 15px rgba(0,0,0,0.2);
                }
                .btn-primary { 
                    background: linear-gradient(45deg, #667eea, #764ba2);
                    color: white;
                }
                .btn-secondary { 
                    background: linear-gradient(45deg, #f093fb, #f5576c);
                    color: white;
                }
                button:hover { 
                    transform: translateY(-2px);
                    box-shadow: 0 6px 20px rgba(0,0,0,0.3);
                }
                .device-grid { 
                    display: grid; 
                    grid-template-columns: repeat(auto-fill, minmax(300px, 1fr)); 
                    gap: 20px;
                }
                .device-card { 
                    background: white;
                    border-radius: 15px;
                    padding: 20px;
                    box-shadow: 0 10px 25px rgba(0,0,0,0.1);
                    transition: transform 0.3s ease;
                    border: 2px solid transparent;
                }
                .device-card:hover {
                    transform: translateY(-5px);
                    border-color: #667eea;
                }
                .device-card.online { border-left: 5px solid #4CAF50; }
                .device-card.offline { border-left: 5px solid #f44336; }
                .device-header { 
                    display: flex; 
                    justify-content: space-between; 
                    align-items: center;
                    margin-bottom: 15px;
                }
                .device-name { 
                    font-size: 1.3em; 
                    font-weight: bold;
                    color: #333;
                }
                .status-badge { 
                    padding: 5px 12px; 
                    border-radius: 20px; 
                    font-size: 0.8em;
                    font-weight: bold;
                    text-transform: uppercase;
                }
                .status-online { 
                    background: #4CAF50; 
                    color: white;
                }
                .status-offline { 
                    background: #f44336; 
                    color: white;
                }
                .device-info { 
                    color: #666;
                    font-size: 0.9em;
                    margin-bottom: 15px;
                }
                .device-controls { 
                    display: flex; 
                    gap: 10px;
                    flex-wrap: wrap;
                }
                .relay-state { 
                    font-size: 1.1em; 
                    font-weight: bold;
                    padding: 8px 16px;
                    border-radius: 20px;
                    margin-bottom: 10px;
                    text-align: center;
                }
                .relay-on { 
                    background: #E8F5E8; 
                    color: #2E7D32;
                    border: 2px solid #4CAF50;
                }
                .relay-off { 
                    background: #FFEBEE; 
                    color: #C62828;
                    border: 2px solid #f44336;
                }
                .loading { 
                    text-align: center; 
                    padding: 40px;
                    font-size: 1.2em;
                    color: #666;
                }
                .no-devices { 
                    text-align: center; 
                    padding: 40px;
                    color: #999;
                    font-size: 1.1em;
                }
                @media (max-width: 768px) {
                    .controls { flex-direction: column; }
                    .device-controls { flex-direction: column; }
                    h1 { font-size: 2em; }
                }
            </style>
        </head>
        <body>
            <div class="container">
                <h1>🎛️ ESP-NOW Controller</h1>
                
                <div class="controls">
                    <button class="btn-primary" onclick="refreshDevices()">🔄 Atualizar</button>
                    <button class="btn-secondary" onclick="performDiscovery()">🔍 Descobrir</button>
                    <button class="btn-primary" onclick="syncAll()">🔄 Sincronizar</button>
                    <button class="btn-primary" onclick="refreshDevices()">Atualizar</button>
                    <button class="btn-secondary" onclick="performDiscovery()">Descobrir</button>
                    <button class="btn-primary" onclick="syncAll()">Sincronizar</button>
                    <button class="btn-primary" onclick="controlAll('on')">Ligar Todos</button>
                    <button class="btn-secondary" onclick="controlAll('off')">Desligar Todos</button>
                    <button class="btn-primary" onclick="controlAll('toggle')">Alternar Todos</button>
                </div>
                
                <div id="deviceList">
                    <div class="loading">Carregando dispositivos...</div>
                </div>
            </div>

            <script>
                let devices = [];
                
                async function refreshDevices() {
                    try {
                        const response = await fetch('/api/devices');
                        const data = await response.json();
                        devices = data.devices;
                        renderDevices();
                    } catch (error) {
                        console.error('Erro ao carregar dispositivos:', error);
                    }
                }
                
                async function performDiscovery() {
                    try {
                        await fetch('/api/discovery', { method: 'POST' });
                        setTimeout(refreshDevices, 2000);
                    } catch (error) {
                        console.error('Erro na descoberta:', error);
                    }
                }
                
                async function syncAll() {
                    try {
                        await fetch('/api/sync', { method: 'POST' });
                        setTimeout(refreshDevices, 1000);
                    } catch (error) {
                        console.error('Erro na sincronização:', error);
                    }
                }
                
                async function controlDevice(deviceId, action) {
                    try {
                        await fetch('/api/device/control', {
                            method: 'POST',
                            headers: { 'Content-Type': 'application/json' },
                            body: JSON.stringify({ deviceId, action })
                        });
                        setTimeout(refreshDevices, 500);
                    } catch (error) {
                        console.error('Erro ao controlar dispositivo:', error);
                    }
                }
                
                async function controlAll(action) {
                    try {
                        await fetch("/api/device/controlAll", {
                            method: "POST",
                            headers: { "Content-Type": "application/json" },
                            body: JSON.stringify({ action })
                        });
                        setTimeout(refreshDevices, 500);
                    } catch (error) {
                        console.error("Erro ao controlar todos os dispositivos:", error);
                    }
                }
                function renderDevices() {
                    const deviceList = document.getElementById('deviceList');
                    
                    if (devices.length === 0) {
                        deviceList.innerHTML = '<div class="no-devices">Nenhum dispositivo encontrado</div>';
                        return;
                    }
                    
                    const html = devices.map(device => `
                        <div class="device-card ${device.isOnline ? 'online' : 'offline'}">
                            <div class="device-header">
                                <div class="device-name">Dispositivo ${device.id}</div>
                                <div class="status-badge ${device.isOnline ? 'status-online' : 'status-offline'}">
                                    ${device.isOnline ? 'Online' : 'Offline'}
                                </div>
                            </div>
                            
                            <div class="relay-state ${device.relayState ? 'relay-on' : 'relay-off'}">
                                Relay: ${device.relayState ? 'LIGADO' : 'DESLIGADO'}
                            </div>
                            
                            <div class="device-info">
                                <div>Tipo: ${device.type}</div>
                                <div>MAC: ${device.macAddress}</div>
                                <div>Última vez visto: ${new Date(device.lastSeen).toLocaleString()}</div>
                            </div>
                            
                            <div class="device-controls">
                                <button class="btn-primary" onclick="controlDevice(${device.id}, 'on')" ${!device.isOnline ? 'disabled' : ''}>
                                    🔛 Ligar
                                </button>
                                <button class="btn-secondary" onclick="controlDevice(${device.id}, 'off')" ${!device.isOnline ? 'disabled' : ''}>
                                    ⭕ Desligar
                                </button>
                                <button class="btn-primary" onclick="controlDevice(${device.id}, 'toggle')" ${!device.isOnline ? 'disabled' : ''}>
                                    🔄 Alternar
                                </button>
                                <button class="btn-secondary" onclick="controlDevice(${device.id}, 'status')" ${!device.isOnline ? 'disabled' : ''}>
                                    📊 Status
                                </button>
                            </div>
                        </div>
                    `).join('');
                    
                    deviceList.innerHTML = `<div class="device-grid">${html}</div>`;
                }
                
                // Carregar dispositivos na inicialização
                refreshDevices();
                
                // Atualizar automaticamente a cada 10 segundos
                setInterval(refreshDevices, 10000);
            </script>
        </body>
        </html>
    )";
}