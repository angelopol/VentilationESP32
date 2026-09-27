#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_arduino_version.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include "DHT.h"

#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, DEVICE_HOSTNAME, AC_DEVICES_JSON (ver secrets.example.h)
#include "web_ui.h"
#include "aircon.h"
#include "icons.h"

// Definido antes de cualquier funcion: el IDE inserta los prototipos automaticos
// justo antes de la primera funcion y necesitan conocer este tipo
struct WifiCred { String ssid; String pass; };

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
// 25 kHz: fuera del rango audible, giro suave. Si el driver se calienta o la velocidad
// no cambia entre niveles (modulos con optoacoplador), bajar a 1000 o 500.
#define PWM1_Freq  25000

#define FAN_PIN 25     // salida PWM al ventilador (ajustar al pin real)

#define DHTPIN 26
#define DHTTYPE DHT11

int LEDROJO = 27;    // WiFi: fijo conectado, parpadeo lento conectando, rapido con red propia activa
int LEDVERDE = 32;   // ventilador en marcha (modos 1-5 y 7)
int LEDAZUL = 33;    // midiendo temperatura (modos 6 y 7)

// Modo actual: 0 apagado, 1-5 velocidades fijas,
// 6 todo/nada segun temperatura, 7 proporcional a la temperatura
int fanMode = 0;
long tmp = 30;       // temperatura objetivo (16..70, de grado en grado)
// Modo Auto: se enciende al llegar al objetivo y no se apaga hasta bajar estos grados por
// debajo, para que no se encienda y apague sin parar alrededor del umbral (0..10)
long hysteresis = 2;
bool autoOn = false;  // estado del modo Auto dentro del margen
int dutyCycle = 0;

float h = NAN, t = NAN, hic = NAN;
bool hasReading = false;

// PWM minimo con el que el motor se mantiene girando: los niveles 1-5 y el modo
// progresivo se reparten entre este valor y 255. Subirlo si el nivel 1 se queda corto.
#define FAN_MIN_PWM 179   // ~70 %
#define FAN_KICK_MS 500   // al arrancar desde parado se aplica el 100 % este tiempo

int speedPwm(int level)   // nivel 1..5 -> PWM
{
  return FAN_MIN_PWM + (255 - FAN_MIN_PWM) * (level - 1) / 4;
}

int appliedDuty = 0;      // lo que recibe el motor (255 durante el impulso de arranque)
unsigned long kickUntil = 0;
bool kicking = false;

const unsigned long SENSOR_INTERVAL = 2000;   // el DHT11 no admite lecturas mas rapidas
const unsigned long BLINK_SLOW = 500;
const unsigned long BLINK_FAST = 150;
const unsigned long CONNECT_TIMEOUT = 15000;      // tiempo maximo por intento de conexion
const unsigned long AP_AFTER_DISCONNECT = 30000;  // sin router este tiempo -> se abre la red propia
const unsigned long AP_RETRY_INTERVAL = 60000;    // con red propia activa, reintenta el router
const unsigned long AP_LINGER = 30000;            // tras conectar, mantiene la red propia un poco
unsigned long lastSensor = 0, lastBlink = 0;
bool blinkOn = false;

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
volatile int lastDisconnectReason = 0;   // motivo del ultimo fallo del WiFi (wifi_err_reason_t)
unsigned long lastPortalUse = 0;         // ultima peticion al portal de configuracion
const unsigned long PORTAL_BUSY = 180000; // mientras se usa el portal no se reintenta el router

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

void fanApply(int duty)
{
  appliedDuty = duty;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(FAN_PIN, duty);
#else
  ledcWrite(PWM1_Ch, duty);
#endif
}

void fanWrite(int duty)
{
  dutyCycle = constrain(duty, 0, 255);
  if (dutyCycle == 0) {
    kicking = false;
    fanApply(0);
  } else if (appliedDuty == 0 && dutyCycle < 255) {
    // Motor parado: impulso al 100 % para vencer la inercia, luego baja al nivel pedido
    kicking = true;
    kickUntil = millis() + FAN_KICK_MS;
    fanApply(255);
  } else if (!kicking) {
    fanApply(dutyCycle);
  }
}

void fanLoop()
{
  if (kicking && (long)(millis() - kickUntil) >= 0) {
    kicking = false;
    fanApply(dutyCycle);
  }
}

void updateFan()
{
  if (fanMode >= 1 && fanMode <= 5) {
    fanWrite(speedPwm(fanMode));
  } else if (fanMode == 6) {
    if (!hasReading) { fanWrite(0); return; }
    if (hic >= tmp) autoOn = true;
    else if (hic <= tmp - hysteresis) autoOn = false;
    fanWrite(autoOn ? 255 : 0);
  } else if (fanMode == 7) {
    if (!hasReading) { fanWrite(0); return; }
    if (hic <= tmp) {
      // Fraccion original 0.2..1 reescalada al rango util del motor
      float f = constrain(1.0f - (tmp - hic) * 0.8f / tmp, 0.2f, 1.0f);
      fanWrite(FAN_MIN_PWM + (255 - FAN_MIN_PWM) * (f - 0.2f) / 0.8f);
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
  fanMode = m;
  autoOn = false;   // al entrar en Auto se enciende solo si ya se llego al objetivo
  Serial.printf("Modo: %d\n", fanMode);
  updateFan();
}

// La temperatura objetivo se guarda en flash y se recupera al arrancar. Solo se escribe
// cuando cambia (la web la manda al soltar el slider), asi la flash apenas se desgasta.
void setSetpoint(long value)
{
  value = constrain(value, 16, 70);
  if (value != tmp) {
    prefs.begin("vento", false);
    prefs.putLong("setpoint", value);
    prefs.end();
  }
  tmp = value;
  Serial.printf("Temperatura objetivo: %ld\n", tmp);
  updateFan();
}

void setHysteresis(long value)
{
  value = constrain(value, 0, 10);
  if (value != hysteresis) {
    prefs.begin("vento", false);
    prefs.putLong("hyst", value);
    prefs.end();
  }
  hysteresis = value;
  Serial.printf("Margen del modo Auto: %ld\n", hysteresis);
  updateFan();
}

void loadSetpoint()
{
  prefs.begin("vento", true);
  tmp = constrain(prefs.getLong("setpoint", tmp), 16, 70);
  hysteresis = constrain(prefs.getLong("hyst", hysteresis), 0, 10);
  prefs.end();
  Serial.printf("Temperatura objetivo guardada: %ld (margen %ld)\n", tmp, hysteresis);
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
  } else if (millis() - lastBlink >= (apActive && !attempting ? BLINK_FAST : BLINK_SLOW)) {
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
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    lastDisconnectReason = info.wifi_sta_disconnected.reason;
    Serial.printf("WiFi: %s (%d)\n", disconnectText(lastDisconnectReason), lastDisconnectReason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  // Canales 1-13: por defecto el ESP32 solo busca activamente en 1-11 y no encuentra un
  // router que haya elegido el 12 o el 13
  wifi_country_t country = {"01", 1, 13, 20, WIFI_COUNTRY_POLICY_MANUAL};
  esp_wifi_set_country(&country);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);   // WiFi siempre despierto: responde antes y mDNS (vento.local) no falla
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
             && (WiFi.softAPgetStationNum() == 0 || now - lastPortalUse >= PORTAL_BUSY)) {
    // El intento puede cambiar de canal y desconectar un momento a quien este en la red propia:
    // no se reintenta mientras alguien usa el portal, pero si solo hay un movil que se unio
    // solo (p. ej. porque recuerda la red "Vento"), si
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

String stateJson()
{
  String json = "{\"mode\":";
  json += fanMode;
  json += ",\"setpoint\":";
  json += tmp;
  json += ",\"hyst\":";
  json += hysteresis;
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
  return json;
}

void sendState()
{
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", stateJson());
}

const char *resetReason()
{
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "encendido";
    case ESP_RST_EXT:      return "boton reset";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "error (panic)";
    case ESP_RST_INT_WDT:  return "watchdog de interrupciones";
    case ESP_RST_TASK_WDT: return "watchdog de tareas";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "bajada de tension";
    default:               return "otro";
  }
}

// La web, una vez sabe la IP, llama a la API por IP en vez de por vento.local (mDNS a veces
// no resuelve): es otro origen, de ahi la cabecera CORS
void sendJson(int code, const String &json)
{
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", json);
}

void sendIcon(const uint8_t *data, size_t len)
{
  server.sendHeader("Cache-Control", "public, max-age=604800");
  server.send_P(200, "image/png", (const char *)data, len);
}

// Motivo de un fallo del WiFi (wifi_err_reason_t) en palabras
const char *disconnectText(int reason)
{
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
      return "no encuentra la red (nombre distinto, solo 5 GHz o fuera de alcance)";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
      return "la contrasena no coincide (el router no completo el cifrado)";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
      return "el router rechazo la autenticacion (contrasena o filtro de dispositivos)";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
      return "la seguridad de la red no es compatible (usa WPA2 o WPA2/WPA3)";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
    case WIFI_REASON_BEACON_TIMEOUT:
      return "senal demasiado debil";
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_CONNECTION_FAIL:
    case WIFI_REASON_ASSOC_TOOMANY:
      return "el router no acepto la conexion (¿limite de dispositivos o filtro MAC?)";
    case WIFI_REASON_ASSOC_LEAVE:
    case WIFI_REASON_AUTH_LEAVE:
      return "desconectado";
    default:
      return "error del WiFi";
  }
}

void sendWifiStatus()
{
  lastPortalUse = millis();
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
  json += ",\"reason\":";
  json += jsonString(connected || !lastDisconnectReason ? String()
                    : String(disconnectText(lastDisconnectReason)) + " (codigo " + lastDisconnectReason + ")");
  // MAC con la que Vento se presenta al router (para filtros de dispositivos o reservar la IP)
  json += ",\"mac\":";
  json += jsonString(WiFi.macAddress());
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
  lastPortalUse = millis();
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
    json += ",\"channel\":";
    json += WiFi.channel(k);
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

  // ?ac=1 (la web): incluye los aires, asi la pagina hace una sola consulta periodica.
  // La app de Windows no lo pide para no mantener activo el sondeo de los aires.
  server.on("/api/state", HTTP_GET, []() {
    String json = stateJson();
    json.remove(json.length() - 1);
    // Diagnostico: si Vento se reinicia, "up" vuelve a 0 y "reset" dice por que
    json += ",\"up\":";
    json += millis() / 1000;
    json += ",\"heap\":";
    json += ESP.getFreeHeap();
    json += ",\"minHeap\":";
    json += ESP.getMinFreeHeap();
    json += ",\"reset\":\"";
    json += resetReason();
    json += "\",\"ip\":\"";
    json += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String();
    json += '"';
    if (server.arg("ac") == "1" && acCount() > 0) {
      json += ",\"acs\":";
      json += acStateJson();
    }
    json += '}';
    sendJson(200, json);
  });
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
  server.on("/api/hysteresis", HTTP_POST, []() {
    if (!server.hasArg("v")) { server.send(400, "text/plain", "falta v"); return; }
    setHysteresis(server.arg("v").toInt());
    sendState();
  });
  server.on("/api/ac", HTTP_GET, []() { sendJson(200, acStateJson()); });
  server.on("/api/ac/raw", HTTP_GET, []() { sendJson(200, acRawJson(server.arg("d").toInt())); });
  // /api/ac/{power,temp,mode,fan,toggle}?d=<aparato>&v=<valor>
  for (const char *what : {"power", "temp", "mode", "fan", "toggle"}) {
    server.on(String("/api/ac/") + what, HTTP_POST, [what]() {
      String error;
      if (!acCommand(server.arg("d").toInt(), what, server.arg("v"), error)) {
        sendJson(400, "{\"error\":" + jsonString(error) + "}");
        return;
      }
      sendJson(200, acStateJson());
    });
  }
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
  Serial.printf("Arranque. Motivo del ultimo reinicio: %s\n", resetReason());
  dht.begin();
  loadSetpoint();

  wifiSetup();
  configTime(0, 0, "pool.ntp.org");   // hora para los mensajes a los aires Tuya
  acSetup();
  webSetup();
}


void loop()
{
  server.handleClient();
  wifiLoop();
  fanLoop();

  if (millis() - lastSensor >= SENSOR_INTERVAL || lastSensor == 0) {
    lastSensor = millis();
    readSensor();
    updateFan();
  }

  updateLeds();
  delay(2);
}
