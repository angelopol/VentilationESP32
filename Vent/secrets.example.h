// Copia este archivo como "secrets.h" y pon los datos de tu red WiFi.
// secrets.h esta en .gitignore para no subir la contrasena al repositorio.
#pragma once

#define WIFI_SSID     "TuRedWiFi"
#define WIFI_PASSWORD "TuContrasena"

// Nombre en la red: la app queda en http://vento.local
#define DEVICE_HOSTNAME "vento"

// Opcional: red propia que crea Vento cuando no puede conectarse al router
// (portal de configuracion en http://192.168.4.1/wifi). Minimo 8 caracteres.
// #define AP_SSID     "Vento"
// #define AP_PASSWORD "vento1234"

// Opcional: aires acondicionados Tuya controlados por la red local (uno o varios).
// id y key (local_key, 16 caracteres) salen de "python -m tinytuya wizard".
// version: "auto" prueba 3.3, 3.4 y 3.5 la primera vez (o pon la que muestre el scan).
// Pon al aire una IP fija en el router para que no cambie.
// dps: numero de cada dato en tu modelo (0 = no lo tiene). Para averiguarlos abre
// http://vento.local/api/ac/raw?d=0 y cambia cosas desde el mando o la app Smart Life.
// modes / fans: valor que usa el aparato -> nombre en la app (algunos fabricantes usan
// otros, p. ej. "Cool"/"Fan"/"Low"). toggles: interruptores extra, DP -> nombre.
// El modelo completo con todos los DP sale de la API de Tuya: /v2.0/cloud/thing/<id>/model.
// scale: 10 si el aire
// manda 240 para 24,0 °C. Todo menos name, id, key e ip es opcional.
// El mismo JSON (cada objeto de la lista) sirve para "AirConditioners" en la app de Windows.
#define AC_DEVICES_JSON R"json([
  {
    "name": "Aire",
    "id": "bf0123456789abcdef",
    "key": "0123456789abcdef",
    "ip": "192.168.1.50",
    "version": "auto",
    "dps": { "power": 1, "setpoint": 2, "temp": 3, "mode": 4, "fan": 5 },
    "scale": 1, "min": 16, "max": 31, "step": 1,
    "modes": { "cold": "Frío", "wet": "Seco", "wind": "Ventilador", "hot": "Calor", "auto": "Auto" },
    "fans": { "low": "Baja", "mid": "Media", "high": "Alta", "auto": "Auto" },
    "toggles": { "15": "Oscilación", "101": "Sueño" }
  }
])json"
