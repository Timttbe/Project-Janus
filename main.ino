#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

<<<<<<< HEAD
// ====================== CONFIGURAÇÕES ======================
#define DEVICE_NAME "PORTEIRO"  // 👉 altere para "PORTA_A", "PORTA_B" ou "PORTEIRO"

// ---- Rede gerada pelo PORTEIRO (fixa, nunca muda) ----
#define AP_SSID "PORTEIRO_AP"
#define AP_PASS "porteiro123"

// ---- WiFi local (só PORTEIRO usa) ----
// Salvo em EEPROM via portal web — não precisa mexer aqui
#define EEPROM_SIZE       256
#define EEPROM_SSID_ADDR    0
#define EEPROM_PASS_ADDR   64
#define EEPROM_VALID_ADDR 128
#define EEPROM_VALID_BYTE 0xAA

#define UDP_PORT   4210
#define RELAY_TIME 5000

// ===================== PINOS ================================
#define BTN1_PIN   5   // D1
#define BTN2_PIN   4   // D2
#define BYPASS_PIN 2   // D4 — deve estar HIGH no boot
#define SENSOR_PIN 14  // D5
#define RELAY_PIN  12  // D6
#define PUPE_PIN   13  // D7

// ===================== VARIÁVEIS ============================
WiFiUDP          udp;
ESP8266WebServer server(80);
IPAddress        localIP;

// Dispositivos conhecidos
struct Device {
  String name;
  String ip;
  unsigned long lastSeen;
  bool portaAberta;
=======
// Change per device before flashing: "PORTA_A", "PORTA_B" or "PORTEIRO"
#define DEVICE_NAME  "PORTA_A"
#define DEV_PORTA_A  "PORTA_A"
#define DEV_PORTA_B  "PORTA_B"
#define DEV_PORTEIRO "PORTEIRO"

#define EEPROM_SIZE        256
#define EEPROM_SSID_ADDR     0
#define EEPROM_PASS_ADDR    64
#define EEPROM_VALID_ADDR  128
#define EEPROM_VALID_MAGIC 0xAA

// PORTA_A and PORTA_B use these same constants to connect back to PORTEIRO on fallback
#define FALLBACK_AP_SSID "PORTEIRO_AP"
#define FALLBACK_AP_PASS "porteiro123"
#define FALLBACK_AP_IP   "192.168.4.1"

#define WIFI_CONNECT_TIMEOUT   15000UL
#define WIFI_RECONNECT_TIMEOUT 20000UL

// Must be identical on all three devices; rejects commands from unknown sources
#define SHARED_KEY "WIRES2025"

#define PORTA_TIMEOUT       300000UL
#define UDP_PORT            4210
#define RELAY_TIME          5000UL
#define MASTER_BUSY_TIMEOUT (RELAY_TIME + 1000UL)
#define SENSOR_OPEN         HIGH  // INPUT_PULLUP: open door = open contact = HIGH
#define MASTER_LEASE_TIME   15000UL
#define MASTER_TIMEOUT      20000UL
#define MAX_DEVICES         10
#define TOKEN_TIMEOUT       10000UL
#define REQ_RETRY_INTERVAL  1000UL
#define REQ_MAX_RETRIES     3
#define APP_WATCHDOG_TIMEOUT 8000UL
#define WIFICFG_INTERVAL    30000UL

#define BTN1_PIN   5
#define BTN2_PIN   4
#define BYPASS_PIN 0   // GPIO0: boot-sensitive, keep HIGH at power-on (add 10k pull-up)
#define SENSOR_PIN 14
#define RELAY_PIN  12
#define PUPE_PIN   13

struct Device
{
  char          name[16];
  char          ip[16];
  unsigned long lastPing;
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16
};
Device knownDevices[5];

<<<<<<< HEAD
bool          bypassMode      = false;
bool          portaAberta     = false;
unsigned long relayStart      = 0;
bool          relayAtivo      = false;
unsigned long lastDiscovery   = 0;
unsigned long lastPingSent    = 0;
unsigned long lastStatusSent  = 0;

bool          portaAAberta    = false;
bool          portaBAberta    = false;
unsigned long lastStatusPortaA = 0;
unsigned long lastStatusPortaB = 0;

bool          configMode      = false;
bool          isMaster        = false; // true = portal de config sem WiFi salvo

// ===================== EEPROM ===============================
void nvRead(int addr, char* buf, int maxLen) {
  for (int i = 0; i < maxLen - 1; i++) {
=======
Device devices[MAX_DEVICES];

inline void deviceClear(int i)
{
  devices[i].name[0]  = '\0';
  devices[i].ip[0]    = '\0';
  devices[i].lastPing = 0;
}

inline bool deviceEmpty(int i)               { return devices[i].name[0] == '\0'; }
inline bool deviceMatch(int i, const char *n) { return strcmp(devices[i].name, n) == 0; }

enum WifiMode { WIFI_MODE_NORMAL, WIFI_MODE_CONFIG, WIFI_MODE_FALLBACK };
WifiMode      wifiMode      = WIFI_MODE_NORMAL;
bool          fallbackAtivo = false;
unsigned long wifiDownSince = 0;

ESP8266WebServer webServer(80);

WiFiUDP   udp;
IPAddress localIP;

int  devicePriority = 1;
int  masterPriority = 0;
bool isMaster       = false;
char networkMaster[16] = "";
char tokenOwner[16]    = "";

bool bypassMode    = false;
bool bypassBootOk  = false;
unsigned long bypassBootTime = 0;
bool portaAberta   = false;
bool sensorLeitura = false;
bool relayAtivo    = false;
bool masterBusy    = false;
bool alertSent     = false;

unsigned long masterBusyTime   = 0;
unsigned long relayStart       = 0;
unsigned long lastDiscovery    = 0;
unsigned long lastPingSent     = 0;
unsigned long lastStatusSent   = 0;
unsigned long portaAbertaTempo = 0;
unsigned long masterLeaseStart = 0;
unsigned long lastOpenEvent    = 0;
unsigned long sensorDebounce   = 0;
unsigned long openToken        = 0;
unsigned long currentToken     = 0;

bool          tokenActive = false;
char          tokenPorta[16] = "";
unsigned long tokenStart  = 0;

// Token must not expire while a relay is physically active —
// expiring early would allow the other door to open mid-cycle.
static bool tokenExpirou()
{
  if (!tokenActive) return false;
  bool relayOcupado = relayAtivo || portaARelayAtivo || portaBRelayAtivo;
  return !relayOcupado && (millis() - tokenStart > TOKEN_TIMEOUT);
}

bool adquirirToken(const char *porta)
{
  if (!tokenActive || tokenExpirou())
  {
    tokenActive = true;
    tokenStart  = millis();
    strncpy(tokenPorta, porta, sizeof(tokenPorta) - 1);
    tokenPorta[sizeof(tokenPorta) - 1] = '\0';
    return true;
  }
  return strcmp(tokenPorta, porta) == 0;
}

void liberarToken()
{
  tokenActive   = false;
  tokenPorta[0] = '\0';
}

bool portaAAberta = false;
bool portaBAberta = false;
bool portaALock   = false;
bool portaBLock   = false;

bool          aguardandoAutorizacao = false;
unsigned long lastStatusPortaA     = 0;
unsigned long lastStatusPortaB     = 0;
unsigned long lastMasterSeen       = 0;

char          reqPorta[16] = "";
int           reqRetryCount = 0;
unsigned long lastReqSent   = 0;

unsigned long lastLoopWatchdog = 0;
unsigned long lastWificfgSent  = 0;

void eepromReadStr(int addr, char *buf, int maxLen)
{
  for (int i = 0; i < maxLen - 1; i++)
  {
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16
    buf[i] = EEPROM.read(addr + i);
    if (buf[i] == '\0') break;
  }
  buf[maxLen - 1] = '\0';
}
void nvWrite(int addr, const char* str, int maxLen) {
  int len = min((int)strlen(str), maxLen - 1);
  for (int i = 0; i < len; i++) EEPROM.write(addr + i, str[i]);
  EEPROM.write(addr + len, '\0');
}
bool nvHasCred()  { return EEPROM.read(EEPROM_VALID_ADDR) == EEPROM_VALID_BYTE; }
void nvSave(const char* s, const char* p) {
  nvWrite(EEPROM_SSID_ADDR, s, 64);
  nvWrite(EEPROM_PASS_ADDR, p, 64);
  EEPROM.write(EEPROM_VALID_ADDR, EEPROM_VALID_BYTE);
  EEPROM.commit();
<<<<<<< HEAD
=======
  Serial.println(F("Credentials saved."));
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16
}
void nvClear() { EEPROM.write(EEPROM_VALID_ADDR, 0); EEPROM.commit(); }

<<<<<<< HEAD
// ===================== UDP ==================================
void sendBroadcast(const String& msg) {
  udp.beginPacket("192.168.4.255", UDP_PORT);
  udp.print(msg);
=======
void startConfigPortal()
{
  char apName[32];
  snprintf(apName, sizeof(apName), "%s_CONFIG", DEVICE_NAME);

  Serial.print(F("No credentials. Starting config AP: "));
  Serial.println(apName);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName);
  Serial.print(F("Portal IP: "));
  Serial.println(WiFi.softAPIP());

  webServer.on("/", HTTP_GET, []() {
    static const char PAGE[] PROGMEM =
      "<!DOCTYPE html><html><head>"
      "<meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Configurar WiFi</title>"
      "<style>"
      "body{font-family:sans-serif;max-width:360px;margin:40px auto;padding:0 16px;background:#f9f9f9;}"
      "h2{color:#1565C0;}p{color:#555;font-size:.9em;}"
      "label{display:block;margin-top:14px;font-size:.85em;color:#777;font-weight:bold;}"
      "input{width:100%;padding:9px;box-sizing:border-box;border:1px solid #ccc;"
      "border-radius:6px;font-size:1em;margin-top:4px;}"
      "button{margin-top:18px;width:100%;padding:11px;background:#1976D2;color:#fff;"
      "border:none;border-radius:6px;font-size:1em;cursor:pointer;font-weight:bold;}"
      "button:hover{background:#1565C0;}"
      ".footer{font-size:.75em;color:#aaa;text-align:center;margin-top:24px;}"
      "</style></head><body>"
      "<h2>Configurar WiFi</h2>"
      "<p>Dispositivo: <strong>" DEVICE_NAME "</strong></p>"
      "<p>Preencha as credenciais da rede WiFi que este dispositivo deve usar.</p>"
      "<form method='POST' action='/save'>"
      "<label>Nome da rede (SSID)</label>"
      "<input name='ssid' type='text' placeholder='Nome do WiFi' required>"
      "<label>Senha</label>"
      "<input name='pass' type='password' placeholder='Senha (deixe vazio se aberta)'>"
      "<button type='submit'>Salvar e conectar</button>"
      "</form>"
      "<div class='footer'>Wires</div>"
      "</body></html>";
    webServer.send_P(200, "text/html", PAGE);
  });

  webServer.on("/save", HTTP_POST, []() {
    String newSsid = webServer.arg("ssid");
    String newPass = webServer.arg("pass");
    if (newSsid.length() == 0)
    {
      webServer.send(400, "text/plain", "SSID nao pode ser vazio.");
      return;
    }
    webServer.send(200, "text/html",
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<style>body{font-family:sans-serif;max-width:360px;margin:40px auto;padding:0 16px;text-align:center;}</style>"
      "</head><body>"
      "<h2>Configuracao salva!</h2>"
      "<p>Conectando em <strong>" + newSsid + "</strong>...</p>"
      "<p>Reconecte ao seu WiFi normal.</p>"
      "</body></html>");
    delay(600);
    eepromSaveCredentials(newSsid.c_str(), newPass.c_str());
    delay(400);
    ESP.restart();
  });

  webServer.on("/sendwifi", HTTP_GET, []() {
    sendWificfg();
    webServer.send(200, "text/html",
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<style>body{font-family:sans-serif;max-width:360px;margin:40px auto;padding:0 16px;text-align:center;}</style>"
      "</head><body>"
      "<h2>Credenciais enviadas!</h2>"
      "<p>PORTA_A e PORTA_B vao reiniciar automaticamente.</p>"
      "<p><a href='/'>Voltar</a></p>"
      "</body></html>");
  });

  webServer.begin();
  wifiMode = WIFI_MODE_CONFIG;
}

void startFallbackAP()
{
  Serial.println(F("Router down. Starting fallback AP."));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(FALLBACK_AP_SSID, FALLBACK_AP_PASS);
  Serial.print(F("AP: ")); Serial.print(FALLBACK_AP_SSID);
  Serial.print(F("  IP: ")); Serial.println(WiFi.softAPIP());

  webServer.on("/", HTTP_GET, []() {
    unsigned long now = millis();
    bool aOnline = lastStatusPortaA && (now - lastStatusPortaA < 30000UL);
    bool bOnline = lastStatusPortaB && (now - lastStatusPortaB < 30000UL);

    WiFiClient client = webServer.client();
    webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    webServer.send(200, F("text/html"), "");

    client.print(F("<!DOCTYPE html><html><head>"
      "<meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta http-equiv='refresh' content='5'>"
      "<title>Status de Emergencia</title>"
      "<style>"
      "body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 16px;background:#fff3e0;}"
      "h2{color:#e65100;}"
      ".badge{display:inline-block;padding:3px 10px;border-radius:12px;font-size:.85em;font-weight:bold;}"
      ".open{background:#ffcdd2;color:#c62828;}.closed{background:#c8e6c9;color:#2e7d32;}"
      ".online{background:#c8e6c9;color:#2e7d32;}.offline{background:#ffcdd2;color:#c62828;}"
      "table{width:100%;border-collapse:collapse;margin-top:16px;}"
      "td,th{padding:10px;border:1px solid #ffe0b2;font-size:.9em;}"
      "th{background:#fff8e1;}"
      ".note{font-size:.75em;color:#999;margin-top:16px;text-align:center;}"
      "</style></head><body>"
      "<h2>Modo Emergencia</h2>"
      "<p>Roteador offline. Sistema operando na rede local do PORTEIRO.</p>"
      "<table>"
      "<tr><th>Dispositivo</th><th>Porta</th><th>Rede</th></tr>"
      "<tr><td>PORTEIRO</td><td>-</td>"
      "<td><span class='badge online'>AP Ativo</span></td></tr>"));

    client.print(F("<tr><td>PORTA_A</td><td><span class='badge "));
    client.print(portaAAberta ? F("open'>Aberta") : F("closed'>Fechada"));
    client.print(F("</span></td><td><span class='badge "));
    client.print(aOnline ? F("online'>Online") : F("offline'>Sem sinal"));
    client.print(F("</span></td></tr>"));

    client.print(F("<tr><td>PORTA_B</td><td><span class='badge "));
    client.print(portaBAberta ? F("open'>Aberta") : F("closed'>Fechada"));
    client.print(F("</span></td><td><span class='badge "));
    client.print(bOnline ? F("online'>Online") : F("offline'>Sem sinal"));
    client.print(F("</span></td></tr>"));

    client.print(F("</table><p class='note'>Atualiza a cada 5s</p></body></html>"));
  });

  webServer.begin();
  fallbackAtivo = true;
  wifiMode      = WIFI_MODE_FALLBACK;
  localIP       = WiFi.softAPIP();
  udp.stop();
  udp.begin(UDP_PORT);
}

void connectFallbackAsClient()
{
  Serial.println(F("Connecting to PORTEIRO_AP..."));
  WiFi.mode(WIFI_STA);
  WiFi.begin(FALLBACK_AP_SSID, FALLBACK_AP_PASS);

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 10000UL)
  {
    ESP.wdtFeed();
    delay(300);
    Serial.print(F("."));
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println(F("Connected to PORTEIRO_AP."));
    localIP = WiFi.localIP();
    Serial.print(F("IP: ")); Serial.println(localIP);
    udp.stop();
    udp.begin(UDP_PORT);
    fallbackAtivo = true;
    wifiMode      = WIFI_MODE_FALLBACK;
  }
  else
  {
    Serial.println(F("Could not reach PORTEIRO_AP."));
  }
}

static void localIPStr(char *buf)
{
  snprintf(buf, 16, "%u.%u.%u.%u", localIP[0], localIP[1], localIP[2], localIP[3]);
}

static void remoteIPStr(char *buf)
{
  IPAddress rip = udp.remoteIP();
  snprintf(buf, 16, "%u.%u.%u.%u", rip[0], rip[1], rip[2], rip[3]);
}

void sendBroadcast(const char *msg)
{
  char frame[280];
  snprintf(frame, sizeof(frame), "%s|%s", SHARED_KEY, msg);
  uint32_t localIPint = (uint32_t)WiFi.localIP();
  uint32_t subnetInt  = (uint32_t)WiFi.subnetMask();
  IPAddress broadcastIP(localIPint | ~subnetInt);
  udp.beginPacket(broadcastIP, UDP_PORT);
  udp.print(frame);
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16
  udp.endPacket();
  Serial.println("[UDP] " + msg);
}

<<<<<<< HEAD
void sendStatus() {
  String estado = portaAberta ? "OPEN" : "CLOSED";
  sendBroadcast("STATUS|" + String(DEVICE_NAME) + "|" + estado);
}

// ===================== DISPOSITIVOS =========================
void updateDevice(const String& name, const String& ip, bool aberta = false) {
  if (name == DEVICE_NAME) return;
  for (int i = 0; i < 5; i++) {
    if (knownDevices[i].name == name) {
      knownDevices[i].ip       = ip;
      knownDevices[i].lastSeen = millis();
      return;
    }
  }
  for (int i = 0; i < 5; i++) {
    if (knownDevices[i].name == "") {
      knownDevices[i] = { name, ip, millis(), false };
      Serial.println("[DISC] " + name + " -> " + ip);
      break;
    }
  }
  sendBroadcast("CONFIRM|" + String(DEVICE_NAME) + "|" + localIP.toString());
}

void updateDeviceStatus(const String& name, bool aberta) {
  for (int i = 0; i < 5; i++) {
    if (knownDevices[i].name == name) {
      knownDevices[i].portaAberta = aberta;
      knownDevices[i].lastSeen    = millis();
      return;
=======
void sendWificfg()
{
  if (strcmp(DEVICE_NAME, DEV_PORTEIRO) != 0) return;
  if (!eepromHasCredentials()) return;
  char ssid[64], pass[64];
  eepromReadStr(EEPROM_SSID_ADDR, ssid, sizeof(ssid));
  eepromReadStr(EEPROM_PASS_ADDR, pass, sizeof(pass));
  char cfgMsg[160];
  snprintf(cfgMsg, sizeof(cfgMsg), "WIFICFG|%s|%s", ssid, pass);
  udp.beginPacket(IPAddress(192, 168, 4, 255), UDP_PORT);
  udp.print(cfgMsg);
  udp.endPacket();
  sendBroadcast(cfgMsg);
  lastWificfgSent = millis();
  Serial.println(F("WIFICFG sent."));
}

void sendStatus()
{
  char msg[64];
  snprintf(msg, sizeof(msg), "STATUS|%s|%s", DEVICE_NAME, portaAberta ? "OPEN" : "CLOSED");
  sendBroadcast(msg);
  lastStatusSent = millis();
}

bool deviceKnown(const char *dev)
{
  if (strcmp(dev, DEVICE_NAME) == 0) return true;
  for (int i = 0; i < MAX_DEVICES; i++)
    if (!deviceEmpty(i) && deviceMatch(i, dev)) return true;
  return false;
}

bool deviceKnownByIP(const char *ip)
{
  char myIP[16]; localIPStr(myIP);
  if (strcmp(ip, myIP) == 0) return true;
  for (int i = 0; i < MAX_DEVICES; i++)
    if (!deviceEmpty(i) && strcmp(devices[i].ip, ip) == 0) return true;
  return false;
}

void addDevice(const char *dev, const char *ip)
{
  if (!dev || !ip || strcmp(dev, DEVICE_NAME) == 0) return;

  for (int i = 0; i < MAX_DEVICES; i++)
  {
    if (!deviceEmpty(i) && deviceMatch(i, dev))
    {
      strncpy(devices[i].ip, ip, sizeof(devices[i].ip) - 1);
      devices[i].ip[sizeof(devices[i].ip) - 1] = '\0';
      devices[i].lastPing = millis();
      return;
    }
  }

  for (int i = 0; i < MAX_DEVICES; i++)
  {
    if (deviceEmpty(i))
    {
      strncpy(devices[i].name, dev, sizeof(devices[i].name) - 1);
      devices[i].name[sizeof(devices[i].name) - 1] = '\0';
      strncpy(devices[i].ip,   ip,  sizeof(devices[i].ip)   - 1);
      devices[i].ip[sizeof(devices[i].ip) - 1] = '\0';
      devices[i].lastPing = millis();
      Serial.print(F("[DISCOVERY] ")); Serial.print(dev);
      Serial.print(F(" -> ")); Serial.println(ip);
      return;
    }
  }

  // Table full: evict least recently seen to allow legitimate nodes in
  int oldest = 0;
  for (int i = 1; i < MAX_DEVICES; i++)
    if (devices[i].lastPing < devices[oldest].lastPing) oldest = i;

  Serial.print(F("Table full, evicting: ")); Serial.println(devices[oldest].name);
  strncpy(devices[oldest].name, dev, sizeof(devices[oldest].name) - 1);
  devices[oldest].name[sizeof(devices[oldest].name) - 1] = '\0';
  strncpy(devices[oldest].ip,   ip,  sizeof(devices[oldest].ip)   - 1);
  devices[oldest].ip[sizeof(devices[oldest].ip) - 1] = '\0';
  devices[oldest].lastPing = millis();
  Serial.print(F("[DISCOVERY] ")); Serial.print(dev);
  Serial.print(F(" -> ")); Serial.println(ip);
}

void assumirMaster()
{
  Serial.println(F("Assuming master role."));
  masterPriority   = devicePriority;
  strncpy(networkMaster, DEVICE_NAME, sizeof(networkMaster) - 1);
  networkMaster[sizeof(networkMaster) - 1] = '\0';
  isMaster         = true;
  masterLeaseStart = millis();
  lastMasterSeen   = millis();
}

void abdicarMaster(int novaPrio, const char *novoDev)
{
  Serial.println(F("Higher-priority master detected. Stepping down."));
  isMaster       = false;
  masterPriority = novaPrio;
  strncpy(networkMaster, novoDev, sizeof(networkMaster) - 1);
  networkMaster[sizeof(networkMaster) - 1] = '\0';
}

void checkMasterHealth()
{
  if (isMaster) return;
  bool masterConhecido  = (networkMaster[0] != '\0');
  bool leaseExpirou     = masterConhecido && (millis() - masterLeaseStart > MASTER_LEASE_TIME * 2);
  bool masterSilencioso = masterConhecido && (millis() - lastMasterSeen   > MASTER_TIMEOUT);
  if (leaseExpirou || masterSilencioso)
  {
    Serial.print(F("Master lost ("));
    Serial.print(leaseExpirou ? F("lease") : F("timeout"));
    Serial.println(F("). Electing self."));
    assumirMaster();
  }
}

void processHello(const char *dev, int prio, const char *ip, unsigned long lease)
{
  if (isMaster && prio > devicePriority) abdicarMaster(prio, dev);

  if (strcmp(dev, networkMaster) == 0)
  {
    masterLeaseStart = millis();
    lastMasterSeen   = millis();
  }
  if (prio > masterPriority)
  {
    masterPriority = prio;
    strncpy(networkMaster, dev, sizeof(networkMaster) - 1);
    networkMaster[sizeof(networkMaster) - 1] = '\0';
    isMaster       = (strcmp(dev, DEVICE_NAME) == 0);
    lastMasterSeen = millis();
  }
  else if (prio == masterPriority && strcmp(dev, DEVICE_NAME) != 0)
  {
    char localIPBuf[16]; localIPStr(localIPBuf);
    if (strcmp(ip, localIPBuf) > 0)
    {
      strncpy(networkMaster, dev, sizeof(networkMaster) - 1);
      networkMaster[sizeof(networkMaster) - 1] = '\0';
      isMaster       = false;
      lastMasterSeen = millis();
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16
    }
  }
}

<<<<<<< HEAD
// ===================== INTERTRAVAMENTO ======================
bool podeAbrir(const String& portaAlvo) {
  if (bypassMode) { Serial.println("Bypass ativo."); return true; }
  if (portaAlvo == "PORTA_A") {
    if (lastStatusPortaB > 0 && millis() - lastStatusPortaB > 10000) { Serial.println("Sem status B."); return false; }
    if (portaBAberta) { Serial.println("B aberta. Bloqueando A."); return false; }
  } else if (portaAlvo == "PORTA_B") {
    if (lastStatusPortaA > 0 && millis() - lastStatusPortaA > 10000) { Serial.println("Sem status A."); return false; }
    if (portaAAberta) { Serial.println("A aberta. Bloqueando B."); return false; }
=======
// Set when the relay activates, before the sensor registers movement.
// Prevents the other door from opening during the physical travel window.
bool portaARelayAtivo = false;
bool portaBRelayAtivo = false;

bool podeAbrir(const char *portaAlvo, bool masterOverride = false)
{
  if (millis() - lastOpenEvent < 2000UL)
  {
    Serial.println(F("Cooldown active."));
    return false;
  }

  if (bypassMode && !masterOverride)
  {
    // Still block if the other relay is physically moving to avoid mechanical damage
    if (strcmp(portaAlvo, DEV_PORTA_A) == 0 && portaBRelayAtivo)
    {
      Serial.println(F("Bypass: PORTA_B relay active."));
      return false;
    }
    if (strcmp(portaAlvo, DEV_PORTA_B) == 0 && portaARelayAtivo)
    {
      Serial.println(F("Bypass: PORTA_A relay active."));
      return false;
    }
    return true;
  }

  if (masterOverride) return true;

  if (strcmp(portaAlvo, DEV_PORTA_A) == 0)
  {
    if (lastStatusPortaB == 0 || millis() - lastStatusPortaB > 10000UL)
    {
      if (lastStatusPortaB == 0)
        Serial.println(F("PORTA_B: no status ever received. Blocking."));
      else
      {
        Serial.print(F("PORTA_B: no contact for "));
        Serial.print((millis() - lastStatusPortaB) / 1000UL);
        Serial.println(F("s. Blocking."));
      }
      return false;
    }
    if (portaBAberta || portaBLock || portaBRelayAtivo)
    {
      Serial.println(F("PORTA_B busy (open/locked/relay active)."));
      return false;
    }
  }
  else if (strcmp(portaAlvo, DEV_PORTA_B) == 0)
  {
    if (lastStatusPortaA == 0 || millis() - lastStatusPortaA > 10000UL)
    {
      if (lastStatusPortaA == 0)
        Serial.println(F("PORTA_A: no status ever received. Blocking."));
      else
      {
        Serial.print(F("PORTA_A: no contact for "));
        Serial.print((millis() - lastStatusPortaA) / 1000UL);
        Serial.println(F("s. Blocking."));
      }
      return false;
    }
    if (portaAAberta || portaALock || portaARelayAtivo)
    {
      Serial.println(F("PORTA_A busy (open/locked/relay active)."));
      return false;
    }
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16
  }
  return true;
}

<<<<<<< HEAD
void abrirPorta() {
  if (relayAtivo) return;
  if (!podeAbrir(String(DEVICE_NAME))) return;
  Serial.println("Abrindo porta...");
=======
void abrirPorta()
{
  if (relayAtivo) return;
  Serial.println(F("Opening door."));
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(PUPE_PIN,  LOW);
  relayStart  = millis();
  relayAtivo  = true;
  portaAberta = true;
  sendStatus();
}

<<<<<<< HEAD
// ===================== MENSAGENS ============================
void processMessage(const String& msg) {
  if (msg.startsWith("DISCOVERY|") || msg.startsWith("CONFIRM|")) {
    int i1 = msg.indexOf('|') + 1, i2 = msg.indexOf('|', i1);
    updateDevice(msg.substring(i1, i2), msg.substring(i2 + 1));
  }
  else if (msg.startsWith("PING|")) {
    int i1 = msg.indexOf('|') + 1, i2 = msg.indexOf('|', i1);
    updateDevice(msg.substring(i1, i2), msg.substring(i2 + 1));
    sendBroadcast("PONG|" + String(DEVICE_NAME) + "|" + localIP.toString());
  }
  else if (msg.startsWith("PONG|")) {
    int i1 = msg.indexOf('|') + 1, i2 = msg.indexOf('|', i1);
    String dev = msg.substring(i1, i2);
    for (int i = 0; i < 5; i++)
      if (knownDevices[i].name == dev) { knownDevices[i].lastSeen = millis(); break; }
  }
  else if (msg.startsWith("OPEN|")) {
    if (msg.substring(5) == DEVICE_NAME) abrirPorta();
  }
  else if (msg.startsWith("BYPASS|")) {
    bypassMode = msg.endsWith("ON");
    Serial.println(bypassMode ? "Bypass ON." : "Bypass OFF.");
  }
  else if (msg.startsWith("STATUS|")) {
    int i1 = msg.indexOf('|') + 1, i2 = msg.indexOf('|', i1);
    String dev = msg.substring(i1, i2), estado = msg.substring(i2 + 1);
    bool ab = (estado == "OPEN");
    if (dev == "PORTA_A") { portaAAberta = ab; lastStatusPortaA = millis(); }
    if (dev == "PORTA_B") { portaBAberta = ab; lastStatusPortaB = millis(); }
    updateDeviceStatus(dev, ab);
    Serial.println("[STATUS] " + dev + ": " + estado);
  }
}

// ===================== INTERFACE WEB (só PORTEIRO) ==========
void setupWebServer() {

  server.on("/", HTTP_GET, []() {
    unsigned long now = millis();

    // --- Build device rows ---
    String deviceRows = "";
    for (int i = 0; i < 5; i++) {
      if (knownDevices[i].name == "") continue;
      bool online = (now - knownDevices[i].lastSeen < 20000);
      bool ab     = knownDevices[i].portaAberta;
      deviceRows += "<div class='dev-card" + String(online ? "" : " offline") + "'>";
      deviceRows += "<div class='dev-header'>";
      deviceRows += "<span class='dev-name'>" + knownDevices[i].name + "</span>";
      deviceRows += "<span class='status-dot " + String(online ? "dot-online" : "dot-offline") + "'></span>";
      deviceRows += "</div>";
      deviceRows += "<div class='dev-body'>";
      deviceRows += "<div class='info-row'><span class='label'>PORTA</span>";
      deviceRows += "<span class='value " + String(ab ? "val-open" : "val-closed") + "'>";
      deviceRows += String(ab ? "&#9650; ABERTA" : "&#9660; FECHADA") + "</span></div>";
      deviceRows += "<div class='info-row'><span class='label'>REDE</span>";
      deviceRows += "<span class='value'>" + knownDevices[i].ip + "</span></div>";
      if (online) {
        deviceRows += "<form action='/open' method='POST'>"
                      "<input type='hidden' name='porta' value='" + knownDevices[i].name + "'>"
                      "<button class='btn-open' type='submit'>ABRIR</button></form>";
=======
void solicitarAbertura(const char *porta, bool masterOverride = false)
{
  if (isMaster)
  {
    // Lock immediately — a second request arriving before masterBusy is set
    // would pass podeAbrir() concurrently and open both doors.
    if (masterBusy)
    {
      Serial.println(F("Master busy."));
      return;
    }
    masterBusy     = true;
    masterBusyTime = millis();

    if (!podeAbrir(porta, masterOverride))
    {
      masterBusy = false;
      return;
    }

    if (strcmp(porta, DEVICE_NAME) == 0)
    {
      abrirPorta();
      char ackMsg[64];
      snprintf(ackMsg, sizeof(ackMsg), "ACK_OPEN|%s", DEVICE_NAME);
      sendBroadcast(ackMsg);
    }
    else
    {
      Serial.print(F("Sending OPEN to ")); Serial.println(porta);
      if (adquirirToken(porta))
      {
        currentToken = millis();
        strncpy(tokenOwner, DEVICE_NAME, sizeof(tokenOwner) - 1);
        tokenOwner[sizeof(tokenOwner) - 1] = '\0';
        char openMsg[48];
        snprintf(openMsg, sizeof(openMsg), masterOverride ? "OPEN|%s|M" : "OPEN|%s", porta);
        sendBroadcast(openMsg);
      }
      else
      {
        masterBusy = false;
      }
    }
    return;
  }

  if (strcmp(porta, DEVICE_NAME) == 0 && podeAbrir(porta))
  {
    abrirPorta();
    char ackMsg[64];
    snprintf(ackMsg, sizeof(ackMsg), "ACK_OPEN|%s", DEVICE_NAME);
    sendBroadcast(ackMsg);
    return;
  }

  if (networkMaster[0] != '\0')
  {
    openToken = millis();
    char reqMsg[64];
    snprintf(reqMsg, sizeof(reqMsg), "REQ_OPEN|%s|%s|%lu", porta, DEVICE_NAME, openToken);
    sendBroadcast(reqMsg);
    aguardandoAutorizacao = true;
    lastReqSent           = millis();
    reqRetryCount         = 0;
    strncpy(reqPorta, porta, sizeof(reqPorta) - 1);
    reqPorta[sizeof(reqPorta) - 1] = '\0';
  }
  else
    Serial.println(F("No master available."));
}

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
  // Reject any packet without the shared key prefix.
  // WIFICFG is exempt: sent before nodes have credentials or know the key.
  const size_t keyLen = strlen(SHARED_KEY);
  if (strncmp(msg, "WIFICFG|", 8) != 0)
  {
    if (strncmp(msg, SHARED_KEY, keyLen) != 0 || msg[keyLen] != '|')
      return;
    msg += keyLen + 1;
  }

  char  buf[256];
  char *f[8];
  int   n;

  if (strncmp(msg, "DISCOVERY|", 10) == 0 || strncmp(msg, "CONFIRM|", 8) == 0)
  {
    n = splitMsg(buf, sizeof(buf), msg, f, 3);
    if (n >= 3)
    {
      addDevice(f[1], f[2]);
      if (strcmp(DEVICE_NAME, DEV_PORTEIRO) == 0 && strncmp(f[2], "192.168.4.", 10) == 0)
      {
        Serial.print(F("Unconfigured node detected: ")); Serial.println(f[1]);
        lastWificfgSent = 0;
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16
      }
      deviceRows += "</div></div>";
    }
<<<<<<< HEAD

    // --- WiFi section ---
    String wifiSection = "";
    if (WiFi.status() == WL_CONNECTED) {
      wifiSection += "<div class='info-row'><span class='label'>REDE</span><span class='value'>" + WiFi.SSID() + "</span></div>";
      wifiSection += "<div class='info-row'><span class='label'>IP LOCAL</span><span class='value'>" + WiFi.localIP().toString() + "</span></div>";
      wifiSection += "<div class='info-row'><span class='label'>SINAL</span><span class='value'>" + String(WiFi.RSSI()) + " dBm</span></div>";
      wifiSection += "<form action='/wificlear' method='POST'><button class='btn-danger' type='submit'>TROCAR REDE</button></form>";
    } else {
      wifiSection += "<div class='info-row'><span class='label'>STATUS</span><span class='value val-open'>SEM CONEXAO</span></div>";
      wifiSection += "<a href='/wificonfig'><button class='btn-open' style='margin-top:12px'>CONFIGURAR WIFI</button></a>";
    }

    String bypassLabel = bypassMode ? "DESATIVAR BYPASS" : "ATIVAR BYPASS";
    String bypassClass = bypassMode ? "btn-bypass-on" : "btn-bypass";

    String html = "<!DOCTYPE html><html lang='pt-BR'><head>"
      "<meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta http-equiv='refresh' content='4'>"
      "<title>Wires | Porteiro</title>"
      "<link href='https://fonts.googleapis.com/css2?family=Bebas+Neue&family=DM+Mono:wght@400;500&display=swap' rel='stylesheet'>"
      "<style>"
      ":root{"
        "--gold:#F5A623;"
        "--gold-dim:#c4851a;"
        "--bg:#0a0a0a;"
        "--surface:#111111;"
        "--surface2:#1a1a1a;"
        "--border:#2a2a2a;"
        "--text:#e8e8e8;"
        "--muted:#666;"
        "--online:#4CAF50;"
        "--offline:#555;"
        "--open:#F5A623;"
        "--closed:#4a90d9;"
      "}"
      "*{margin:0;padding:0;box-sizing:border-box;}"
      "body{background:var(--bg);color:var(--text);font-family:'DM Mono',monospace;"
        "min-height:100vh;}"
      /* Top bar */
      ".topbar{background:var(--surface);border-bottom:1px solid var(--border);"
        "padding:0 24px;height:56px;display:flex;align-items:center;"
        "justify-content:space-between;position:sticky;top:0;z-index:10;}"
      ".logo{display:flex;align-items:center;gap:12px;}"
      ".logo-mark{font-family:'Bebas Neue',sans-serif;font-size:1.6em;color:var(--gold);"
        "letter-spacing:3px;}"
      ".logo-sub{font-size:.65em;color:var(--muted);letter-spacing:2px;text-transform:uppercase;"
        "margin-top:2px;}"
      ".topbar-right{font-size:.72em;color:var(--muted);text-align:right;line-height:1.6;}"
      ".topbar-right b{color:var(--gold);}"
      /* Layout */
      ".main{padding:24px;max-width:960px;margin:0 auto;}"
      ".section-label{font-family:'Bebas Neue',sans-serif;font-size:1em;letter-spacing:4px;"
        "color:var(--gold);border-bottom:1px solid var(--border);padding-bottom:8px;"
        "margin-bottom:16px;margin-top:28px;}"
      /* Device grid */
      ".dev-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:14px;}"
      ".dev-card{background:var(--surface);border:1px solid var(--border);"
        "border-radius:4px;overflow:hidden;transition:border-color .2s;}"
      ".dev-card:hover{border-color:var(--gold-dim);}"
      ".dev-card.offline{opacity:.5;}"
      ".dev-header{background:var(--surface2);padding:12px 16px;"
        "display:flex;justify-content:space-between;align-items:center;"
        "border-bottom:1px solid var(--border);}"
      ".dev-name{font-family:'Bebas Neue',sans-serif;letter-spacing:2px;font-size:1.05em;"
        "color:var(--text);}"
      ".status-dot{width:8px;height:8px;border-radius:50%;}"
      ".dot-online{background:var(--online);box-shadow:0 0 6px var(--online);}"
      ".dot-offline{background:var(--offline);}"
      ".dev-body{padding:14px 16px;}"
      ".info-row{display:flex;justify-content:space-between;align-items:center;"
        "padding:5px 0;border-bottom:1px solid #1e1e1e;font-size:.78em;}"
      ".info-row:last-of-type{border-bottom:none;}"
      ".label{color:var(--muted);letter-spacing:1px;text-transform:uppercase;font-size:.85em;}"
      ".value{color:var(--text);font-weight:500;}"
      ".val-open{color:var(--open);}"
      ".val-closed{color:var(--closed);}"
      /* Porteiro card */
      ".porteiro-card{background:var(--surface);border:1px solid var(--border);"
        "border-radius:4px;padding:16px;display:flex;flex-wrap:wrap;gap:16px;"
        "align-items:center;justify-content:space-between;}"
      ".porteiro-info{display:flex;flex-direction:column;gap:8px;flex:1;min-width:180px;}"
      ".porteiro-actions{display:flex;gap:10px;flex-wrap:wrap;}"
      /* Buttons */
      ".btn-open,.btn-bypass,.btn-bypass-on,.btn-danger{"
        "padding:9px 18px;border:none;border-radius:3px;cursor:pointer;"
        "font-family:'DM Mono',monospace;font-size:.78em;letter-spacing:1px;"
        "text-transform:uppercase;font-weight:500;transition:all .15s;}"
      ".btn-open{background:var(--gold);color:#000;margin-top:12px;width:100%;}"
      ".btn-open:hover{background:var(--gold-dim);}"
      ".btn-bypass{background:transparent;border:1px solid var(--gold);color:var(--gold);}"
      ".btn-bypass:hover{background:rgba(245,166,35,.1);}"
      ".btn-bypass-on{background:var(--gold);color:#000;}"
      ".btn-bypass-on:hover{background:var(--gold-dim);}"
      ".btn-danger{background:transparent;border:1px solid #c62828;color:#ef5350;margin-top:10px;width:100%;}"
      ".btn-danger:hover{background:rgba(198,40,40,.15);}"
      /* WiFi card */
      ".wifi-card{background:var(--surface);border:1px solid var(--border);"
        "border-radius:4px;padding:16px;max-width:360px;}"
      /* Ticker */
      ".ticker{font-size:.68em;color:var(--muted);margin-top:20px;text-align:center;"
        "letter-spacing:1px;}"
      /* Bypass badge */
      ".bypass-badge{display:inline-block;background:rgba(245,166,35,.15);"
        "border:1px solid var(--gold);color:var(--gold);padding:2px 10px;"
        "border-radius:2px;font-size:.72em;letter-spacing:2px;margin-left:8px;}"
      "</style></head><body>"
      "<div class='topbar'>"
        "<div class='logo'>"
          "<div>"
            "<div class='logo-mark'>WIRES</div>"
            "<div class='logo-sub'>Access Control</div>"
          "</div>"
        "</div>"
        "<div class='topbar-right'>"
          "<div>AP &nbsp;<b>192.168.4.1</b></div>";

    if (WiFi.status() == WL_CONNECTED)
      html += "<div>LAN &nbsp;<b>" + WiFi.localIP().toString() + "</b></div>";

    html += "</div></div>"
      "<div class='main'>"
      "<div class='section-label'>PORTEIRO</div>"
      "<div class='porteiro-card'>"
        "<div class='porteiro-info'>"
          "<div class='info-row'><span class='label'>STATUS</span>"
            "<span class='value' style='display:flex;align-items:center;gap:8px;'>"
              "<span class='status-dot dot-online'></span> ONLINE";

    if (bypassMode) html += "<span class='bypass-badge'>BYPASS</span>";

    html += "</span></div>"
          "<div class='info-row'><span class='label'>AP</span>"
            "<span class='value'>" + String(AP_SSID) + "</span></div>";

    if (WiFi.status() == WL_CONNECTED)
      html += "<div class='info-row'><span class='label'>REDE LOCAL</span>"
              "<span class='value'>" + WiFi.SSID() + "</span></div>";

    html += "</div>"
        "<div class='porteiro-actions'>"
          "<form action='/bypass' method='POST'>"
            "<button class='" + bypassClass + "' type='submit'>" + bypassLabel + "</button>"
          "</form>"
        "</div>"
      "</div>"
      "<div class='section-label'>PORTAS</div>"
      "<div class='dev-grid'>" + deviceRows + "</div>"
      "<div class='section-label'>WIFI LOCAL</div>"
      "<div class='wifi-card'>" + wifiSection + "</div>"
      "<div class='ticker'>&#x25B6; WIRES ACCESS CONTROL &nbsp;&nbsp; ATUALIZA A CADA 4S &nbsp;&nbsp; &#x25B6;</div>"
      "</div></body></html>";

    server.send(200, "text/html", html);
  });

  server.on("/bypass", HTTP_POST, []() {
    bypassMode = !bypassMode;
    sendBroadcast("BYPASS|" + String(bypassMode ? "ON" : "OFF"));
    server.sendHeader("Location", "/"); server.send(303);
  });

  server.on("/open", HTTP_POST, []() {
    String porta = server.arg("porta");
    if (podeAbrir(porta)) sendBroadcast("OPEN|" + porta);
    server.sendHeader("Location", "/"); server.send(303);
  });

  server.on("/wificonfig", HTTP_GET, []() {
    server.send(200, "text/html", F(
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<link href='https://fonts.googleapis.com/css2?family=Bebas+Neue&family=DM+Mono:wght@400;500&display=swap' rel='stylesheet'>"
      "<title>Wires | Config WiFi</title>"
      "<style>"
      "*{margin:0;padding:0;box-sizing:border-box;}"
      "body{background:#0a0a0a;color:#e8e8e8;font-family:'DM Mono',monospace;"
        "display:flex;align-items:center;justify-content:center;min-height:100vh;padding:20px;}"
      ".card{background:#111;border:1px solid #2a2a2a;border-radius:4px;padding:32px;width:100%;max-width:360px;}"
      "h2{font-family:'Bebas Neue',sans-serif;color:#F5A623;letter-spacing:4px;font-size:1.6em;margin-bottom:4px;}"
      "p{color:#666;font-size:.78em;margin-bottom:24px;line-height:1.6;}"
      "label{display:block;font-size:.72em;letter-spacing:2px;color:#666;text-transform:uppercase;margin-bottom:6px;margin-top:16px;}"
      "input{width:100%;background:#1a1a1a;border:1px solid #2a2a2a;color:#e8e8e8;"
        "padding:10px 12px;border-radius:3px;font-family:'DM Mono',monospace;font-size:.9em;outline:none;}"
      "input:focus{border-color:#F5A623;}"
      "button{margin-top:24px;width:100%;padding:12px;background:#F5A623;color:#000;"
        "border:none;border-radius:3px;font-family:'DM Mono',monospace;font-size:.85em;"
        "letter-spacing:2px;text-transform:uppercase;font-weight:500;cursor:pointer;}"
      ".back{display:block;text-align:center;margin-top:14px;color:#555;font-size:.75em;text-decoration:none;}"
      ".back:hover{color:#F5A623;}"
      "</style></head><body>"
      "<div class='card'>"
        "<h2>WIRES</h2>"
        "<p>Configure a rede local. O AP PORTEIRO_AP continua ativo para as portas.</p>"
        "<form method='POST' action='/wifisave'>"
          "<label>SSID</label><input name='ssid' type='text' autocomplete='off' required>"
          "<label>SENHA</label><input name='pass' type='password'>"
          "<button type='submit'>SALVAR E CONECTAR</button>"
        "</form>"
        "<a class='back' href='/'>&#8592; Voltar</a>"
      "</div></body></html>"
    ));
  });

  server.on("/wifisave", HTTP_POST, []() {
    String ss = server.arg("ssid"), ps = server.arg("pass");
    if (!ss.length()) { server.send(400, "text/plain", "SSID vazio."); return; }
    server.send(200, "text/html", F(
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<link href='https://fonts.googleapis.com/css2?family=Bebas+Neue&family=DM+Mono:wght@400&display=swap' rel='stylesheet'>"
      "<style>body{background:#0a0a0a;color:#e8e8e8;font-family:'DM Mono',monospace;"
        "display:flex;align-items:center;justify-content:center;min-height:100vh;}"
      ".card{text-align:center;padding:40px;}"
      "h2{font-family:'Bebas Neue',sans-serif;color:#F5A623;letter-spacing:4px;font-size:2em;}"
      "p{color:#666;font-size:.82em;margin-top:12px;line-height:1.8;}"
      "</style></head><body>"
      "<div class='card'><h2>CONECTANDO</h2>"
        "<p>Credenciais salvas.<br>Aguarde e acesse pelo IP do roteador.</p>"
      "</div></body></html>"
    ));
    delay(800);
    nvSave(ss.c_str(), ps.c_str());
    WiFi.begin(ss.c_str(), ps.c_str());
  });

  server.on("/wificlear", HTTP_POST, []() {
    server.send(200, "text/html", F(
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<link href='https://fonts.googleapis.com/css2?family=Bebas+Neue&display=swap' rel='stylesheet'>"
      "<style>body{background:#0a0a0a;color:#e8e8e8;font-family:monospace;"
        "display:flex;align-items:center;justify-content:center;min-height:100vh;}"
      "h2{font-family:'Bebas Neue',sans-serif;color:#F5A623;letter-spacing:4px;font-size:2em;text-align:center;}"
      "</style></head><body><h2>REINICIANDO...</h2></body></html>"
    ));
    delay(600); nvClear(); ESP.restart();
  });

  server.begin();
  Serial.println("Web server ativo.");
}

// ===================== SETUP ================================
void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
=======
  }
  else if (strncmp(msg, "WIFICFG|", 8) == 0)
  {
    // Only accept if unconfigured and originating from the PORTEIRO AP subnet
    if (!eepromHasCredentials())
    {
      char remoteIP[16]; remoteIPStr(remoteIP);
      bool origemValida = (strncmp(remoteIP, "192.168.4.", 10) == 0) ||
                          (wifiMode == WIFI_MODE_FALLBACK);
      if (!origemValida)
      {
        Serial.print(F("WIFICFG rejected from ")); Serial.println(remoteIP);
        return;
      }
      n = splitMsg(buf, sizeof(buf), msg, f, 3);
      if (n >= 2 && strlen(f[1]) > 0)
      {
        Serial.print(F("WIFICFG received. SSID: ")); Serial.println(f[1]);
        eepromSaveCredentials(f[1], n >= 3 ? f[2] : "");
        delay(300);
        ESP.restart();
      }
    }
  }
  else if (strncmp(msg, "PING|", 5) == 0)
  {
    n = splitMsg(buf, sizeof(buf), msg, f, 3);
    if (n >= 3)
    {
      addDevice(f[1], f[2]);
      char pongMsg[80];
      char localIPBuf[16]; localIPStr(localIPBuf);
      snprintf(pongMsg, sizeof(pongMsg), "%s|PONG|%s|%s", SHARED_KEY, DEVICE_NAME, localIPBuf);
      udp.beginPacket(udp.remoteIP(), UDP_PORT);
      udp.print(pongMsg);
      udp.endPacket();
    }
  }
  else if (strncmp(msg, "HELLO|", 6) == 0)
  {
    n = splitMsg(buf, sizeof(buf), msg, f, 6);
    if (n < 6) return;
    addDevice(f[1], f[3]);
    processHello(f[1], atoi(f[2]), f[3], (unsigned long)atol(f[5]));
  }
  else if (strncmp(msg, "PONG|", 5) == 0)
  {
    n = splitMsg(buf, sizeof(buf), msg, f, 3);
    if (n < 2) return;
    for (int i = 0; i < MAX_DEVICES; i++)
      if (!deviceEmpty(i) && deviceMatch(i, f[1])) { devices[i].lastPing = millis(); break; }
  }
  else if (strncmp(msg, "REQ_OPEN|", 9) == 0)
  {
    if (!isMaster) return;
    n = splitMsg(buf, sizeof(buf), msg, f, 4);
    if (n < 4) return;
    if (!deviceKnown(f[2])) return;
    Serial.print(F("REQ_OPEN: ")); Serial.print(f[1]); Serial.print(F(" from ")); Serial.println(f[2]);

    // Lock before validation: two concurrent REQ_OPENs must not both pass podeAbrir()
    if (masterBusy)
    {
      char denyMsg[64]; snprintf(denyMsg, sizeof(denyMsg), "DENY|%s", f[1]); sendBroadcast(denyMsg); return;
    }
    masterBusy     = true;
    masterBusyTime = millis();

    if (!podeAbrir(f[1]))
    {
      masterBusy = false;
      char denyMsg[64]; snprintf(denyMsg, sizeof(denyMsg), "DENY|%s", f[1]); sendBroadcast(denyMsg); return;
    }
    if (adquirirToken(f[1]))
    {
      currentToken   = (unsigned long)atol(f[3]);
      strncpy(tokenOwner, f[2], sizeof(tokenOwner) - 1); tokenOwner[sizeof(tokenOwner)-1] = '\0';
      char allowMsg[64]; snprintf(allowMsg, sizeof(allowMsg), "ALLOW|%s|%lu", f[1], currentToken); sendBroadcast(allowMsg);
    }
    else
    {
      masterBusy = false;
      char denyMsg[64]; snprintf(denyMsg, sizeof(denyMsg), "DENY|%s", f[1]); sendBroadcast(denyMsg);
    }
  }
  else if (strncmp(msg, "ALLOW|", 6) == 0)
  {
    n = splitMsg(buf, sizeof(buf), msg, f, 3);
    if (n < 3) return;
    if (strcmp(f[1], DEVICE_NAME) == 0 && (unsigned long)atol(f[2]) == openToken)
    {
      Serial.println(F("Authorized."));
      lastOpenEvent = millis(); abrirPorta();
      char ackMsg[64]; snprintf(ackMsg, sizeof(ackMsg), "ACK_OPEN|%s", DEVICE_NAME); sendBroadcast(ackMsg);
      aguardandoAutorizacao = false; reqRetryCount = 0; reqPorta[0] = '\0';
    }
  }
  else if (strncmp(msg, "DENY|", 5) == 0)
  {
    if (strcmp(msg + 5, DEVICE_NAME) == 0)
    {
      Serial.println(F("Open denied."));
      aguardandoAutorizacao = false; reqRetryCount = 0; reqPorta[0] = '\0';
    }
  }
  else if (strncmp(msg, "ACK_OPEN|", 9) == 0)
  {
    Serial.print(F("ACK_OPEN from ")); Serial.println(msg + 9);
    if (isMaster) { masterBusy = false; liberarToken(); }
  }
  else if (strncmp(msg, "OPEN|", 5) == 0)
  {
    char remoteIP[16]; remoteIPStr(remoteIP);
    if (!deviceKnownByIP(remoteIP)) return;
    char buf2[32];
    strncpy(buf2, msg + 5, sizeof(buf2) - 1);
    buf2[sizeof(buf2) - 1] = '\0';
    char *target  = strtok(buf2, "|");
    char *flagStr = strtok(NULL, "|");
    bool override = (flagStr && strcmp(flagStr, "M") == 0);
    if (target && strcmp(target, DEVICE_NAME) == 0)
    {
      if (override || podeAbrir(DEVICE_NAME))
      {
        abrirPorta();
        char ackMsg[64];
        snprintf(ackMsg, sizeof(ackMsg), "ACK_OPEN|%s", DEVICE_NAME);
        sendBroadcast(ackMsg);
      }
    }
  }
  else if (strncmp(msg, "LOCK|", 5) == 0)
  {
    char remoteIP[16]; remoteIPStr(remoteIP);
    if (!deviceKnownByIP(remoteIP)) return;
    const char *dev = msg + 5;
    if (strcmp(dev, DEV_PORTA_A) == 0) { portaALock = true;  portaARelayAtivo = true; }
    if (strcmp(dev, DEV_PORTA_B) == 0) { portaBLock = true;  portaBRelayAtivo = true; }
    Serial.print(F("LOCK from ")); Serial.println(dev);
  }
  else if (strncmp(msg, "UNLOCK|", 7) == 0)
  {
    char remoteIP[16]; remoteIPStr(remoteIP);
    if (!deviceKnownByIP(remoteIP)) return;
    const char *dev = msg + 7;
    if (strcmp(dev, DEV_PORTA_A) == 0) { portaALock = false; portaARelayAtivo = false; }
    if (strcmp(dev, DEV_PORTA_B) == 0) { portaBLock = false; portaBRelayAtivo = false; }
    Serial.print(F("UNLOCK from ")); Serial.println(dev);
  }
  else if (strncmp(msg, "BYPASS|", 7) == 0)
  {
    char remoteIP[16]; remoteIPStr(remoteIP);
    if (!deviceKnownByIP(remoteIP)) return;
    bypassMode = (strcmp(msg + 7, "ON") == 0);
    Serial.println(bypassMode ? F("Bypass ON.") : F("Bypass OFF."));
  }
  else if (strncmp(msg, "STATUS|", 7) == 0)
  {
    char remoteIP[16]; remoteIPStr(remoteIP);
    if (!deviceKnownByIP(remoteIP)) return;
    n = splitMsg(buf, sizeof(buf), msg, f, 3);
    if (n < 3) return;
    if (strcmp(f[1], DEV_PORTA_A) == 0)
    {
      bool ant = portaAAberta;
      portaAAberta = (strcmp(f[2], "OPEN") == 0);
      portaALock   = portaAAberta && strcmp(DEVICE_NAME, DEV_PORTA_A) != 0;
      lastStatusPortaA = millis();
      // STATUS counts as a keepalive to avoid false "inactive" removal
      for (int i = 0; i < MAX_DEVICES; i++)
        if (!deviceEmpty(i) && deviceMatch(i, f[1])) { devices[i].lastPing = millis(); break; }
      Serial.print(F("[STATUS] PORTA_A: ")); Serial.print(f[2]);
      Serial.println(ant != portaAAberta ? F(" (changed)") : F(""));
    }
    else if (strcmp(f[1], DEV_PORTA_B) == 0)
    {
      bool ant = portaBAberta;
      portaBAberta = (strcmp(f[2], "OPEN") == 0);
      portaBLock   = portaBAberta && strcmp(DEVICE_NAME, DEV_PORTA_B) != 0;
      lastStatusPortaB = millis();
      for (int i = 0; i < MAX_DEVICES; i++)
        if (!deviceEmpty(i) && deviceMatch(i, f[1])) { devices[i].lastPing = millis(); break; }
      Serial.print(F("[STATUS] PORTA_B: ")); Serial.print(f[2]);
      Serial.println(ant != portaBAberta ? F(" (changed)") : F(""));
    }
  }
  else if (strncmp(msg, "ALERT|", 6) == 0)
  {
    char remoteIP[16]; remoteIPStr(remoteIP);
    if (!deviceKnownByIP(remoteIP)) return;
    n = splitMsg(buf, sizeof(buf), msg, f, 3);
    if (n >= 3)
    {
      Serial.print(F("ALERT: ")); Serial.print(f[1]);
      Serial.print(F(" from ")); Serial.println(f[2]);
    }
  }
}

void setup()
{
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  ESP.wdtEnable(8000);
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16

  pinMode(BTN1_PIN,   INPUT_PULLUP);
  pinMode(BTN2_PIN,   INPUT_PULLUP);
  pinMode(BYPASS_PIN, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN,  OUTPUT);
  pinMode(PUPE_PIN,   OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(PUPE_PIN,  HIGH);

<<<<<<< HEAD
  bool isPorteiro = (String(DEVICE_NAME) == "PORTEIRO");
  isMaster = isPorteiro;
=======
  // GPIO0 boot guard: if bypass switch is closed at power-on the chip would
  // enter flash mode. We detect it and warn; bypass input is ignored for 2s.
  delay(200);
  if (strcmp(DEVICE_NAME, DEV_PORTEIRO) == 0 && digitalRead(BYPASS_PIN) == LOW)
  {
    Serial.println(F("WARNING: BYPASS_PIN (GPIO0) is LOW at boot."));
    Serial.println(F("Add a 10k pull-up resistor between D3 and 3.3V."));
  }
  bypassBootTime = millis();
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16

  if (isPorteiro) {
    // Sempre sobe o AP com canal fixo 1
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.softAPConfig(
      IPAddress(192,168,4,1),
      IPAddress(192,168,4,1),
      IPAddress(255,255,255,0)
    );
    WiFi.softAP(AP_SSID, AP_PASS, 1); // canal 1 fixo
    Serial.println("AP: " + String(AP_SSID) + " | IP: 192.168.4.1");

    localIP = WiFi.softAPIP();

<<<<<<< HEAD
    // Inicia web server ANTES de tentar roteador — sempre disponível em 192.168.4.1
    setupWebServer();

    // Se tem WiFi local salvo, conecta em background
    if (nvHasCred()) {
      char ss[64], ps[64];
      nvRead(EEPROM_SSID_ADDR, ss, 64);
      nvRead(EEPROM_PASS_ADDR, ps, 64);
      Serial.println("Conectando ao roteador: " + String(ss));
      WiFi.setAutoReconnect(true);
      WiFi.begin(ss, ps);
      unsigned long t = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t < 12000) {
        delay(200); Serial.print(".");
      }
      Serial.println();
      if (WiFi.status() == WL_CONNECTED)
        Serial.println("Roteador OK. IP: " + WiFi.localIP().toString());
      else
        Serial.println("Roteador indisponivel. Seguindo so com AP.");
=======
  if (!eepromHasCredentials())
  {
    if (strcmp(DEVICE_NAME, DEV_PORTEIRO) == 0)
    {
      startConfigPortal();
      return;
    }
    else
    {
      Serial.println(F("No credentials. Waiting for WIFICFG from PORTEIRO..."));
      connectFallbackAsClient();
      if (!fallbackAtivo)
      {
        unsigned long tEspera = millis();
        while (!fallbackAtivo)
        {
          ESP.wdtFeed();
          delay(3000);
          connectFallbackAsClient();
          if (millis() - tEspera > 120000UL)
          {
            Serial.println(F("PORTEIRO not found after 2min. Restarting."));
            ESP.restart();
          }
        }
      }
      udp.begin(UDP_PORT);
      unsigned long tCfg = millis();
      while (millis() - tCfg < 300000UL)
      {
        ESP.wdtFeed();
        int pkt = udp.parsePacket();
        if (pkt)
        {
          char rxBuf[128];
          int len = udp.read(rxBuf, 127);
          if (len > 0)
          {
            rxBuf[len] = '\0';
            if (strncmp(rxBuf, "WIFICFG|", 8) == 0)
            {
              char tmp[128];
              strncpy(tmp, rxBuf, sizeof(tmp) - 1);
              tmp[sizeof(tmp) - 1] = '\0';
              char *f0   = strtok(tmp, "|");
              char *ssid = strtok(NULL, "|");
              char *pass = strtok(NULL, "|");
              if (ssid)
              {
                Serial.print(F("WIFICFG received. SSID: ")); Serial.println(ssid);
                eepromSaveCredentials(ssid, pass ? pass : "");
                delay(300);
                ESP.restart();
              }
            }
          }
        }
        delay(50);
      }
      Serial.println(F("WIFICFG timeout. Restarting."));
      ESP.restart();
      return;
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16
    }

  } else {
    // PORTA_A / PORTA_B: conecta no AP do PORTEIRO
    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.begin(AP_SSID, AP_PASS);
    Serial.print("Conectando ao PORTEIRO_AP");
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nConectado! IP: " + WiFi.localIP().toString());
    localIP = WiFi.localIP();
  }

<<<<<<< HEAD
  Serial.println("Device: " + String(DEVICE_NAME));
  udp.begin(UDP_PORT);
  sendBroadcast("DISCOVERY|" + String(DEVICE_NAME) + "|" + localIP.toString());
}

// ===================== LOOP =================================
void loop() {
  // Web server (só PORTEIRO)
  if (isMaster) server.handleClient();

  // Reconexão das portas ao PORTEIRO_AP se cair
  if (!isMaster && WiFi.status() != WL_CONNECTED) {
    static unsigned long lastRC = 0;
    if (millis() - lastRC > 10000) {
      lastRC = millis();
      Serial.println("Reconectando ao PORTEIRO_AP...");
      WiFi.begin(AP_SSID, AP_PASS);
    }
  }

  // Recebe UDP
  int pkt = udp.parsePacket();
  if (pkt) {
    char buf[255]; int len = udp.read(buf, 254);
    if (len > 0) { buf[len] = '\0'; processMessage(String(buf)); }
  }

  // Discovery a cada 5s
  if (millis() - lastDiscovery > 5000) {
    sendBroadcast("DISCOVERY|" + String(DEVICE_NAME) + "|" + localIP.toString());
    lastDiscovery = millis();
  }

  // Ping a cada 10s
  if (millis() - lastPingSent > 10000) {
    sendBroadcast("PING|" + String(DEVICE_NAME) + "|" + localIP.toString());
    lastPingSent = millis();
  }

  // Status a cada 3s
  if (millis() - lastStatusSent > 3000) {
    sendStatus();
    lastStatusSent = millis();
  }

  // Remove inativos
  for (int i = 0; i < 5; i++) {
    if (knownDevices[i].name != "" && millis() - knownDevices[i].lastSeen > 30000) {
      Serial.println("Inativo: " + knownDevices[i].name);
      knownDevices[i] = { "", "", 0, false };
    }
  }

  // ---- Botões ----
  if (isMaster) {
    static bool lb1 = HIGH; static unsigned long lp1 = 0;
    bool b1 = digitalRead(BTN1_PIN);
    if (b1 == LOW && lb1 == HIGH && millis() - lp1 > 300) {
      lp1 = millis(); Serial.println("BTN1-A");
      if (podeAbrir("PORTA_A")) sendBroadcast("OPEN|PORTA_A");
      else Serial.println("Bloqueado.");
=======
  char savedSsid[64], savedPass[64];
  eepromReadStr(EEPROM_SSID_ADDR, savedSsid, sizeof(savedSsid));
  eepromReadStr(EEPROM_PASS_ADDR, savedPass, sizeof(savedPass));

  Serial.print(F("Connecting to ")); Serial.println(savedSsid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid, savedPass);

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_CONNECT_TIMEOUT)
  {
    ESP.wdtFeed();
    delay(400);
    Serial.print(F("."));
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    localIP  = WiFi.localIP();
    wifiMode = WIFI_MODE_NORMAL;
    Serial.println(F("WiFi connected."));
    Serial.print(F("IP: ")); Serial.println(localIP);
  }
  else
  {
    Serial.println(F("Router unreachable. Starting fallback."));
    if (strcmp(DEVICE_NAME, DEV_PORTEIRO) == 0) startFallbackAP();
    else                                          connectFallbackAsClient();
  }

  udp.begin(UDP_PORT);
  portaAberta   = (digitalRead(SENSOR_PIN) == SENSOR_OPEN);
  sensorLeitura = portaAberta;
  assumirMaster();

  char msg[64];
  char localIPBuf[16]; localIPStr(localIPBuf);
  snprintf(msg, sizeof(msg), "DISCOVERY|%s|%s", DEVICE_NAME, localIPBuf);
  sendBroadcast(msg);
  sendStatus();

  Serial.print(F("Device: ")); Serial.println(DEVICE_NAME);
  Serial.print(F("WiFi mode: "));
  Serial.println(wifiMode == WIFI_MODE_NORMAL ? F("NORMAL") : F("FALLBACK"));
}

void loop()
{
  ESP.wdtFeed();

  // Logic watchdog: detects a stalled loop that the hardware watchdog would miss
  if (lastLoopWatchdog != 0 && millis() - lastLoopWatchdog > APP_WATCHDOG_TIMEOUT)
  {
    Serial.println(F("Logic watchdog triggered. Restarting."));
    delay(100);
    ESP.restart();
  }
  lastLoopWatchdog = millis();

  if (wifiMode == WIFI_MODE_CONFIG)
  {
    webServer.handleClient();
    return;
  }

  unsigned long now = millis();

  bool leitura = (digitalRead(SENSOR_PIN) == SENSOR_OPEN);
  if (leitura != sensorLeitura && now - sensorDebounce > 100UL)
  {
    sensorDebounce = now;
    sensorLeitura  = leitura;
  }

  if (fallbackAtivo) webServer.handleClient();

  if (wifiMode == WIFI_MODE_NORMAL)
  {
    static unsigned long lastReconnect = 0;

    if (WiFi.status() != WL_CONNECTED)
    {
      if (wifiDownSince == 0) wifiDownSince = now;

      if (now - lastReconnect > 5000UL)
      {
        char savedSsid[64], savedPass[64];
        eepromReadStr(EEPROM_SSID_ADDR, savedSsid, sizeof(savedSsid));
        eepromReadStr(EEPROM_PASS_ADDR, savedPass, sizeof(savedPass));
        Serial.println(F("Reconnecting WiFi..."));
        WiFi.disconnect();
        WiFi.begin(savedSsid, savedPass);
        lastReconnect = now;
      }

      if (now - wifiDownSince > WIFI_RECONNECT_TIMEOUT)
      {
        Serial.println(F("Router timeout. Activating fallback."));
        udp.stop();
        if (strcmp(DEVICE_NAME, DEV_PORTEIRO) == 0) startFallbackAP();
        else                                          connectFallbackAsClient();
        wifiDownSince = 0;
      }
    }
    else
    {
      if (wifiDownSince != 0)
      {
        Serial.println(F("WiFi restored."));
        localIP       = WiFi.localIP();
        wifiDownSince = 0;
        udp.stop();
        udp.begin(UDP_PORT);
        sendStatus();
      }
    }
  }

  if (WiFi.status() != WL_CONNECTED && !fallbackAtivo) return;

  int packetSize = udp.parsePacket();
  if (packetSize)
  {
    char buffer[256];
    int len = udp.read(buffer, sizeof(buffer) - 1);
    if (len > 0) { buffer[len] = '\0'; processMessage(buffer); }
  }

  if (now - lastDiscovery > 15000UL)
  {
    char localIPBuf[16]; localIPStr(localIPBuf);
    char msg[64];
    snprintf(msg, sizeof(msg), "DISCOVERY|%s|%s", DEVICE_NAME, localIPBuf);
    sendBroadcast(msg);
    char helloMsg[128];
    snprintf(helloMsg, sizeof(helloMsg), "HELLO|%s|%d|%s|%s|%lu",
             DEVICE_NAME, devicePriority, localIPBuf,
             isMaster ? "MASTER" : "NODE", MASTER_LEASE_TIME);
    sendBroadcast(helloMsg);
    lastDiscovery = now;
  }

  if (strcmp(DEVICE_NAME, DEV_PORTEIRO) == 0 && eepromHasCredentials())
  {
    if (lastWificfgSent == 0 || now - lastWificfgSent > WIFICFG_INTERVAL)
    {
      bool temSemConfig = false;
      for (int i = 0; i < MAX_DEVICES; i++)
      {
        if (!deviceEmpty(i) && strncmp(devices[i].ip, "192.168.4.", 10) == 0)
        {
          temSemConfig = true;
          break;
        }
      }
      if (temSemConfig || lastWificfgSent == 0)
        sendWificfg();
      else
        lastWificfgSent = now;
    }
  }

  if (now - lastPingSent > 15000UL)
  {
    char localIPBuf[16]; localIPStr(localIPBuf);
    char pingMsg[64];
    snprintf(pingMsg, sizeof(pingMsg), "PING|%s|%s", DEVICE_NAME, localIPBuf);
    sendBroadcast(pingMsg);
    lastPingSent = now;
  }

  if (now - lastStatusSent > 15000UL)
  {
    sendStatus();
    if (lastStatusPortaA != 0 && now - lastStatusPortaA > 20000UL) { portaAAberta = false; portaALock = false; }
    if (lastStatusPortaB != 0 && now - lastStatusPortaB > 20000UL) { portaBAberta = false; portaBLock = false; }
    Serial.print(F("My door: "));  Serial.println(portaAberta  ? F("OPEN") : F("CLOSED"));
    Serial.print(F("PORTA_A: ")); Serial.print(portaAAberta   ? F("OPEN") : F("CLOSED"));
    Serial.print(F(" (")); Serial.print(lastStatusPortaA ? now - lastStatusPortaA : 0UL); Serial.println(F("ms)"));
    Serial.print(F("PORTA_B: ")); Serial.print(portaBAberta   ? F("OPEN") : F("CLOSED"));
    Serial.print(F(" (")); Serial.print(lastStatusPortaB ? now - lastStatusPortaB : 0UL); Serial.println(F("ms)"));
    Serial.print(F("Master: "));  Serial.print(networkMaster); Serial.println(isMaster ? F(" (me)") : F(""));
    Serial.println(F("---"));
  }

  for (int i = 0; i < MAX_DEVICES; i++)
  {
    if (!deviceEmpty(i) && now - devices[i].lastPing > 45000UL)
    {
      Serial.print(F("Inactive: ")); Serial.println(devices[i].name);
      if (strcmp(devices[i].name, networkMaster) == 0)
      {
        networkMaster[0] = '\0'; masterPriority = 0; isMaster = false;
      }
      deviceClear(i);
    }
  }

  if (strcmp(DEVICE_NAME, DEV_PORTEIRO) == 0)
  {
    static bool lastBtn1 = HIGH; static unsigned long lastBtn1Press = 0;
    bool btn1 = digitalRead(BTN1_PIN);
    if (btn1 == LOW && lastBtn1 == HIGH && now - lastBtn1Press > 300UL)
    {
      lastBtn1Press = now;
      if (podeAbrir(DEV_PORTA_A, true)) solicitarAbertura(DEV_PORTA_A, true);
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16
    }
    lb1 = b1;

<<<<<<< HEAD
    static bool lb2 = HIGH; static unsigned long lp2 = 0;
    bool b2 = digitalRead(BTN2_PIN);
    if (b2 == LOW && lb2 == HIGH && millis() - lp2 > 300) {
      lp2 = millis(); Serial.println("BTN2-B");
      if (podeAbrir("PORTA_B")) sendBroadcast("OPEN|PORTA_B");
      else Serial.println("Bloqueado.");
=======
    static bool lastBtn2 = HIGH; static unsigned long lastBtn2Press = 0;
    bool btn2 = digitalRead(BTN2_PIN);
    if (btn2 == LOW && lastBtn2 == HIGH && now - lastBtn2Press > 300UL)
    {
      lastBtn2Press = now;
      if (podeAbrir(DEV_PORTA_B, true)) solicitarAbertura(DEV_PORTA_B, true);
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16
    }
    lb2 = b2;

<<<<<<< HEAD
    bool bp = (digitalRead(BYPASS_PIN) == LOW);
    static bool lbp = false;
    if (bp != lbp) {
      bypassMode = bp;
      sendBroadcast("BYPASS|" + String(bp ? "ON" : "OFF"));
      lbp = bp;
      Serial.println("Bypass: " + String(bp ? "ON" : "OFF"));
    }
  } else {
    static bool lb = HIGH; static unsigned long lp = 0;
    bool b = digitalRead(BTN1_PIN);
    if (b == LOW && lb == HIGH && millis() - lp > 300) {
      lp = millis(); abrirPorta();
=======
    if (!bypassBootOk && millis() - bypassBootTime > 2000UL)
      bypassBootOk = true;

    if (bypassBootOk)
    {
      bool bypassState = (digitalRead(BYPASS_PIN) == LOW);
      static bool lastBypass = false;
      if (bypassState != lastBypass)
      {
        char bMsg[16]; snprintf(bMsg, sizeof(bMsg), "BYPASS|%s", bypassState ? "ON" : "OFF");
        sendBroadcast(bMsg); lastBypass = bypassState;
        Serial.print(F("Bypass: ")); Serial.println(bypassState ? F("ON") : F("OFF"));
      }
    }
  }
  else
  {
    static bool lastBtn = HIGH; static unsigned long lastPress = 0;
    bool btnState = digitalRead(BTN1_PIN);
    if (btnState == LOW && lastBtn == HIGH && now - lastPress > 300UL)
    {
      lastPress = now;
      solicitarAbertura(DEVICE_NAME);
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16
    }
    lb = b;
  }

<<<<<<< HEAD
  // Sensor (HIGH = aberta)
  bool novoEstado = (digitalRead(SENSOR_PIN) == HIGH);
  if (novoEstado != portaAberta && !relayAtivo) {
    portaAberta = novoEstado;
    sendStatus();
    Serial.println(portaAberta ? "Sensor: ABERTA" : "Sensor: FECHADA");
  }

  // Relé timeout
  if (relayAtivo && millis() - relayStart >= RELAY_TIME) {
    digitalWrite(RELAY_PIN, HIGH); digitalWrite(PUPE_PIN, HIGH);
    relayAtivo = false; Serial.println("Rele OFF.");
    bool sa = (digitalRead(SENSOR_PIN) == HIGH);
    if (portaAberta != sa) { portaAberta = sa; sendStatus(); }
  }
=======
  if (sensorLeitura != portaAberta)
  {
    portaAberta = sensorLeitura;
    if (portaAberta)
    {
      portaAbertaTempo = now; alertSent = false;
      char lockMsg[32]; snprintf(lockMsg, sizeof(lockMsg), "LOCK|%s", DEVICE_NAME); sendBroadcast(lockMsg);
      if      (strcmp(DEVICE_NAME, DEV_PORTA_A) == 0) { portaAAberta=true;  portaALock=true;  lastStatusPortaA=now; }
      else if (strcmp(DEVICE_NAME, DEV_PORTA_B) == 0) { portaBAberta=true;  portaBLock=true;  lastStatusPortaB=now; }
    }
    else
    {
      portaAbertaTempo = 0;
      char unlockMsg[32]; snprintf(unlockMsg, sizeof(unlockMsg), "UNLOCK|%s", DEVICE_NAME); sendBroadcast(unlockMsg);
      if      (strcmp(DEVICE_NAME, DEV_PORTA_A) == 0) { portaAAberta=false; portaALock=false; lastStatusPortaA=now; }
      else if (strcmp(DEVICE_NAME, DEV_PORTA_B) == 0) { portaBAberta=false; portaBLock=false; lastStatusPortaB=now; }
    }
    sendStatus(); lastStatusSent = now;
    Serial.println(portaAberta ? F("Sensor: OPEN") : F("Sensor: CLOSED"));
  }

  if (relayAtivo && now - relayStart >= RELAY_TIME)
  {
    digitalWrite(RELAY_PIN, HIGH); digitalWrite(PUPE_PIN, HIGH);
    relayAtivo = false;
    if (masterBusy)
    {
      masterBusy = false;
      Serial.println(F("Master released (relay done)."));
    }
    liberarToken();
    bool sensorAberto = (digitalRead(SENSOR_PIN) == SENSOR_OPEN);
    if (portaAberta != sensorAberto) { portaAberta = sensorAberto; sendStatus(); }
  }

  if (portaAberta && !alertSent && now - portaAbertaTempo > PORTA_TIMEOUT)
  {
    char alertMsg[64]; snprintf(alertMsg, sizeof(alertMsg), "ALERT|PORTA_ABERTA|%s", DEVICE_NAME);
    sendBroadcast(alertMsg); alertSent = true;
  }

  if (aguardandoAutorizacao && now - lastReqSent >= REQ_RETRY_INTERVAL)
  {
    if (reqRetryCount < REQ_MAX_RETRIES)
    {
      reqRetryCount++;
      char reqMsg[64]; snprintf(reqMsg, sizeof(reqMsg), "REQ_OPEN|%s|%s|%lu", reqPorta, DEVICE_NAME, openToken);
      sendBroadcast(reqMsg); lastReqSent = now;
    }
    else
    {
      Serial.println(F("No master reply after retries."));
      aguardandoAutorizacao = false; reqRetryCount = 0; reqPorta[0] = '\0';
    }
  }

  if (masterBusy && now - masterBusyTime > MASTER_BUSY_TIMEOUT)
  {
    Serial.println(F("Master busy timeout. Releasing."));
    masterBusy = false; liberarToken(); portaALock = false; portaBLock = false;
  }

  checkMasterHealth();
>>>>>>> dbd05ea8a847185caadc54c80987c61e8ae56a16
}
