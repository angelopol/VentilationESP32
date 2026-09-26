#include "aircon.h"
#include "tuya.h"
#include "secrets.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <map>
#include <vector>

#ifndef AC_DEVICES_JSON
#define AC_DEVICES_JSON "[]"
#endif

// Mientras alguien mire la app (o la de Windows use la API) se consultan los aparatos cada
// POLL_INTERVAL; sin nadie mirando no se les molesta. Tras un fallo se espera mas.
static const unsigned long ACTIVE_WINDOW = 30000;
static const unsigned long POLL_INTERVAL = 10000;
// El aire (o la red) a veces no contesta una vez aunque funcione: solo se da por perdido
// tras varios fallos seguidos, y mientras tanto se sigue mostrando el ultimo estado
static const int FAILS_OFFLINE = 3;
static const unsigned long RETRY_INTERVAL = 15000;
// Tras una orden, el aire tarda unos segundos en aplicarla: mientras tanto (y como mucho HOLD)
// se sigue mostrando el valor pedido aunque la consulta devuelva todavia el anterior
static const unsigned long HOLD = 15000;

// Valores tipicos de la categoria "kt" (aire acondicionado) de Tuya
static const char DEFAULT_MODES[] =
  "{\"cold\":\"Frío\",\"wet\":\"Seco\",\"wind\":\"Ventilador\",\"hot\":\"Calor\",\"auto\":\"Auto\"}";
static const char DEFAULT_FANS[] =
  "{\"low\":\"Baja\",\"mid\":\"Media\",\"high\":\"Alta\",\"auto\":\"Auto\"}";

struct AcDevice {
  // Configuracion (solo lectura tras acSetup)
  String name;
  int dpPower, dpSetpoint, dpTemp, dpMode, dpFan;
  float scale, minT, maxT, step;
  String requestDps;   // {"1":null,...} para los aparatos que solo informan de lo que se pide
  JsonObject modes, fans;
  std::vector<std::pair<int, String>> toggles;   // interruptores extra: DP -> nombre (oscilacion, sueño...)
  TuyaClient client;

  // Estado, protegido por lock
  bool polled = false, online = false, power = false;
  int fails = 0;
  float setpoint = NAN, temp = NAN;
  String mode, fan, raw, error;
  std::map<int, bool> toggleOn;
  std::map<int, String> pending;   // DP -> valor JSON por enviar
  struct Expect { String json; unsigned long until; };
  std::map<int, Expect> expect;    // DP -> valor pedido que el aire aun no ha confirmado
  unsigned long nextPoll = 0;
};

static JsonDocument config;
static std::vector<AcDevice *> devices;
static SemaphoreHandle_t lock;
static volatile unsigned long lastActivity = 0;

struct Guard {
  Guard() { xSemaphoreTake(lock, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(lock); }
};

// Pone una orden en cola y recuerda el valor hasta que el aire lo confirme
static void queue(AcDevice &d, int dp, const String &json)
{
  d.pending[dp] = json;
  d.expect[dp] = {json, millis() + HOLD};
}

// Con lock tomado
static void applyDps(AcDevice &d, const String &raw)
{
  JsonDocument doc;
  if (deserializeJson(doc, raw)) return;

  // Los DP con una orden sin confirmar no se actualizan hasta que el aire informe del valor
  // pedido o pase HOLD (si el aire la rechazo, se vuelve a mostrar lo que dice el aparato)
  std::vector<int> held;
  for (auto it = d.expect.begin(); it != d.expect.end();) {
    JsonVariantConst v = doc[String(it->first)];
    String got;
    if (!v.isNull()) serializeJson(v, got);
    if (got == it->second.json || (long)(millis() - it->second.until) >= 0) {
      it = d.expect.erase(it);
    } else {
      held.push_back(it->first);
      ++it;
    }
  }
  auto get = [&](int dp) -> JsonVariantConst {
    if (dp <= 0 || std::find(held.begin(), held.end(), dp) != held.end()) return JsonVariantConst();
    return doc[String(dp)];
  };

  JsonVariantConst v;
  if (!(v = get(d.dpPower)).isNull()) d.power = v.as<bool>();
  if (!(v = get(d.dpSetpoint)).isNull()) d.setpoint = v.as<float>() / d.scale;
  if (!(v = get(d.dpTemp)).isNull()) d.temp = v.as<float>() / d.scale;
  if (!(v = get(d.dpMode)).isNull()) d.mode = v.as<String>();
  if (!(v = get(d.dpFan)).isNull()) d.fan = v.as<String>();
  for (auto &t : d.toggles)
    if (!(v = get(t.first)).isNull()) d.toggleOn[t.first] = v.as<bool>();
}

static void pollDevice(AcDevice &d)
{
  String raw;
  bool ok = d.client.query(raw, d.requestDps);
  Guard g;
  d.polled = true;
  if (!ok) {
    // Hasta FAILS_OFFLINE se reintenta pronto sin cambiar lo que se muestra
    if (++d.fails >= FAILS_OFFLINE || !d.online) {
      d.online = false;
      d.error = d.client.lastError;
      d.nextPoll = millis() + RETRY_INTERVAL;
    } else {
      d.nextPoll = millis() + 2000;
    }
    return;
  }
  d.fails = 0;
  d.online = true;
  d.nextPoll = millis() + POLL_INTERVAL;
  d.error = "";
  d.raw = raw;
  applyDps(d, raw);
}

static void acTask(void *)
{
  for (;;) {
    for (AcDevice *p : devices) {
      AcDevice &d = *p;
      if (WiFi.status() != WL_CONNECTED) continue;

      String cmd;
      bool poll;
      {
        Guard g;
        if (!d.pending.empty()) {
          cmd = "{";
          for (auto &kv : d.pending) {
            if (cmd.length() > 1) cmd += ',';
            cmd += '"' + String(kv.first) + "\":" + kv.second;
          }
          cmd += '}';
          d.pending.clear();
        }
        bool active = !d.polled || millis() - lastActivity < ACTIVE_WINDOW;
        poll = active && (long)(millis() - d.nextPoll) >= 0;
      }

      if (cmd.length()) {
        // El aparato puede estar atendiendo otra conexion (p. ej. la app de Windows): un reintento
        bool ok = d.client.set(cmd);
        if (!ok) {
          vTaskDelay(pdMS_TO_TICKS(500));
          ok = d.client.set(cmd);
        }
        if (!ok) Serial.printf("Aire \"%s\": %s\n", d.name.c_str(), d.client.lastError.c_str());
        poll = true;
      }
      if (poll) pollDevice(d);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void acSetup()
{
  lock = xSemaphoreCreateMutex();
  DeserializationError err = deserializeJson(config, AC_DEVICES_JSON);
  if (err) {
    Serial.printf("AC_DEVICES_JSON no es JSON valido: %s\n", err.c_str());
    return;
  }

  static JsonDocument defaults;
  deserializeJson(defaults, "{\"modes\":" + String(DEFAULT_MODES) + ",\"fans\":" + DEFAULT_FANS + "}");

  for (JsonObject o : config.as<JsonArray>()) {
    String id = o["id"] | "", key = o["key"] | "", ip = o["ip"] | "";
    if (id.isEmpty() || key.length() != 16 || ip.isEmpty()) {
      Serial.printf("Aire \"%s\" ignorado: faltan id, ip o una key de 16 caracteres\n", (const char *)(o["name"] | "?"));
      continue;
    }
    // "auto" (o sin version): se averigua probando 3.3, 3.4 y 3.5
    String version = o["version"].isNull() ? String("auto") : o["version"].as<String>();

    AcDevice *d = new AcDevice();
    d->name = o["name"] | "Aire";
    JsonObject dps = o["dps"];
    d->dpPower = dps["power"] | 1;
    d->dpSetpoint = dps["setpoint"] | 2;
    d->dpTemp = dps["temp"] | 3;
    d->dpMode = dps["mode"] | 4;
    d->dpFan = dps["fan"] | 5;
    d->scale = o["scale"] | 1.0f;
    if (d->scale <= 0) d->scale = 1;
    d->minT = o["min"] | 16.0f;
    d->maxT = o["max"] | 31.0f;
    d->step = o["step"] | 1.0f;
    d->modes = o["modes"].is<JsonObject>() ? o["modes"].as<JsonObject>() : defaults["modes"].as<JsonObject>();
    d->fans = o["fans"].is<JsonObject>() ? o["fans"].as<JsonObject>() : defaults["fans"].as<JsonObject>();
    d->client.begin(id, key, ip, (int)lroundf(version.toFloat() * 10));
    // "toggles": {"15": "Oscilación", "101": "Sueño"}
    for (JsonPair kv : o["toggles"].as<JsonObject>()) {
      int dp = atoi(kv.key().c_str());
      if (dp > 0) d->toggles.push_back({dp, kv.value().as<String>()});
    }
    std::vector<int> all = {d->dpPower, d->dpSetpoint, d->dpTemp, d->dpMode, d->dpFan};
    for (auto &t : d->toggles) all.push_back(t.first);
    d->requestDps = "{";
    for (int dp : all) {
      if (dp <= 0) continue;
      if (d->requestDps.length() > 1) d->requestDps += ',';
      d->requestDps += '"' + String(dp) + "\":null";
    }
    d->requestDps += '}';
    devices.push_back(d);
    Serial.printf("Aire \"%s\" en %s (protocolo %s)\n", d->name.c_str(), ip.c_str(), version.c_str());
  }

  if (!devices.empty())
    // Nucleo 1 (el del loop): el 0 queda para WiFi, Bluetooth y mDNS
    xTaskCreatePinnedToCore(acTask, "aircon", 10240, nullptr, 1, nullptr, 1);
}

int acCount()
{
  return devices.size();
}

String acStateJson()
{
  lastActivity = millis();
  JsonDocument out;
  JsonArray arr = out["devices"].to<JsonArray>();
  {
    Guard g;
    for (AcDevice *p : devices) {
      AcDevice &d = *p;
      JsonObject o = arr.add<JsonObject>();
      o["name"] = d.name;
      o["online"] = d.online;
      o["polled"] = d.polled;
      o["error"] = d.error;
      o["power"] = d.power;
      if (isnan(d.setpoint)) o["setpoint"] = nullptr; else o["setpoint"] = d.setpoint;
      if (isnan(d.temp) || !d.dpTemp) o["temp"] = nullptr; else o["temp"] = d.temp;
      o["mode"] = d.mode;
      o["fan"] = d.fan;
      o["min"] = d.minT;
      o["max"] = d.maxT;
      o["step"] = d.step;
      if (d.dpMode) o["modes"] = d.modes; else o["modes"].to<JsonObject>();
      if (d.dpFan) o["fans"] = d.fans; else o["fans"].to<JsonObject>();
      JsonArray toggles = o["toggles"].to<JsonArray>();
      for (auto &t : d.toggles) {
        JsonObject j = toggles.add<JsonObject>();
        j["dp"] = t.first;
        j["name"] = t.second;
        j["on"] = d.toggleOn[t.first];
      }
    }
  }
  String s;
  serializeJson(out, s);
  return s;
}

bool acCommand(int index, const String &what, const String &value, String &error)
{
  if (index < 0 || index >= (int)devices.size()) { error = "Aparato no valido"; return false; }
  AcDevice &d = *devices[index];
  lastActivity = millis();
  Guard g;

  if (what == "power" && d.dpPower) {
    bool on = value == "1" || value == "true" || value == "on";
    queue(d, d.dpPower, on ? "true" : "false");
    d.power = on;
    return true;
  }
  if (what == "temp" && d.dpSetpoint) {
    if (value.isEmpty()) { error = "Falta la temperatura"; return false; }
    float t = constrain(value.toFloat(), d.minT, d.maxT);
    t = d.minT + roundf((t - d.minT) / d.step) * d.step;
    queue(d, d.dpSetpoint, String(lroundf(t * d.scale)));
    d.setpoint = t;
    return true;
  }
  if ((what == "mode" && d.dpMode) || (what == "fan" && d.dpFan)) {
    bool isMode = what == "mode";
    JsonObject options = isMode ? d.modes : d.fans;
    if (value.isEmpty() || value.indexOf('"') >= 0 || value.indexOf('\\') >= 0 || options[value].isNull()) {
      error = isMode ? "Modo no valido" : "Velocidad no valida";
      return false;
    }
    queue(d, isMode ? d.dpMode : d.dpFan, '"' + value + '"');
    (isMode ? d.mode : d.fan) = value;
    return true;
  }
  if (what == "toggle") {
    // value: "<dp>:<0|1>"
    int colon = value.indexOf(':');
    int dp = value.substring(0, colon).toInt();
    bool known = false;
    for (auto &t : d.toggles) known |= t.first == dp;
    if (colon < 0 || !known) { error = "Interruptor no valido"; return false; }
    bool on = value.substring(colon + 1) == "1";
    queue(d, dp, on ? "true" : "false");
    d.toggleOn[dp] = on;
    return true;
  }
  error = "Orden no valida";
  return false;
}

String acRawJson(int index)
{
  if (index < 0 || index >= (int)devices.size()) return "{\"error\":\"Aparato no valido\"}";
  lastActivity = millis();
  AcDevice &d = *devices[index];
  Guard g;
  JsonDocument out;
  out["name"] = d.name;
  out["online"] = d.online;
  out["error"] = d.error;
  // Version del protocolo detectada: ponerla en secrets.h ahorra la deteccion al arrancar
  int v = d.client.version();
  if (v) out["version"] = String(v / 10) + "." + String(v % 10); else out["version"] = "auto";
  JsonDocument dps;
  if (d.raw.length() && !deserializeJson(dps, d.raw)) out["dps"] = dps;
  else out["dps"] = nullptr;
  String s;
  serializeJson(out, s);
  return s;
}
