// Aires acondicionados Tuya controlados por red local (lista AC_DEVICES_JSON en secrets.h).
// Una tarea en segundo plano los consulta y les manda las ordenes, asi el servidor web
// y el ventilador no se bloquean mientras el aparato responde.
#pragma once
#include <Arduino.h>

void acSetup();
int acCount();

// {"devices":[{name, online, error, power, setpoint, temp, mode, fan, min, max, modes, fans,
//              toggles: [{dp, name, on}]}]}
// Consultarlo mantiene activo el sondeo de los aparatos durante un rato.
String acStateJson();

// what: "power" (v=0/1), "temp" (grados), "mode" o "fan" (uno de los valores configurados),
// "toggle" (v="<dp>:<0|1>", uno de los "toggles" configurados).
// Se aplica en segundo plano; el estado devuelto ya refleja el cambio.
bool acCommand(int device, const String &what, const String &value, String &error);

// DPS tal cual los devuelve el aparato, para averiguar que es cada uno
String acRawJson(int device);
