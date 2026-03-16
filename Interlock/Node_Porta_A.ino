#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// ====================== CONFIGURAÇÕES ======================
#define DEVICE_NAME "PORTA_A" // 👉 altere para "PORTA_A", "PORTA_B" ou "PORTEIRO"
#define DEV_PORTA_A "PORTA_A"
#define DEV_PORTA_B "PORTA_B"
#define DEV_PORTEIRO "PORTEIRO"
#define MASTER_DEVICE "PORTEIRO"

// ⚠️ SEGURANÇA: mova estas credenciais para um arquivo secrets.h em produção
const char *ssid     = "Evosystems&Wires Visitante";
const char *password = "Wifi2025";

#define PORTA_TIMEOUT     300000UL // 5 min
#define UDP_PORT          4210
#define RELAY_TIME        5000UL   // 5 segundos
#define SENSOR_OPEN       LOW
#define MASTER_LEASE_TIME 15000UL
#define MASTER_TIMEOUT    20000UL  // tempo sem HELLO antes de eleger novo master

// ===================== PINOS ================================
#define BTN1_PIN   5  // D1 - facial / botão 1 do porteiro
#define BTN2_PIN   4  // D2 - botão 2 (apenas porteiro)
#define BYPASS_PIN 0  // D3 - interruptor bypass (apenas porteiro)
#define SENSOR_PIN 14 // D5 - sensor da porta
#define RELAY_PIN  12 // D6 - eletroímã
#define PUPE_PIN   13 // D7 - LED puxe/empurre

// ===================== VARIÁVEIS ============================
WiFiUDP    udp;
IPAddress  localIP;

char knownNames[5][16];
char knownIPs[5][16];
unsigned long lastPing[5];

int  devicePriority = 1;
int  masterPriority = 0;
bool isMaster       = false;
char networkMaster[16] = "";
char tokenOwner[16]    = "";

bool bypassMode   = false;
bool portaAberta  = false;  // estado atual confirmado pelo sensor
bool sensorLeitura = false; // leitura debounced do sensor (substitui novoEstado)
bool relayAtivo   = false;
bool masterBusy   = false;
bool alertSent    = false;

unsigned long masterBusyTime  = 0;
unsigned long relayStart      = 0;
unsigned long lastDiscovery   = 0;
unsigned long lastPingSent    = 0;
unsigned long lastStatusSent  = 0;
unsigned long portaAbertaTempo = 0;
unsigned long masterLeaseExpire = 0;
unsigned long lastOpenEvent   = 0;
unsigned long sensorDebounce  = 0;
unsigned long openToken       = 0;
unsigned long currentToken    = 0;

// ===================== TOKEN LOCK ==========================
bool tokenActive = false;
char tokenPorta[16] = "";
unsigned long tokenExpire = 0;

bool adquirirToken(const char *porta)
{
  if (!tokenActive || millis() > tokenExpire)
  {
    tokenActive = true;
    strncpy(tokenPorta, porta, sizeof(tokenPorta) - 1);
    tokenPorta[sizeof(tokenPorta) - 1] = '\0';
    tokenExpire = millis() + 10000UL;
    return true;
  }
  return strcmp(tokenPorta, porta) == 0;
}

void liberarToken()
{
  tokenActive    = false;
  tokenPorta[0]  = '\0';
}

// ===================== ESTADO DAS OUTRAS PORTAS =============
bool portaAAberta = false;
bool portaBAberta = false;
bool portaALock   = false;
bool portaBLock   = false;

bool aguardandoAutorizacao = false;
unsigned long lastStatusPortaA = 0;
unsigned long lastStatusPortaB = 0;
unsigned long lastMasterSeen   = 0;
unsigned long reqTimeout       = 0;

// ===================== FUNÇÕES AUXILIARES ==================

void sendBroadcast(const char *msg)
{
  IPAddress broadcastIP = WiFi.localIP() | ~WiFi.subnetMask();
  udp.beginPacket(broadcastIP, UDP_PORT);
  udp.print(msg);
  udp.endPacket();
}

void sendStatus()
{
  char msg[64];
  snprintf(msg, sizeof(msg), "STATUS|%s|%s",
           DEVICE_NAME,
           portaAberta ? "OPEN" : "CLOSED");
  sendBroadcast(msg);
  lastStatusSent = millis();
}

bool deviceKnown(const char *dev)
{
  if (strcmp(dev, DEVICE_NAME) == 0)
    return true;
  for (int i = 0; i < 5; i++)
  {
    if (strcmp(knownNames[i], dev) == 0)
      return true;
  }
  return false;
}

void addDevice(const char *dev, const char *ip)
{
  if (!dev || !ip || strcmp(dev, DEVICE_NAME) == 0)
    return;

  // Atualiza dispositivo existente
  for (int i = 0; i < 5; i++)
  {
    if (strcmp(knownNames[i], dev) == 0)
    {
      strncpy(knownIPs[i], ip, sizeof(knownIPs[i]) - 1);
      knownIPs[i][sizeof(knownIPs[i]) - 1] = '\0';
      lastPing[i] = millis();
      return;
    }
  }
  // Adiciona novo dispositivo
  for (int i = 0; i < 5; i++)
  {
    if (knownNames[i][0] == '\0')
    {
      strncpy(knownNames[i], dev, sizeof(knownNames[i]) - 1);
      knownNames[i][sizeof(knownNames[i]) - 1] = '\0';
      strncpy(knownIPs[i], ip, sizeof(knownIPs[i]) - 1);
      knownIPs[i][sizeof(knownIPs[i]) - 1] = '\0';
      lastPing[i] = millis();
      Serial.print("[DISCOVERY] ");
      Serial.print(dev);
      Serial.print(" -> ");
      Serial.println(ip);
      break;
    }
  }
}

// ===================== LÓGICA DE MASTER ====================
// Ponto único de eleição: chamado sempre que um HELLO é recebido
// ou quando o master atual é detectado como ausente.

void assumirMaster()
{
  Serial.println("👑 Assumindo papel de master!");
  masterPriority = devicePriority;
  strncpy(networkMaster, DEVICE_NAME, sizeof(networkMaster) - 1);
  networkMaster[sizeof(networkMaster) - 1] = '\0';
  isMaster          = true;
  masterLeaseExpire = millis() + MASTER_LEASE_TIME * 2;
  lastMasterSeen    = millis();
}

void abdicarMaster(int novaPrio, const char *novoDev)
{
  Serial.println("⚠️ Detectado master com prioridade maior!");
  Serial.println("➡️ Abdicando do papel de master");
  isMaster       = false;
  masterPriority = novaPrio;
  strncpy(networkMaster, novoDev, sizeof(networkMaster) - 1);
  networkMaster[sizeof(networkMaster) - 1] = '\0';
}

// Verifica se o master atual sumiu e elege novo se necessário.
// Chamado uma única vez no loop, consolida as duas lógicas duplicadas.
void checkMasterHealth()
{
  if (isMaster)
    return; // sou master, nada a fazer

  bool masterConhecido = (networkMaster[0] != '\0');

  // Expirou o lease enviado pelo próprio master via HELLO
  bool leaseExpirou = masterConhecido && (millis() > masterLeaseExpire);

  // Não recebemos HELLO/PING do master há MASTER_TIMEOUT ms
  bool masterSilencioso = masterConhecido && (millis() - lastMasterSeen > MASTER_TIMEOUT);

  if (leaseExpirou || masterSilencioso)
  {
    Serial.print("⚠️ Master perdido (");
    Serial.print(leaseExpirou ? "lease expirado" : "timeout");
    Serial.println(")!");
    assumirMaster();
  }
}

// Processa HELLO recebido e atualiza eleição de master
void processHello(const char *dev, int prio, const char *ip, unsigned long lease)
{
  // Anti split-brain: se eu sou master e aparece alguém com prio maior, abdico
  if (isMaster && prio > devicePriority)
  {
    abdicarMaster(prio, dev);
  }

  // Renova lease se for o master atual
  if (strcmp(dev, networkMaster) == 0)
  {
    masterLeaseExpire = millis() + lease;
    lastMasterSeen    = millis();
  }

  // Atualiza master se prio maior
  if (prio > masterPriority)
  {
    masterPriority = prio;
    strncpy(networkMaster, dev, sizeof(networkMaster) - 1);
    networkMaster[sizeof(networkMaster) - 1] = '\0';
    isMaster       = (strcmp(dev, DEVICE_NAME) == 0);
    lastMasterSeen = millis();
  }
  // Desempate por IP quando prioridade igual
  else if (prio == masterPriority && strcmp(dev, DEVICE_NAME) != 0)
  {
    if (strcmp(ip, localIP.toString().c_str()) > 0)
    {
      strncpy(networkMaster, dev, sizeof(networkMaster) - 1);
      networkMaster[sizeof(networkMaster) - 1] = '\0';
      isMaster       = false;
      lastMasterSeen = millis();
    }
  }
}

// ===================== INTERTRAVAMENTO =====================

bool podeAbrir(const char *portaAlvo)
{
  // Cooldown após última abertura
  if (millis() - lastOpenEvent < 2000UL)
  {
    Serial.println("⏳ Sistema aguardando estabilização");
    return false;
  }

  if (bypassMode)
  {
    Serial.println("✅ Bypass ativo, abrindo sem intertravamento");
    return true;
  }

  if (strcmp(portaAlvo, DEV_PORTA_A) == 0)
  {
    if (lastStatusPortaB == 0 || millis() - lastStatusPortaB > 10000UL)
    {
      Serial.println("⚠️ Sem comunicação com PORTA_B! Bloqueando por segurança.");
      return false;
    }
    if (portaBAberta || portaBLock)
    {
      Serial.println("🚫 PORTA_B está aberta/travada! Não pode abrir PORTA_A.");
      return false;
    }
    Serial.println("✅ PORTA_B fechada, pode abrir PORTA_A");
  }
  else if (strcmp(portaAlvo, DEV_PORTA_B) == 0)
  {
    if (lastStatusPortaA == 0 || millis() - lastStatusPortaA > 10000UL)
    {
      Serial.println("⚠️ Sem comunicação com PORTA_A! Bloqueando por segurança.");
      return false;
    }
    if (portaAAberta || portaALock)
    {
      Serial.println("🚫 PORTA_A está aberta/travada! Não pode abrir PORTA_B.");
      return false;
    }
    Serial.println("✅ PORTA_A fechada, pode abrir PORTA_B");
  }

  return true;
}

// ===================== ABERTURA ============================

void abrirPorta()
{
  if (relayAtivo)
    return;

  Serial.println("🚪 Abrindo porta");
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(PUPE_PIN, LOW);
  relayStart     = millis();
  relayAtivo     = true;
  lastOpenEvent  = millis();
}

void solicitarAbertura(const char *porta)
{
  if (strcmp(porta, DEVICE_NAME) == 0 && podeAbrir(porta))
  {
    Serial.println("✅ Condições OK, abrindo localmente");
    abrirPorta();
    char ackMsg[64];
    snprintf(ackMsg, sizeof(ackMsg), "ACK_OPEN|%s", DEVICE_NAME);
    sendBroadcast(ackMsg);
    return;
  }

  if (networkMaster[0] != '\0')
  {
    Serial.println("📨 Pedindo autorização ao master");
    openToken = millis();
    char reqMsg[64];
    snprintf(reqMsg, sizeof(reqMsg), "REQ_OPEN|%s|%s|%lu", porta, DEVICE_NAME, openToken);
    sendBroadcast(reqMsg);
    aguardandoAutorizacao = true;
    reqTimeout            = millis();
  }
  else
  {
    Serial.println("❌ Nenhum master disponível e não pode abrir");
  }
}

// ===================== PROCESSAMENTO DE MENSAGENS ==========

// Extrai até maxFields campos delimitados por '|' de buf (modificado in-place).
// Retorna o número de campos encontrados.
static int splitMsg(char *buf, size_t bufSize, const char *src, char **fields, int maxFields)
{
  strncpy(buf, src, bufSize - 1);
  buf[bufSize - 1] = '\0';
  int count = 0;
  char *tok = strtok(buf, "|");
  while (tok && count < maxFields)
  {
    fields[count++] = tok;
    tok = strtok(NULL, "|");
  }
  return count;
}

void processMessage(char *msg)
{
  char buf[256];
  char *f[8];
  int  n;

  // ---- DISCOVERY / CONFIRM / PING (mesma estrutura: CMD|DEV|IP) ----
  if (strncmp(msg, "DISCOVERY|", 10) == 0 ||
      strncmp(msg, "CONFIRM|",   8)  == 0)
  {
    n = splitMsg(buf, sizeof(buf), msg, f, 3);
    if (n >= 3)
      addDevice(f[1], f[2]);
  }

  else if (strncmp(msg, "PING|", 5) == 0)
  {
    n = splitMsg(buf, sizeof(buf), msg, f, 3);
    if (n >= 3)
    {
      addDevice(f[1], f[2]);
      char pongMsg[64];
      snprintf(pongMsg, sizeof(pongMsg), "PONG|%s|%s", DEVICE_NAME, localIP.toString().c_str());
      udp.beginPacket(udp.remoteIP(), UDP_PORT);
      udp.print(pongMsg);
      udp.endPacket();
    }
  }

  // ---- HELLO|DEV|PRIO|IP|ROLE|LEASE ----
  else if (strncmp(msg, "HELLO|", 6) == 0)
  {
    n = splitMsg(buf, sizeof(buf), msg, f, 6);
    if (n < 6)
      return; // FIX: proteção contra crash com strtok(NULL) em mensagem truncada
    int           prio  = atoi(f[2]);
    unsigned long lease = (unsigned long)atol(f[5]);
    addDevice(f[1], f[3]);
    processHello(f[1], prio, f[3], lease);
  }

  // ---- PONG|DEV|IP ----
  else if (strncmp(msg, "PONG|", 5) == 0)
  {
    n = splitMsg(buf, sizeof(buf), msg, f, 3);
    if (n < 2)
      return;
    for (int i = 0; i < 5; i++)
    {
      if (strcmp(knownNames[i], f[1]) == 0)
      {
        lastPing[i] = millis();
        break;
      }
    }
  }

  // ---- REQ_OPEN|PORTA|ORIGEM|TOKEN (apenas master processa) ----
  else if (strncmp(msg, "REQ_OPEN|", 9) == 0)
  {
    if (!isMaster)
      return;

    n = splitMsg(buf, sizeof(buf), msg, f, 4);
    if (n < 4)
      return;

    const char    *porta   = f[1];
    const char    *origem  = f[2];
    unsigned long  tokenID = (unsigned long)atol(f[3]);

    if (!deviceKnown(origem))
      return;

    Serial.print("📩 Pedido de abertura: ");
    Serial.print(porta);
    Serial.print(" de ");
    Serial.println(origem);

    if (masterBusy || !podeAbrir(porta))
    {
      char denyMsg[64];
      snprintf(denyMsg, sizeof(denyMsg), "DENY|%s", porta);
      sendBroadcast(denyMsg);
      return;
    }

    if (adquirirToken(porta))
    {
      masterBusy    = true;
      masterBusyTime = millis();
      currentToken  = tokenID;
      strncpy(tokenOwner, origem, sizeof(tokenOwner) - 1);
      tokenOwner[sizeof(tokenOwner) - 1] = '\0';
      char allowMsg[64];
      snprintf(allowMsg, sizeof(allowMsg), "ALLOW|%s|%lu", porta, currentToken);
      sendBroadcast(allowMsg);
    }
    else
    {
      char denyMsg[64];
      snprintf(denyMsg, sizeof(denyMsg), "DENY|%s", porta);
      sendBroadcast(denyMsg);
    }
  }

  // ---- ALLOW|PORTA|TOKEN ----
  else if (strncmp(msg, "ALLOW|", 6) == 0)
  {
    n = splitMsg(buf, sizeof(buf), msg, f, 3);
    if (n < 3)
      return;
    unsigned long tokenID = (unsigned long)atol(f[2]);
    if (strcmp(f[1], DEVICE_NAME) == 0 && tokenID == openToken)
    {
      Serial.println("✅ Autorização recebida!");
      lastOpenEvent = millis();
      abrirPorta();
      char ackMsg[64];
      snprintf(ackMsg, sizeof(ackMsg), "ACK_OPEN|%s", DEVICE_NAME);
      sendBroadcast(ackMsg);
      aguardandoAutorizacao = false;
    }
  }

  // ---- DENY|PORTA ----
  else if (strncmp(msg, "DENY|", 5) == 0)
  {
    if (strcmp(msg + 5, DEVICE_NAME) == 0)
    {
      Serial.println("❌ Abertura negada!");
      aguardandoAutorizacao = false;
    }
  }

  // ---- ACK_OPEN|PORTA ----
  else if (strncmp(msg, "ACK_OPEN|", 9) == 0)
  {
    Serial.print("✔ Porta confirmou abertura: ");
    Serial.println(msg + 9);
    if (isMaster)
    {
      masterBusy = false;
      liberarToken();
    }
  }

  // ---- OPEN|PORTA (comando direto) ----
  else if (strncmp(msg, "OPEN|", 5) == 0)
  {
    if (strcmp(msg + 5, DEVICE_NAME) == 0)
      solicitarAbertura(DEVICE_NAME);
  }

  // ---- BYPASS|ON ou BYPASS|OFF ----
  else if (strncmp(msg, "BYPASS|", 7) == 0)
  {
    // FIX: comparação exata em vez de strstr (evita falso positivo em "BYPASS|NONE")
    bypassMode = (strcmp(msg + 7, "ON") == 0);
    Serial.println(bypassMode ? "⚠️ Bypass ativado!" : "🔒 Bypass desativado.");
  }

  // ---- STATUS|DEV|OPEN|CLOSED ----
  else if (strncmp(msg, "STATUS|", 7) == 0)
  {
    n = splitMsg(buf, sizeof(buf), msg, f, 3);
    if (n < 3)
      return;
    const char *dev    = f[1];
    const char *estado = f[2];

    if (strcmp(dev, DEV_PORTA_A) == 0)
    {
      bool anterior = portaAAberta;
      portaAAberta  = (strcmp(estado, "OPEN") == 0);
      portaALock    = portaAAberta && (strcmp(DEVICE_NAME, DEV_PORTA_A) != 0);
      lastStatusPortaA = millis();
      Serial.print("[STATUS] PORTA_A: ");
      Serial.print(estado);
      Serial.println(anterior != portaAAberta ? " (MUDOU!)" : "");
    }
    else if (strcmp(dev, DEV_PORTA_B) == 0)
    {
      bool anterior = portaBAberta;
      portaBAberta  = (strcmp(estado, "OPEN") == 0);
      portaBLock    = portaBAberta && (strcmp(DEVICE_NAME, DEV_PORTA_B) != 0);
      lastStatusPortaB = millis();
      Serial.print("[STATUS] PORTA_B: ");
      Serial.print(estado);
      Serial.println(anterior != portaBAberta ? " (MUDOU!)" : "");
    }
  }

  // ---- LOCK|DEV ----
  else if (strncmp(msg, "LOCK|", 5) == 0)
  {
    const char *dev = msg + 5;
    if (strcmp(dev, DEV_PORTA_A) == 0) portaALock = true;
    if (strcmp(dev, DEV_PORTA_B) == 0) portaBLock = true;
    Serial.print("🔒 LOCK recebido de ");
    Serial.println(dev);
  }

  // ---- UNLOCK|DEV ----
  else if (strncmp(msg, "UNLOCK|", 7) == 0)
  {
    const char *dev = msg + 7;
    if (strcmp(dev, DEV_PORTA_A) == 0) portaALock = false;
    if (strcmp(dev, DEV_PORTA_B) == 0) portaBLock = false;
    Serial.print("🔓 UNLOCK recebido de ");
    Serial.println(dev);
  }

  // ---- ALERT|TIPO|ORIGEM ----
  else if (strncmp(msg, "ALERT|", 6) == 0)
  {
    n = splitMsg(buf, sizeof(buf), msg, f, 3);
    if (n >= 3)
    {
      Serial.print("🚨 ALERTA recebido: ");
      Serial.print(f[1]);
      Serial.print(" de ");
      Serial.println(f[2]);
    }
  }
}

// ===================== SETUP ================================

void setup()
{
  Serial.begin(115200);

  pinMode(BTN1_PIN,   INPUT_PULLUP);
  pinMode(BTN2_PIN,   INPUT_PULLUP);
  pinMode(BYPASS_PIN, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN,  OUTPUT);
  pinMode(PUPE_PIN,   OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(PUPE_PIN,  HIGH);

  memset(knownNames, 0, sizeof(knownNames));
  memset(knownIPs,   0, sizeof(knownIPs));
  memset(lastPing,   0, sizeof(lastPing));

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi conectado!");
  localIP = WiFi.localIP();
  Serial.println("IP: " + localIP.toString());
  Serial.println("Device: " DEVICE_NAME);

  udp.begin(UDP_PORT);

  // Define prioridade do dispositivo
  if      (strcmp(DEVICE_NAME, DEV_PORTEIRO) == 0) devicePriority = 3;
  else if (strcmp(DEVICE_NAME, DEV_PORTA_A)  == 0) devicePriority = 2;
  else                                               devicePriority = 1;

  // Lê estado inicial do sensor
  portaAberta   = (digitalRead(SENSOR_PIN) == SENSOR_OPEN);
  sensorLeitura = portaAberta;

  // Assume master até ouvir alguém com prioridade maior
  assumirMaster();

  // Anuncia presença
  char msg[64];
  snprintf(msg, sizeof(msg), "DISCOVERY|%s|%s", DEVICE_NAME, localIP.toString().c_str());
  sendBroadcast(msg);
  sendStatus();
}

// ===================== LOOP =================================

void loop()
{
  unsigned long now = millis();

  // --- Leitura do sensor com debounce ---
  bool leitura = (digitalRead(SENSOR_PIN) == SENSOR_OPEN);
  if (leitura != sensorLeitura && now - sensorDebounce > 100UL)
  {
    sensorDebounce = now;
    sensorLeitura  = leitura;
  }

  // --- Reconexão WiFi ---
  static unsigned long lastReconnect = 0;
  static bool wifiWasDown = false;

  if (WiFi.status() != WL_CONNECTED)
  {
    wifiWasDown = true;
    if (now - lastReconnect > 5000UL)
    {
      Serial.println("⚠️ Tentando reconectar WiFi...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      if (WiFi.status() == WL_CONNECTED)
      {
        udp.stop();
        udp.begin(UDP_PORT);
      }
      lastReconnect = now;
    }
  }
  else if (wifiWasDown)
  {
    Serial.println("✅ Conexão restabelecida!");
    wifiWasDown = false;
    sendStatus();
  }

  // --- Receber pacotes UDP ---
  int packetSize = udp.parsePacket();
  if (packetSize)
  {
    char buffer[256];
    int len = udp.read(buffer, 255);
    if (len > 0)
    {
      buffer[len] = '\0';
      processMessage(buffer);
    }
  }

  // --- Discovery + HELLO a cada 15s ---
  if (now - lastDiscovery > 15000UL)
  {
    char msg[64];
    snprintf(msg, sizeof(msg), "DISCOVERY|%s|%s", DEVICE_NAME, localIP.toString().c_str());
    sendBroadcast(msg);

    char helloMsg[128];
    snprintf(helloMsg, sizeof(helloMsg), "HELLO|%s|%d|%s|%s|%lu",
             DEVICE_NAME, devicePriority,
             localIP.toString().c_str(),
             isMaster ? "MASTER" : "NODE",
             MASTER_LEASE_TIME);
    sendBroadcast(helloMsg);
    lastDiscovery = now;
  }

  // --- PING a cada 15s ---
  if (now - lastPingSent > 15000UL)
  {
    char pingMsg[64];
    snprintf(pingMsg, sizeof(pingMsg), "PING|%s|%s", DEVICE_NAME, localIP.toString().c_str());
    sendBroadcast(pingMsg);
    lastPingSent = now;
  }

  // --- STATUS a cada 15s ---
  if (now - lastStatusSent > 15000UL)
  {
    sendStatus();

    // Limpa estado de portas que não enviam status há 20s
    if (lastStatusPortaA != 0 && now - lastStatusPortaA > 20000UL)
    {
      portaAAberta = false;
      portaALock   = false;
    }
    if (lastStatusPortaB != 0 && now - lastStatusPortaB > 20000UL)
    {
      portaBAberta = false;
      portaBLock   = false;
    }

    // Log periódico
    Serial.print("Minha porta: ");
    Serial.println(portaAberta ? "ABERTA" : "FECHADA");
    Serial.print("PORTA_A: ");
    Serial.print(portaAAberta ? "ABERTA" : "FECHADA");
    Serial.print(" (ultima att: ");
    Serial.print(lastStatusPortaA ? (now - lastStatusPortaA) : 0UL);
    Serial.println(" ms)");
    Serial.print("PORTA_B: ");
    Serial.print(portaBAberta ? "ABERTA" : "FECHADA");
    Serial.print(" (ultima att: ");
    Serial.print(lastStatusPortaB ? (now - lastStatusPortaB) : 0UL);
    Serial.println(" ms)");
    Serial.print("Master: ");
    Serial.print(networkMaster);
    Serial.println(isMaster ? " (EU)" : "");
    Serial.print("Bypass: ");
    Serial.println(bypassMode ? "ON" : "OFF");
    Serial.print("Relay ativo: ");
    Serial.println(relayAtivo ? "SIM" : "NAO");
    Serial.println("-------------------------");
  }

  // --- Remove dispositivos inativos (sem PONG há 30s) ---
  for (int i = 0; i < 5; i++)
  {
    if (knownNames[i][0] != '\0' && now - lastPing[i] > 30000UL)
    {
      // FIX: substituído String concatenation por Serial.print separado
      Serial.print("⚠️ Dispositivo inativo removido: ");
      Serial.println(knownNames[i]);

      if (strcmp(knownNames[i], networkMaster) == 0)
      {
        Serial.println("⚠️ Master removido por inatividade!");
        networkMaster[0] = '\0';
        masterPriority   = 0;
        isMaster         = false;
        // checkMasterHealth() vai eleger novo master no próximo ciclo
      }
      knownNames[i][0] = '\0';
      knownIPs[i][0]   = '\0';
    }
  }

  // --- Lógica de botões ---
  if (strcmp(DEVICE_NAME, DEV_PORTEIRO) == 0)
  {
    // Botão 1 -> PORTA_A
    static bool lastBtn1 = HIGH;
    static unsigned long lastBtn1Press = 0;
    bool btn1 = digitalRead(BTN1_PIN);
    if (btn1 == LOW && lastBtn1 == HIGH && now - lastBtn1Press > 300UL)
    {
      lastBtn1Press = now;
      Serial.println("🔘 Botão 1 pressionado - PORTA_A");
      if (podeAbrir(DEV_PORTA_A))
        solicitarAbertura(DEV_PORTA_A);
      else
        Serial.println("❌ Bloqueado pelo intertravamento");
    }
    lastBtn1 = btn1;

    // Botão 2 -> PORTA_B
    static bool lastBtn2 = HIGH;
    static unsigned long lastBtn2Press = 0;
    bool btn2 = digitalRead(BTN2_PIN);
    if (btn2 == LOW && lastBtn2 == HIGH && now - lastBtn2Press > 300UL)
    {
      lastBtn2Press = now;
      Serial.println("🔘 Botão 2 pressionado - PORTA_B");
      if (podeAbrir(DEV_PORTA_B))
        solicitarAbertura(DEV_PORTA_B);
      else
        Serial.println("❌ Bloqueado pelo intertravamento");
    }
    lastBtn2 = btn2;

    // Bypass
    bool bypassState = (digitalRead(BYPASS_PIN) == LOW);
    static bool lastBypass = false;
    if (bypassState != lastBypass)
    {
      char bypassMsg[16];
      snprintf(bypassMsg, sizeof(bypassMsg), "BYPASS|%s", bypassState ? "ON" : "OFF");
      sendBroadcast(bypassMsg);
      lastBypass = bypassState;
      Serial.print("🔀 Bypass alterado: ");
      Serial.println(bypassState ? "ON" : "OFF");
    }
  }
  else
  {
    // Botão local nas portas
    static bool lastBtn = HIGH;
    static unsigned long lastPress = 0;
    bool btnState = digitalRead(BTN1_PIN);
    if (btnState == LOW && lastBtn == HIGH && now - lastPress > 300UL)
    {
      lastPress = now;
      Serial.println("🔘 Botão local pressionado");
      solicitarAbertura(DEVICE_NAME);
    }
    lastBtn = btnState;
  }

  // --- FIX: Atualização de estado do sensor (era novoEstado, agora sensorLeitura) ---
  // sensorLeitura já foi atualizado com debounce no início do loop.
  if (sensorLeitura != portaAberta)
  {
    portaAberta = sensorLeitura;

    if (portaAberta)
    {
      portaAbertaTempo = now;
      alertSent        = false;

      char lockMsg[32];
      snprintf(lockMsg, sizeof(lockMsg), "LOCK|%s", DEVICE_NAME);
      sendBroadcast(lockMsg);

      if (strcmp(DEVICE_NAME, DEV_PORTA_A) == 0)
      {
        portaAAberta     = true;
        portaALock       = true;
        lastStatusPortaA = now;
      }
      else if (strcmp(DEVICE_NAME, DEV_PORTA_B) == 0)
      {
        portaBAberta     = true;
        portaBLock       = true;
        lastStatusPortaB = now;
      }
    }
    else
    {
      portaAbertaTempo = 0;

      char unlockMsg[32];
      snprintf(unlockMsg, sizeof(unlockMsg), "UNLOCK|%s", DEVICE_NAME);
      sendBroadcast(unlockMsg);

      if (strcmp(DEVICE_NAME, DEV_PORTA_A) == 0)
      {
        portaAAberta     = false;
        portaALock       = false;
        lastStatusPortaA = now;
      }
      else if (strcmp(DEVICE_NAME, DEV_PORTA_B) == 0)
      {
        portaBAberta     = false;
        portaBLock       = false;
        lastStatusPortaB = now;
      }
    }

    sendStatus();
    lastStatusSent = now;
    Serial.println(portaAberta ? "🚪 Sensor: Porta ABERTA" : "🚪 Sensor: Porta FECHADA");
  }

  // --- Desliga o relé após RELAY_TIME ---
  if (relayAtivo && now - relayStart >= RELAY_TIME)
  {
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(PUPE_PIN,  HIGH);
    relayAtivo = false;
    Serial.println("🔒 Relé desligado.");

    // Sincroniza estado com sensor após fechar relé
    bool sensorAberto = (digitalRead(SENSOR_PIN) == SENSOR_OPEN);
    if (portaAberta != sensorAberto)
    {
      portaAberta = sensorAberto;
      sendStatus();
    }
  }

  // --- Alerta de porta aberta por muito tempo ---
  if (portaAberta && !alertSent && now - portaAbertaTempo > PORTA_TIMEOUT)
  {
    Serial.println("⚠️ Porta aberta por muito tempo! Enviando alerta...");
    char alertMsg[64];
    snprintf(alertMsg, sizeof(alertMsg), "ALERT|PORTA_ABERTA|%s", DEVICE_NAME);
    sendBroadcast(alertMsg);
    alertSent = true;
  }

  // --- Timeout de autorização ---
  if (aguardandoAutorizacao && now - reqTimeout > 3000UL)
  {
    Serial.println("⚠️ Timeout aguardando autorização do master!");
    aguardandoAutorizacao = false;
  }

  // --- Timeout do master busy ---
  if (masterBusy && now - masterBusyTime > 10000UL)
  {
    Serial.println("⚠️ Master busy timeout! Liberando sistema.");
    masterBusy = false;
    liberarToken();
    portaALock = false;
    portaBLock = false;
  }

  // --- Eleição de master (ponto único, substitui dois blocos duplicados) ---
  checkMasterHealth();
}
