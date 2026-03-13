#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <espnow.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

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
const unsigned long DEVICE_TIMEOUT = 120000;

void setup() {
    Serial.begin(115200);
    EEPROM.begin(512);
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP_Control", "12345678");  
    
    Serial.println("\n[INFO] Access Point Mode Activated");
    Serial.print("SSID: ESP_Control | IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != 0) {
        Serial.println("[ERROR] Failed to intialize ESP-NOW");
        ESP.restart();  
    }
    
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
    
    esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
    
    setupWebServer();
    server.begin();
    Serial.println("[INFO] Web Server Intialized");

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

void setupWebServer() {
    server.on("/", handleRoot);

    server.on("/api/devices", HTTP_GET, handleGetDevices);
    server.on("/api/device/control", HTTP_POST, handleDeviceControl);
    server.on("/api/device/discover", HTTP_POST, handleDiscovery);
    server.on("/api/sync", HTTP_POST, handleSync);
    server.on("/api/device/controlAll", HTTP_POST, handleControlAll);
    server.on("/api/device/alternateControl", HTTP_POST, handleAlternateControl);

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

    esp_now_send(devices[deviceIndex].macAddress, (uint8_t *)&message, sizeof(message));

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Command sent");
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

int lastAlternateIndex = -1;

void handleAlternateControl() {

    int onlineIndexes[10];
    int onlineCount = 0;
    for (int i = 0; i < deviceCount; i++) {
        if (devices[i].isOnline) {
            onlineIndexes[onlineCount++] = i;
        }
    }
    if (onlineCount == 0) {
        server.send(400, "text/plain", "No online devices found");
        return;
    }

    lastAlternateIndex = (lastAlternateIndex + 1) % onlineCount;

    for (int i = 0; i < onlineCount; i++) {
        esp_now_message message;
        message.deviceId = devices[onlineIndexes[i]].deviceId;
        if (i == lastAlternateIndex) {
            message.command = 1; // ON
            strcpy(message.message, "ALTERNATE_ON");
        } else {
            message.command = 0; // OFF
            strcpy(message.message, "ALTERNATE_OFF");
        }
        message.relayState = 0;
        esp_now_send(devices[onlineIndexes[i]].macAddress, (uint8_t *)&message, sizeof(message));
        delay(100);
    }

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Alternated relay ON, others OFF");
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
        memcpy(devices[index].macAddress, mac, 6);
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
    String html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<title>ESP-NOW Smart Controller</title>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<style>"
        "* { margin: 0; padding: 0; box-sizing: border-box; }"
        "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: linear-gradient(135deg, #1a1a1a 0%, #2d2d2d 50%, #1a1a1a 100%); min-height: 100vh; padding: 20px; color: #fff; }"
        ".container { max-width: 1200px; margin: 0 auto; background: rgba(0,0,0,0.8); border: 2px solid #ffd700; border-radius: 20px; padding: 30px; box-shadow: 0 20px 40px rgba(255,215,0,0.3), inset 0 1px 0 rgba(255,215,0,0.2); backdrop-filter: blur(10px); }"
        "h1 { text-align: center; color: #ffd700; margin-bottom: 30px; font-size: 2.8em; text-shadow: 0 0 20px rgba(255,215,0,0.5); font-weight: 300; letter-spacing: 2px; }"
        ".controls { display: flex; justify-content: center; gap: 20px; margin-bottom: 40px; flex-wrap: wrap; }"
        "button { padding: 12px 24px; border: 2px solid #ffd700; border-radius: 25px; cursor: pointer; font-size: 14px; font-weight: bold; transition: all 0.3s ease; text-transform: uppercase; letter-spacing: 1px; background: transparent; color: #ffd700; position: relative; overflow: hidden; }"
        "button:before { content: ''; position: absolute; top: 0; left: -100%; width: 100%; height: 100%; background: linear-gradient(90deg, transparent, rgba(255,215,0,0.2), transparent); transition: left 0.5s; }"
        "button:hover:before { left: 100%; }"
        "button:hover { background: rgba(255,215,0,0.1); box-shadow: 0 0 20px rgba(255,215,0,0.5); transform: translateY(-2px); }"
        ".device-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(350px, 1fr)); gap: 25px; }"
        ".device-card { background: linear-gradient(145deg, #2a2a2a, #1f1f1f); border: 1px solid #444; border-radius: 20px; padding: 25px; transition: all 0.3s ease; position: relative; overflow: hidden; }"
        ".device-card:before { content: ''; position: absolute; top: 0; left: 0; right: 0; height: 3px; background: linear-gradient(90deg, #ffd700, #ffed4a, #ffd700); opacity: 0; transition: opacity 0.3s ease; }"
        ".device-card:hover:before { opacity: 1; }"
        ".device-card:hover { transform: translateY(-5px); border-color: #ffd700; box-shadow: 0 15px 35px rgba(255,215,0,0.2); }"
        ".device-card.online { border-left: 4px solid #4CAF50; box-shadow: 0 0 10px rgba(76,175,80,0.3); }"
        ".device-card.offline { border-left: 4px solid #f44336; box-shadow: 0 0 10px rgba(244,67,54,0.3); opacity: 0.7; }"
        ".device-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }"
        ".device-name { font-size: 1.4em; font-weight: bold; color: #ffd700; text-shadow: 0 0 10px rgba(255,215,0,0.3); }"
        ".status-badge { padding: 6px 12px; border-radius: 20px; font-size: 0.8em; font-weight: bold; text-transform: uppercase; border: 1px solid; }"
        ".status-online { background: rgba(76,175,80,0.2); color: #4CAF50; border-color: #4CAF50; box-shadow: 0 0 10px rgba(76,175,80,0.3); }"
        ".status-offline { background: rgba(244,67,54,0.2); color: #f44336; border-color: #f44336; }"
        ".device-info { color: #ccc; font-size: 0.9em; margin-bottom: 20px; line-height: 1.6; }"
        ".device-info div { margin-bottom: 5px; }"
        ".toggle-container { display: flex; align-items: center; justify-content: center; gap: 15px; margin-top: 15px; }"
        ".master-toggle { margin-bottom: 20px; }"
        ".toggle-switch { position: relative; width: 80px; height: 40px; background: #333; border-radius: 25px; border: 2px solid #555; cursor: pointer; transition: all 0.3s ease; box-shadow: inset 0 2px 5px rgba(0,0,0,0.5); }"
        ".toggle-switch:hover { border-color: #ffd700; box-shadow: 0 0 15px rgba(255,215,0,0.3), inset 0 2px 5px rgba(0,0,0,0.5); }"
        ".toggle-switch.active { background: linear-gradient(45deg, #4CAF50, #66BB6A); border-color: #4CAF50; box-shadow: 0 0 20px rgba(76,175,80,0.4), inset 0 2px 5px rgba(0,0,0,0.2); }"
        ".toggle-switch.inactive { background: linear-gradient(45deg, #f44336, #ef5350); border-color: #f44336; box-shadow: 0 0 20px rgba(244,67,54,0.4), inset 0 2px 5px rgba(0,0,0,0.2); }"
        ".toggle-switch.disabled { opacity: 0.5; cursor: not-allowed; }"
        ".toggle-slider { position: absolute; top: 3px; left: 3px; width: 30px; height: 30px; background: #fff; border-radius: 50%; transition: all 0.3s ease; box-shadow: 0 2px 5px rgba(0,0,0,0.3); }"
        ".toggle-switch.active .toggle-slider { transform: translateX(40px); background: #e8f5e8; }"
        ".toggle-switch.inactive .toggle-slider { transform: translateX(0px); background: #ffebee; }"
        ".toggle-label { font-size: 0.9em; font-weight: bold; color: #ccc; min-width: 80px; text-align: center; }"
        ".toggle-switch.active + .toggle-label { color: #4CAF50; text-shadow: 0 0 5px rgba(76,175,80,0.5); }"
        ".toggle-switch.inactive + .toggle-label { color: #f44336; text-shadow: 0 0 5px rgba(244,67,54,0.5); }"
        ".master-controls { text-align: center; padding: 20px; background: rgba(255,215,0,0.05); border: 1px solid rgba(255,215,0,0.2); border-radius: 15px; margin-bottom: 30px; }"
        ".master-controls h3 { color: #ffd700; margin-bottom: 15px; font-size: 1.2em; }"
        ".master-control-buttons { display: flex; justify-content: center; gap: 15px; margin-top: 15px; flex-wrap: wrap; }"
        ".alternate-btn { background: linear-gradient(45deg, #ff6b35, #f7931e); border: 2px solid #ff6b35; color: white; padding: 10px 20px; border-radius: 20px; cursor: pointer; font-weight: bold; transition: all 0.3s ease; text-transform: uppercase; font-size: 12px; }"
        ".alternate-btn:hover { background: linear-gradient(45deg, #f7931e, #ff6b35); box-shadow: 0 0 20px rgba(255,107,53,0.5); transform: translateY(-2px); }"
        ".loading { text-align: center; padding: 60px; font-size: 1.4em; color: #ffd700; animation: pulse 2s infinite; }"
        "@keyframes pulse { 0% { opacity: 0.6; } 50% { opacity: 1; } 100% { opacity: 0.6; } }"
        ".no-devices { text-align: center; padding: 60px; color: #888; font-size: 1.2em; }"
        "@media (max-width: 768px) { .controls { flex-direction: column; align-items: center; } .device-grid { grid-template-columns: 1fr; } h1 { font-size: 2.2em; } .container { padding: 20px; margin: 10px; } .toggle-container { flex-direction: column; gap: 10px; } .master-control-buttons { flex-direction: column; } }"
        "</style>"
        "</head>"
        "<body>"
        "<div class=\"container\">"
        "<h1>&#9889; ESP-NOW Smart Controller &#9889;</h1>"
        "<div class=\"controls\">"
        "<button onclick=\"refreshDevices()\">&#128260; Refresh</button>"
        "<button onclick=\"performDiscovery()\">&#128269; Discover</button>"
        "<button onclick=\"syncAll()\">&#128260; Sync</button>"
        "</div>"
        "<div class=\"master-controls\">"
        "<h3>Master Controls</h3>"
        "<div class=\"toggle-container master-toggle\">"
        "<div class=\"toggle-switch\" id=\"masterToggle\" onclick=\"toggleAllDevices()\">"
        "<div class=\"toggle-slider\"></div>"
        "</div>"
        "<div class=\"toggle-label\" id=\"masterLabel\">All Devices</div>"
        "</div>"
        "<div class=\"master-control-buttons\" style=\"justify-content: center;\">"
        "<div class=\"toggle-container\">"
        "<div class=\"toggle-switch inactive\" id=\"alternateToggle\" onclick=\"toggleAlternateControl()\">"
        "<div class=\"toggle-slider\"></div>"
        "</div>"
        "<div class=\"toggle-label\">Alternate Control</div>"
        "</div>"
        "</div>"
        "<div id=\"deviceList\">"
        "<div class=\"loading\">&#9889; Loading devices...</div>"
        "</div>"
        "</div>"
        "<script>"
        "let devices = [];"
        "let allDevicesState = false;"
        "let alternateActive = false;"
        "function toggleAlternateControl() {"
        "  const toggle = document.getElementById('alternateToggle');"
        "  alternateActive = !alternateActive;"
        "  toggle.className = 'toggle-switch ' + (alternateActive ? 'active' : 'inactive');"
        "  alternateDevices();"
        "  setTimeout(() => {"
        "    alternateActive = false;"
        "    toggle.className = 'toggle-switch inactive';"
        "  }, 600);"
        "}"
        "async function refreshDevices() {"
        "  try {"
        "    const response = await fetch('/api/devices');"
        "    const data = await response.json();"
        "    devices = data.devices;"
        "    updateMasterToggle();"
        "    renderDevices();"
        "  } catch (error) {"
        "    console.error('Error loading devices: ', error);"
        "  }"
        "}"
        "async function performDiscovery() {"
        "  try {"
        "    await fetch('/api/device/discover', { method: 'POST' });"
        "    setTimeout(refreshDevices, 2000);"
        "  } catch (error) {"
        "    console.error('Discovery error: ', error);"
        "  }"
        "}"
        "async function syncAll() {"
        "  try {"
        "    await fetch('/api/sync', { method: 'POST' });"
        "    setTimeout(refreshDevices, 1000);"
        "  } catch (error) {"
        "    console.error('Sync error:', error);"
        "  }"
        "}"
        "async function controlDevice(deviceId, currentState) {"
        "  try {"
        "    await fetch('/api/device/control', {"
        "      method: 'POST',"
        "      headers: { 'Content-Type': 'application/json' },"
        "      body: JSON.stringify({ deviceId, action: 'toggle' })"
        "    });"
        "    setTimeout(refreshDevices, 500);"
        "  } catch (error) {"
        "    console.error('Device control error:', error);"
        "  }"
        "}"
        "async function toggleAllDevices() {"
        "  try {"
        "    const action = allDevicesState ? 'off' : 'on';"
        "    await fetch('/api/device/controlAll', {"
        "      method: 'POST',"
        "      headers: { 'Content-Type': 'application/json' },"
        "      body: JSON.stringify({ action })"
        "    });"
        "    allDevicesState = !allDevicesState;"
        "    updateMasterToggleUI();"
        "    setTimeout(refreshDevices, 500);"
        "  } catch (error) {"
        "    console.error('Control all devices error:', error);"
        "  }"
        "}"
        "async function alternateDevices() {"
        "  try {"
        "    await fetch('/api/device/alternateControl', {"
        "      method: 'POST',"
        "      headers: { 'Content-Type': 'application/json' }"
        "    });"
        "    setTimeout(refreshDevices, 500);"
        "  } catch (error) {"
        "    console.error('Alternate control error:', error);"
        "  }"
        "}"
        "function updateMasterToggle() {"
        "  const onlineDevices = devices.filter(d => d.isOnline);"
        "  const activeDevices = onlineDevices.filter(d => d.relayState);"
        "  allDevicesState = activeDevices.length > onlineDevices.length / 2;"
        "  updateMasterToggleUI();"
        "}"
        "function updateMasterToggleUI() {"
        "  const masterToggle = document.getElementById('masterToggle');"
        "  const masterLabel = document.getElementById('masterLabel');"
        "  masterToggle.className = 'toggle-switch ' + (allDevicesState ? 'active' : 'inactive');"
        "  masterLabel.textContent = allDevicesState ? 'All ON' : 'All OFF';"
        "}"
        "function renderDevices() {"
        "  const deviceList = document.getElementById('deviceList');"
        "  if (devices.length === 0) {"
        "    deviceList.innerHTML = '<div class=\"no-devices\">No devices found. Try refreshing or discovering devices.</div>';"
        "    return;"
        "  }"
        "  const devicesHTML = devices.map(device => `"
        "<div class=\"device-card ${device.isOnline ? 'online' : 'offline'}\">"
        "<div class=\"device-header\">"
        "<div class=\"device-name\">${device.name}</div>"
        "<div class=\"status-badge ${device.isOnline ? 'status-online' : 'status-offline'}\">"
        "${device.isOnline ? 'Online' : 'Offline'}"
        "</div>"
        "</div>"
        "<div class=\"device-info\">"
        "<div><strong>Device ID:</strong> ${device.id}</div>"
        "<div><strong>MAC Address:</strong> ${device.macAddress}</div>"
        "<div><strong>Last Seen:</strong> ${device.lastSeen}</div>"
        "<div><strong>Signal:</strong> ${device.signalStrength} dBm</div>"
        "<div><strong>Relay State:</strong> ${device.relayState ? 'ON' : 'OFF'}</div>"
        "</div>"
        "<div class=\"toggle-container\">"
        "<div class=\"toggle-switch ${device.isOnline ? (device.relayState ? 'active' : 'inactive') : 'disabled'}\" "
        "onclick=\"${device.isOnline ? `controlDevice('${device.id}', ${device.relayState})` : ''}\">"
        "<div class=\"toggle-slider\"></div>"
        "</div>"
        "<div class=\"toggle-label\">${device.relayState ? 'ON' : 'OFF'}</div>"
        "</div>"
        "</div>"
        "`).join('');"
        "  deviceList.innerHTML = `<div class=\"device-grid\">${devicesHTML}</div>`;"
        "}"
        "document.addEventListener('DOMContentLoaded', function() {"
        "  refreshDevices();"
        "  setInterval(refreshDevices, 30000);"
        "});"
        "</script>"
        "</body>"
        "</html>";
    return html;
}