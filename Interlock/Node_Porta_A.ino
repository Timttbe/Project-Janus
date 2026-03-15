#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// ====================== CONFIGURAÇÕES ======================
#define DEVICE_NAME "PORTA_A"  // 👉 altere para "PORTA_A", "PORTA_B" ou "PORTEIRO"
const char* ssid = "Evosystems&Wires Visitante";
const char* password = "Wifi2025";

#define PORTA_TIMEOUT 300000
#define UDP_PORT 4210
#define RELAY_TIME 5000  // 5 segundos

// ===================== PINOS ================================
#define BTN1_PIN 5    // D1 - facial / botão 1 do porteiro
#define BTN2_PIN 4    // D2 - botão 2 (apenas porteiro)
#define BYPASS_PIN 0  // D3 - interruptor bypass (apenas porteiro)
#define SENSOR_PIN 14 // D5 - sensor da porta
#define RELAY_PIN 12  // D6 - LED ímã
#define PUPE_PIN 13   // D7 - LED puxe/empurre

// ===================== VARIÁVEIS ============================
WiFiUDP udp;
IPAddress localIP;
String knownNames[5];
String knownIPs[5];
unsigned long lastPing[5];
bool bypassMode = false;
bool portaAberta = false;
unsigned long relayStart = 0;
bool relayAtivo = false;
unsigned long lastDiscovery = 0;
unsigned long lastPingSent = 0;
unsigned long lastStatusSent = 0;
unsigned long portaAbertaTempo = 0;

// Estado das outras portas (recebido via rede)
bool portaAAberta = false;
bool portaBAberta = false;
bool portaALock = false;
bool portaBLock = false;
unsigned long lastStatusPortaA = 0;
unsigned long lastStatusPortaB = 0;

// ===================== FUNÇÕES =============================
void sendBroadcast(const String& msg) {
  udp.beginPacket("255.255.255.255", UDP_PORT);
  udp.print(msg);
  udp.endPacket();
  Serial.println("[UDP] Broadcast enviado: " + msg);
}

void sendStatus() {
  String estado = portaAberta ? "OPEN" : "CLOSED";
  sendBroadcast("STATUS|" + DEVICE_NAME + "|" + estado);
}

bool podeAbrir(const String& portaAlvo) {
  // Se bypass está ativo, pode abrir sempre
  if (bypassMode) {
    Serial.println("✅ Bypass ativo, abrindo sem verificar intertravamento");
    return true;
  }

  // Verifica intertravamento: só pode abrir se a OUTRA porta estiver fechada
  if (portaAlvo == "PORTA_A" ) {
    // Verifica se recebeu status da PORTA_B recentemente
    if (millis() - lastStatusPortaB > 10000 && lastStatusPortaB > 0) {
      Serial.println("⚠️ Sem comunicação com PORTA_B! Bloqueando por segurança.");
      return false;
    }
    
    if (portaBAberta) {
      Serial.println("🚫 PORTA_B está aberta! Não pode abrir PORTA_A.");
      Serial.println("   Estado PORTA_B: " + String(portaBAberta ? "ABERTA" : "FECHADA"));
      return false;
    }
    Serial.println("✅ PORTA_B está fechada, pode abrir PORTA_A");
  } 
  else if (portaAlvo == "PORTA_B") {
    // Verifica se recebeu status da PORTA_A recentemente
    if (millis() - lastStatusPortaA > 10000 && lastStatusPortaA > 0) {
      Serial.println("⚠️ Sem comunicação com PORTA_A! Bloqueando por segurança.");
      return false;
    }
    
    if (portaAAberta) {
      Serial.println("🚫 PORTA_A está aberta! Não pode abrir PORTA_B.");
      Serial.println("   Estado PORTA_A: " + String(portaAAberta ? "ABERTA" : "FECHADA"));
      return false;
    }
    Serial.println("✅ PORTA_A está fechada, pode abrir PORTA_B");
  }

  if (portaAlvo == "PORTA_A" && portaBLock) {
    Serial.println("🚫 PORTA_B está travando o sistema");
    return false;
  }

  if (portaAlvo == "PORTA_B" && portaALock) {
    Serial.println("🚫 PORTA_A está travando o sistema");
    return false;
  }

  return true;
}

void abrirPorta() {
  if (relayAtivo) return;
  
  // Verifica se ESTA porta pode abrir (olhando o estado da OUTRA)
  if (!podeAbrir(DEVICE_NAME)) {
    return;
  }

  Serial.println("🔓 Porta abrindo...");
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(PUPE_PIN, LOW);
  relayStart = millis();
  relayAtivo = true;
  sendStatus();
}

void addDevice(const String& dev, const String& ip) {
  if (dev == DEVICE_NAME) return;

  for (int i = 0; i < 5; i++) {
    if (knownNames[i] == dev) {
      knownIPs[i] = ip;
      lastPing[i] = millis();
      return;
    }
  }

  for (int i = 0; i < 5; i++) {
    if (knownNames[i] == "") {
      knownNames[i] = dev;
      knownIPs[i] = ip;
      lastPing[i] = millis();
      Serial.println("[DISCOVERY] Novo device: " + dev + " -> " + ip);
      break;
    }
  }

  // Envia confirmação mútua
  if (countKnownDevices() < 5) {
    sendBroadcast("CONFIRM|" + DEVICE_NAME + "|" + localIP.toString());
  }
}

int countKnownDevices() {
  int count = 0;
  for (int i = 0; i < 5; i++) {
    if (knownNames[i] != "") count++;
  }
  return count;
}

void processMessage(const String& msg) {
  if (msg.startsWith("DISCOVERY|")) {
    int i1 = msg.indexOf('|') + 1;
    int i2 = msg.indexOf('|', i1);
    String dev = msg.substring(i1, i2);
    String ip = msg.substring(i2 + 1);
    addDevice(dev, ip);
  }

  else if (msg.startsWith("CONFIRM|")) {
    int i1 = msg.indexOf('|') + 1;
    int i2 = msg.indexOf('|', i1);
    String dev = msg.substring(i1, i2);
    String ip = msg.substring(i2 + 1);
    addDevice(dev, ip);
  }

  else if (msg.startsWith("PING|")) {
    int i1 = msg.indexOf('|') + 1;
    int i2 = msg.indexOf('|', i1);
    String dev = msg.substring(i1, i2);
    String ip = msg.substring(i2 + 1);
    addDevice(dev, ip);
    sendBroadcast("PONG|" + DEVICE_NAME + "|" + localIP.toString());
  }

  else if (msg.startsWith("PONG|")) {
    int i1 = msg.indexOf('|') + 1;
    int i2 = msg.indexOf('|', i1);
    String dev = msg.substring(i1, i2);
    for (int i = 0; i < 5; i++) {
      if (knownNames[i] == dev) {
        lastPing[i] = millis();
      }
    }
  }

  else if (msg.startsWith("OPEN|")) {
    String target = msg.substring(5);
    Serial.println("[OPEN recebido] Target: " + target + " | Meu nome: " + DEVICE_NAME);
    if (target == DEVICE_NAME) {
      abrirPorta();
    }
  }

  else if (msg.startsWith("BYPASS|")) {
    bypassMode = msg.endsWith("ON");
    Serial.println(bypassMode ? "⚠️ Bypass ativado!" : "🔒 Bypass desativado.");
  }

  else if (msg.startsWith("STATUS|")) {
    int i1 = msg.indexOf('|') + 1;
    int i2 = msg.indexOf('|', i1);
    String dev = msg.substring(i1, i2);
    String estado = msg.substring(i2 + 1);
    
    // Atualiza o estado das outras portas
    if (dev == "PORTA_A") {
      bool estadoAnterior = portaAAberta;
      portaAAberta = (estado == "OPEN");
      lastStatusPortaA = millis();
      Serial.println("[STATUS] PORTA_A: " + estado + 
                     (estadoAnterior != portaAAberta ? " (MUDOU!)" : ""));
    } else if (dev == "PORTA_B") {
      bool estadoAnterior = portaBAberta;
      portaBAberta = (estado == "OPEN");
      lastStatusPortaB = millis();
      Serial.println("[STATUS] PORTA_B: " + estado + 
                     (estadoAnterior != portaBAberta ? " (MUDOU!)" : ""));
    }
  }

  else if (msg.startsWith("LOCK|")) {
    String dev = msg.substring(5);

    if (dev == "PORTA_A") portaALock = true;
    if (dev == "PORTA_B") portaBLock = true;

    Serial.println("🔒 LOCK recebido de " + dev);
  }

  else if (msg.startsWith("UNLOCK|")) {
    String dev = msg.substring(7);

    if (dev == "PORTA_A") portaALock = false;
    if (dev == "PORTA_B") portaBLock = false;

    Serial.println("🔓 UNLOCK recebido de " + dev);
  }
}

// ===================== SETUP ===============================
void setup() {
  Serial.begin(115200);
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(BYPASS_PIN, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(PUPE_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(PUPE_PIN, HIGH);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ WiFi conectado!");
  localIP = WiFi.localIP();
  Serial.println("IP: " + localIP.toString());
  Serial.println("Device: " + DEVICE_NAME);

  udp.begin(UDP_PORT);
  sendBroadcast("DISCOVERY|" + DEVICE_NAME + "|" + localIP.toString());
}

// ===================== LOOP ================================
void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi desconectado, tentando reconectar...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED) {
      static unsigned long lastReconnect = 0;
      if (millis() - lastReconnect > 5000) {
        Serial.println("⚠️ Tentando reconectar WiFi...");
        WiFi.disconnect();
        WiFi.begin(ssid, password);
        lastReconnect = millis();
      }
    }
    Serial.println("\n✅ Conexão restabelecida!");
  }
  // Receber pacotes UDP
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char buffer[255];
    int len = udp.read(buffer, 255);
    if (len > 0) buffer[len] = '\0';
    processMessage(String(buffer));
  }

  // SEMPRE envia broadcast de descoberta a cada 5s (não para nunca!)
  if (millis() - lastDiscovery > 5000) {
    sendBroadcast("DISCOVERY|" + DEVICE_NAME + "|" + localIP.toString());
    lastDiscovery = millis();
  }

  // Envia PING a cada 10s
  if (millis() - lastPingSent > 10000) {
    sendBroadcast("PING|" + DEVICE_NAME + "|" + localIP.toString());
    lastPingSent = millis();
  }

  // Envia STATUS a cada 15s (para manter todos sincronizados)
  if (millis() - lastStatusSent > 15000) {
    sendStatus();
    lastStatusSent = millis();
    

    Serial.println("Minha porta: " + String(portaAberta ? "ABERTA" : "FECHADA"));
    Serial.println("PORTA_A: " + String(portaAAberta ? "ABERTA" : "FECHADA") + 
                   " (última atualização: " + String(millis() - lastStatusPortaA) + "ms)");
    Serial.println("PORTA_B: " + String(portaBAberta ? "ABERTA" : "FECHADA") + 
                   " (última atualização: " + String(millis() - lastStatusPortaB) + "ms)");
    Serial.println("Bypass: " + String(bypassMode ? "ON" : "OFF"));
    Serial.println("Relay ativo: " + String(relayAtivo ? "SIM" : "NÃO"));
    Serial.println("-------------------------");
  }

  // Remove dispositivos inativos (sem PONG há 30s)
  for (int i = 0; i < 5; i++) {
    if (knownNames[i] != "" && millis() - lastPing[i] > 30000) {
      Serial.println("⚠️ Dispositivo inativo removido: " + knownNames[i]);
      knownNames[i] = "";
      knownIPs[i] = "";
    }
  }

  // Lógica de botões
  if (String(DEVICE_NAME) == "PORTEIRO") {
    // Botão 1 - Abre PORTA_A
    static bool lastBtn1 = HIGH;
    bool btn1State = digitalRead(BTN1_PIN);
    if (btn1State == LOW && lastBtn1 == HIGH) {
      Serial.println("🔘 Botão 1 pressionado - tentando abrir PORTA_A");
      if (podeAbrir("PORTA_A")) {
        sendBroadcast("OPEN|PORTA_A");
        Serial.println("✅ Comando enviado para PORTA_A");
      } else {
        Serial.println("❌ Bloqueado pelo intertravamento");
      }
      delay(300);
    }
    lastBtn1 = btn1State;

    // Botão 2 - Abre PORTA_B
    static bool lastBtn2 = HIGH;
    bool btn2State = digitalRead(BTN2_PIN);
    if (btn2State == LOW && lastBtn2 == HIGH) {
      Serial.println("🔘 Botão 2 pressionado - tentando abrir PORTA_B");
      if (podeAbrir("PORTA_B")) {
        sendBroadcast("OPEN|PORTA_B");
        Serial.println("✅ Comando enviado para PORTA_B");
      } else {
        Serial.println("❌ Bloqueado pelo intertravamento");
      }
      delay(300);
    }
    lastBtn2 = btn2State;

    // Interruptor Bypass
    bool bypassState = (digitalRead(BYPASS_PIN) == LOW);
    static bool lastBypass = !bypassState;
    if (bypassState != lastBypass) {
      sendBroadcast("BYPASS|" + String(bypassState ? "ON" : "OFF"));
      lastBypass = bypassState;
      Serial.println("🔀 Bypass alterado: " + String(bypassState ? "ON" : "OFF"));
      delay(300);
    }
  } else {
    // Portas A e B - botão local
    static bool lastBtn1 = HIGH;
    bool btn1State = digitalRead(BTN1_PIN);
    if (btn1State == LOW && lastBtn1 == HIGH) {
      Serial.println("🔘 Botão local pressionado");
      abrirPorta();
      delay(300);
    }
    lastBtn1 = btn1State;
  }

  // Atualiza estado do sensor e envia status se mudar
  bool novoEstado = (digitalRead(SENSOR_PIN) == LOW);
  if (novoEstado != portaAberta) {
    portaAberta = novoEstado;
    if (portaAberta) {
      portaAbertaTempo = millis();
      sendBroadcast("LOCK|" + DEVICE_NAME);
    }
    else {
      sendBroadcast("UNLOCK|" + DEVICE_NAME);
    }
    sendStatus();
    Serial.println(portaAberta ? "🚪 Sensor: Porta ABERTA" : "🚪 Sensor: Porta FECHADA");
  }

  // Desliga o relé após 5s
  if (relayAtivo && millis() - relayStart >= RELAY_TIME) {
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(PUPE_PIN, HIGH);
    relayAtivo = false;
    Serial.println("🔒 Relé desligado.");
    
    // Atualiza status baseado no sensor
    bool sensorAberto = (digitalRead(SENSOR_PIN) == LOW);
    if (portaAberta != sensorAberto) {
      portaAberta = sensorAberto;
      sendStatus();
    }
  }

  if (portaAberta && millis() - portaAbertaTempo > PORTA_TIMEOUT) {
    Serial.println("⚠️ Porta aberta por muito tempo! Enviando alerta...");
    // CRIAR UMA LÓGICA DE ALERTA AQUI
  }
}