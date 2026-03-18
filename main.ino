#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

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
};

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
    buf[i] = EEPROM.read(addr + i);
    if (buf[i] == '\0') break;
  }
  buf[maxLen - 1] = '\0';
}

void eepromWriteStr(int addr, const char *str, int maxLen)
{
  int len = (int)strlen(str);
  int end = (len < maxLen - 1) ? len : maxLen - 1;
  for (int i = 0; i < end; i++)
    EEPROM.write(addr + i, str[i]);
  EEPROM.write(addr + end, '\0');
}

bool eepromHasCredentials()
{
  return EEPROM.read(EEPROM_VALID_ADDR) == EEPROM_VALID_MAGIC;
}

void eepromSaveCredentials(const char *ssid, const char *pass)
{
  eepromWriteStr(EEPROM_SSID_ADDR, ssid, 64);
  eepromWriteStr(EEPROM_PASS_ADDR, pass, 64);
  EEPROM.write(EEPROM_VALID_ADDR, EEPROM_VALID_MAGIC);
  EEPROM.commit();
  Serial.println(F("Credentials saved."));
}

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
  udp.endPacket();
}

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
    }
  }
}

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
  }
  return true;
}

void abrirPorta()
{
  if (relayAtivo) return;
  Serial.println(F("Opening door."));
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(PUPE_PIN,  LOW);
  relayStart    = millis();
  relayAtivo    = true;
  lastOpenEvent = millis();
}

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
      }
    }
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

  pinMode(BTN1_PIN,   INPUT_PULLUP);
  pinMode(BTN2_PIN,   INPUT_PULLUP);
  pinMode(BYPASS_PIN, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN,  OUTPUT);
  pinMode(PUPE_PIN,   OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(PUPE_PIN,  HIGH);

  // GPIO0 boot guard: if bypass switch is closed at power-on the chip would
  // enter flash mode. We detect it and warn; bypass input is ignored for 2s.
  delay(200);
  if (strcmp(DEVICE_NAME, DEV_PORTEIRO) == 0 && digitalRead(BYPASS_PIN) == LOW)
  {
    Serial.println(F("WARNING: BYPASS_PIN (GPIO0) is LOW at boot."));
    Serial.println(F("Add a 10k pull-up resistor between D3 and 3.3V."));
  }
  bypassBootTime = millis();

  memset(devices, 0, sizeof(devices));

  if      (strcmp(DEVICE_NAME, DEV_PORTEIRO) == 0) devicePriority = 3;
  else if (strcmp(DEVICE_NAME, DEV_PORTA_A)  == 0) devicePriority = 2;
  else                                               devicePriority = 1;

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
    }
  }

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
    }
    lastBtn1 = btn1;

    static bool lastBtn2 = HIGH; static unsigned long lastBtn2Press = 0;
    bool btn2 = digitalRead(BTN2_PIN);
    if (btn2 == LOW && lastBtn2 == HIGH && now - lastBtn2Press > 300UL)
    {
      lastBtn2Press = now;
      if (podeAbrir(DEV_PORTA_B, true)) solicitarAbertura(DEV_PORTA_B, true);
    }
    lastBtn2 = btn2;

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
    }
    lastBtn = btnState;
  }

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
}
