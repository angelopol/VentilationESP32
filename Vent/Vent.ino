#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_arduino_version.h>
#include "DHT.h"

#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, DEVICE_HOSTNAME (ver secrets.example.h)
#include "web_ui.h"
#include "icons.h"

// Red propia para configurar el WiFi si no se puede conectar al router
#ifndef AP_SSID
#define AP_SSID "Vento"
#endif
#ifndef AP_PASSWORD
#define AP_PASSWORD "vento1234"
#endif

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

#define PWM1_Ch    0
#define PWM1_Res   8
#define PWM1_Freq  40

#define FAN_PIN 25     // salida PWM al ventilador (ajustar al pin real)

#define DHTPIN 26
#define DHTTYPE DHT11

int LEDROJO = 27;    // WiFi: fijo conectado, parpadeo lento conectando, rapido con red propia activa
int LEDVERDE = 32;   // ventilador en marcha (modos 1-5 y 7)
int LEDAZUL = 33;    // midiendo temperatura (modos 6 y 7)

// Modo actual: 0 apagado, 1-5 velocidades fijas,
// 6 todo/nada segun temperatura, 7 proporcional a la temperatura
int fanMode = 0;
long tmp = 30;       // temperatura objetivo (16..70, pasos de 3)
int dutyCycle = 0;

float h = NAN, t = NAN, hic = NAN;
bool hasReading = false;

const int SPEED_PWM[] = {0, 51, 102, 153, 204, 255};

const unsigned long SENSOR_INTERVAL = 2000;   // el DHT11 no admite lecturas mas rapidas
const unsigned long BLINK_SLOW = 500;
const unsigned long BLINK_FAST = 150;
const unsigned long CONNECT_TIMEOUT = 15000;      // tiempo maximo por intento de conexion
const unsigned long AP_AFTER_DISCONNECT = 30000;  // sin router este tiempo -> se abre la red propia
const unsigned long AP_RETRY_INTERVAL = 60000;    // con red propia activa, reintenta el router
const unsigned long AP_LINGER = 30000;            // tras conectar, mantiene la red propia un poco
unsigned long lastSensor = 0, lastBlink = 0;
bool blinkOn = false;

struct WifiCred { String ssid; String pass; };
WifiCred creds[2];              // red guardada desde el portal y la de secrets.h
int credCount = 0, credIdx = 0;
WifiCred pendingCred;           // red enviada desde el portal, se guarda solo si conecta
bool hasPending = false;
bool attempting = false;
bool apActive = false;
bool wifiWasConnected = false;
bool mdnsStarted = false;
unsigned long attemptStart = 0, lastApRetry = 0, disconnectedSince = 0, connectedSince = 0;
String currentSsid, lastFailedSsid, savedSsid;

DNSServer dns;
Preferences prefs;

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

// ---------- Ventilador ----------

void fanSetup()
{
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(FAN_PIN, PWM1_Freq, PWM1_Res);
#else
  ledcSetup(PWM1_Ch, PWM1_Freq, PWM1_Res);
  ledcAttachPin(FAN_PIN, PWM1_Ch);
#endif
}

void fanWrite(int duty)
{
  dutyCycle = constrain(duty, 0, 255);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(FAN_PIN, dutyCycle);
#else
  ledcWrite(PWM1_Ch, dutyCycle);
#endif
}

void updateFan()
{
  if (fanMode >= 1 && fanMode <= 5) {
    fanWrite(SPEED_PWM[fanMode]);
  } else if (fanMode == 6) {
    if (!hasReading) { fanWrite(0); return; }
    fanWrite(hic >= tmp ? 255 : 0);
  } else if (fanMode == 7) {
    if (!hasReading) { fanWrite(0); return; }
    if (hic <= tmp) {
      long re = 255 - (((tmp - hic) * 204) / tmp);
      fanWrite(re);
    } else {
      fanWrite(255);
    }
  } else {
    fanWrite(0);
  }
}

// ---------- Estado ----------

void setMode(int m)
{
  if (m < 0 || m > 7 || m == fanMode) return;
  fanWrite(0);
  fanMode = m;
  Serial.printf("Modo: %d\n", fanMode);
  updateFan();
}

void setSetpoint(long value)
{
  value = constrain(value, 16, 70);
  tmp = 16 + ((value - 16 + 1) / 3) * 3;   // redondea al paso de 3 mas cercano
  Serial.printf("Temperatura objetivo: %ld\n", tmp);
  updateFan();
}

// Protocolo de un caracter de la version Bluetooth, ahora por el monitor serie:
// '0'-'7' cambian de modo, 'a'-'s' fijan la temperatura 16..70
void handleCommand(char c)
{
  if (c >= '0' && c <= '7') {
    setMode(c - '0');
  } else if (c >= 'a' && c <= 's') {
    setSetpoint(16 + (c - 'a') * 3);
  }
}

void readSensor()
{
  float nh = dht.readHumidity();
  float nt = dht.readTemperature();
  if (isnan(nh) || isnan(nt)) {
    Serial.println(F("Error leyendo el DHT11"));
    return;
  }
  h = nh;
  t = nt;
  hic = dht.computeHeatIndex(t, h, false);
  hasReading = true;
}

// ---------- LEDs ----------

void updateLeds()
{
  digitalWrite(LEDAZUL, fanMode >= 6 ? HIGH : LOW);
  digitalWrite(LEDVERDE, (fanMode >= 1 && fanMode <= 5) || fanMode == 7 ? HIGH : LOW);

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LEDROJO, HIGH);
  } else if (millis() - lastBlink >= (apActive ? BLINK_FAST : BLINK_SLOW)) {
    lastBlink = millis();
    blinkOn = !blinkOn;
    digitalWrite(LEDROJO, blinkOn ? HIGH : LOW);
  }
}

// ---------- WiFi ----------

void loadCreds()
{
  credCount = 0;
  credIdx = 0;
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();
  savedSsid = ssid;
  if (ssid.length()) creds[credCount++] = {ssid, pass};
  String defSsid = WIFI_SSID;
  if (defSsid.length() && defSsid != ssid) creds[credCount++] = {defSsid, WIFI_PASSWORD};
}

void startAp()
{
  if (apActive) return;
  WiFi.setAutoReconnect(false);   // los reintentos los controla wifiLoop para no molestar a la red propia
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  dns.start(53, "*", WiFi.softAPIP());   // portal cautivo: cualquier dominio apunta al ESP32
  apActive = true;
  lastApRetry = millis();
  Serial.printf("Red propia \"%s\" activa. Configura el WiFi en http://%s/wifi\n",
                AP_SSID, WiFi.softAPIP().toString().c_str());
}

void stopAp()
{
  if (!apActive) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.setAutoReconnect(true);
  apActive = false;
  Serial.println(F("Red propia apagada"));
}

void startAttempt(const WifiCred &c)
{
  currentSsid = c.ssid;
  WiFi.begin(c.ssid.c_str(), c.pass.c_str());
  attempting = true;
  attemptStart = millis();
  Serial.printf("Conectando a la red WiFi \"%s\"...\n", c.ssid.c_str());
}

// Prueba las redes conocidas en orden; si no hay ninguna, abre la red propia
void startNextAttempt()
{
  if (credIdx >= credCount) credIdx = 0;
  if (credCount == 0) {
    startAp();
    return;
  }
  startAttempt(creds[credIdx]);
}

// Llamado desde el portal: prueba la red nueva sin guardarla todavia
void submitCred(const String &ssid, const String &pass)
{
  pendingCred = {ssid, pass};
  hasPending = true;
  lastFailedSsid = "";
  WiFi.disconnect();
  startAttempt(pendingCred);
}

void forgetSavedCred()
{
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  loadCreds();
  Serial.println(F("Red guardada olvidada"));
}

void wifiSetup()
{
  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  loadCreds();
  startNextAttempt();
}

void wifiLoop()
{
  unsigned long now = millis();
  bool connected = WiFi.status() == WL_CONNECTED;

  if (apActive) dns.processNextRequest();

  if (connected && !wifiWasConnected) {
    attempting = false;
    connectedSince = now;
    currentSsid = WiFi.SSID();
    lastFailedSsid = "";
    if (hasPending) {
      hasPending = false;
      prefs.begin("wifi", false);
      prefs.putString("ssid", pendingCred.ssid);
      prefs.putString("pass", pendingCred.pass);
      prefs.end();
      loadCreds();
      Serial.println(F("Red WiFi guardada"));
    }
    Serial.print(F("WiFi conectado. Abre http://"));
    Serial.println(WiFi.localIP());
    if (!mdnsStarted && MDNS.begin(DEVICE_HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
    }
    if (mdnsStarted) Serial.printf("o http://%s.local\n", DEVICE_HOSTNAME);
  } else if (!connected && wifiWasConnected) {
    Serial.println(F("WiFi desconectado, reintentando..."));
    disconnectedSince = now;
  }
  wifiWasConnected = connected;

  if (connected) {
    if (apActive && now - connectedSince >= AP_LINGER) stopAp();
    return;
  }

  if (attempting) {
    if (now - attemptStart < CONNECT_TIMEOUT) return;
    attempting = false;
    WiFi.disconnect();
    Serial.printf("No se pudo conectar a \"%s\"\n", currentSsid.c_str());
    if (hasPending) {
      hasPending = false;
      lastFailedSsid = pendingCred.ssid;
      credIdx = 0;
      if (apActive) lastApRetry = now;
      else startNextAttempt();          // vuelve a las redes conocidas
      return;
    }
    credIdx++;
    if (credIdx < credCount) {
      startNextAttempt();
      return;
    }
    credIdx = 0;
    startAp();
    lastApRetry = now;
    return;
  }

  if (!apActive) {
    // Se perdio el router: el ESP32 reintenta solo; si tarda demasiado abre la red propia
    if (now - disconnectedSince >= AP_AFTER_DISCONNECT) startAp();
  } else if (credCount > 0 && now - lastApRetry >= AP_RETRY_INTERVAL
             && WiFi.softAPgetStationNum() == 0) {
    // Solo reintenta si nadie esta usando la red propia (el intento cambia de canal)
    lastApRetry = now;
    startNextAttempt();
  }
}

// ---------- Servidor web ----------

void addJsonNumber(String &json, const char *key, float value)
{
  json += '"';
  json += key;
  json += "\":";
  if (isnan(value)) json += "null";
  else json += String(value, 1);
}

String jsonString(const String &value)
{
  String out = "\"";
  for (size_t k = 0; k < value.length(); k++) {
    char c = value[k];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((uint8_t)c < 0x20) {
      char buf[7];
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      out += buf;
    } else {
      out += c;
    }
  }
  out += '"';
  return out;
}

void sendState()
{
  String json = "{\"mode\":";
  json += fanMode;
  json += ",\"setpoint\":";
  json += tmp;
  json += ",\"pwm\":";
  json += dutyCycle;
  json += ',';
  addJsonNumber(json, "temp", hasReading ? t : NAN);
  json += ',';
  addJsonNumber(json, "hum", hasReading ? h : NAN);
  json += ',';
  addJsonNumber(json, "hic", hasReading ? hic : NAN);
  json += ",\"rssi\":";
  json += WiFi.RSSI();
  json += '}';
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void sendIcon(const uint8_t *data, size_t len)
{
  server.sendHeader("Cache-Control", "public, max-age=604800");
  server.send_P(200, "image/png", (const char *)data, len);
}

void sendWifiStatus()
{
  bool connected = WiFi.status() == WL_CONNECTED;
  String json = "{\"connected\":";
  json += connected ? "true" : "false";
  json += ",\"ssid\":";
  json += jsonString(connected ? WiFi.SSID() : String());
  json += ",\"ip\":";
  json += jsonString(connected ? WiFi.localIP().toString() : String());
  json += ",\"attempting\":";
  json += attempting ? "true" : "false";
  json += ",\"target\":";
  json += jsonString(currentSsid);
  json += ",\"failed\":";
  json += jsonString(lastFailedSsid);
  json += ",\"saved\":";
  json += jsonString(savedSsid);
  json += ",\"ap\":";
  json += apActive ? "true" : "false";
  json += ",\"apSsid\":";
  json += jsonString(AP_SSID);
  json += ",\"apIp\":";
  json += jsonString(WiFi.softAPIP().toString());
  json += '}';
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void sendWifiScan()
{
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);   // asincrono, el portal vuelve a preguntar
    n = WIFI_SCAN_RUNNING;
  }
  if (n == WIFI_SCAN_RUNNING) {
    server.send(200, "application/json", "{\"scanning\":true}");
    return;
  }
  String json = "{\"scanning\":false,\"networks\":[";
  for (int k = 0; k < n; k++) {
    if (k) json += ',';
    json += "{\"ssid\":";
    json += jsonString(WiFi.SSID(k));
    json += ",\"rssi\":";
    json += WiFi.RSSI(k);
    json += ",\"open\":";
    json += WiFi.encryptionType(k) == WIFI_AUTH_OPEN ? "true" : "false";
    json += '}';
  }
  json += "]}";
  WiFi.scanDelete();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void webSetup()
{
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/wifi", HTTP_GET, []() {
    server.send_P(200, "text/html; charset=utf-8", WIFI_HTML);
  });
  server.on("/app.css", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "public, max-age=3600");
    server.send_P(200, "text/css", APP_CSS);
  });
  server.on("/manifest.json", HTTP_GET, []() {
    server.send_P(200, "application/manifest+json", MANIFEST_JSON);
  });
  server.on("/apple-touch-icon.png", HTTP_GET, []() { sendIcon(ICON_180, ICON_180_len); });
  server.on("/apple-touch-icon-precomposed.png", HTTP_GET, []() { sendIcon(ICON_180, ICON_180_len); });
  server.on("/favicon.ico", HTTP_GET, []() { sendIcon(ICON_192, ICON_192_len); });
  server.on("/icon-192.png", HTTP_GET, []() { sendIcon(ICON_192, ICON_192_len); });
  server.on("/icon-512.png", HTTP_GET, []() { sendIcon(ICON_512, ICON_512_len); });

  server.on("/api/state", HTTP_GET, sendState);
  server.on("/api/mode", HTTP_POST, []() {
    if (!server.hasArg("v")) { server.send(400, "text/plain", "falta v"); return; }
    setMode(server.arg("v").toInt());
    updateLeds();
    sendState();
  });
  server.on("/api/setpoint", HTTP_POST, []() {
    if (!server.hasArg("v")) { server.send(400, "text/plain", "falta v"); return; }
    setSetpoint(server.arg("v").toInt());
    sendState();
  });
  server.on("/api/wifi/status", HTTP_GET, sendWifiStatus);
  server.on("/api/wifi/scan", HTTP_GET, sendWifiScan);
  server.on("/api/wifi", HTTP_POST, []() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (ssid.length() == 0 || ssid.length() > 32 || (pass.length() > 0 && pass.length() < 8) || pass.length() > 63) {
      server.send(400, "application/json", "{\"error\":\"Nombre o contrasena no validos\"}");
      return;
    }
    server.send(200, "application/json", "{\"ok\":true}");
    submitCred(ssid, pass);
  });
  server.on("/api/wifi/forget", HTTP_POST, []() {
    forgetSavedCred();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.onNotFound([]() {
    if (apActive) {
      // Portal cautivo: el iPhone abre esta pagina al unirse a la red propia
      server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/wifi");
      server.send(302, "text/plain", "");
      return;
    }
    server.send(404, "text/plain", "No encontrado");
  });
  server.begin();
}

// ---------- Arduino ----------

void setup()
{
  pinMode(LEDROJO, OUTPUT);
  pinMode(LEDVERDE, OUTPUT);
  pinMode(LEDAZUL, OUTPUT);
  digitalWrite(LEDAZUL, LOW);
  digitalWrite(LEDROJO, LOW);
  digitalWrite(LEDVERDE, LOW);

  fanSetup();
  fanWrite(0);

  Serial.begin(115200);
  dht.begin();

  wifiSetup();
  webSetup();
}

void loop()
{
  server.handleClient();
  wifiLoop();

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') continue;
    Serial.println(c);
    handleCommand(c);
  }

  if (millis() - lastSensor >= SENSOR_INTERVAL || lastSensor == 0) {
    lastSensor = millis();
    readSensor();
    updateFan();
  }

  updateLeds();
  delay(2);
}
