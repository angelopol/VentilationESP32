// Cliente del protocolo local de Tuya (versiones 3.3, 3.4 y 3.5) por TCP, puerto 6668.
// Cada operacion abre una conexion, negocia la clave de sesion si la version lo pide
// (3.4 y 3.5) y la cierra: asi el aparato queda libre para la app de Windows u otros.
#pragma once
#include <Arduino.h>
#include <WiFiClient.h>
#include <vector>

class TuyaClient
{
public:
  // version: 33, 34 o 35; 0 = averiguarla probando las tres la primera vez
  void begin(const String &id, const String &localKey, const String &ip, int version);
  int version() const { return _autoVersion && !_detected ? 0 : _version; }

  // Estado de los DPS como objeto JSON ({"1":true,"2":24,...}). requestDps ({"1":null,...})
  // se usa con los aparatos 3.3 que no aceptan la consulta normal ("device22" en tinytuya).
  bool query(String &dpsJson, const String &requestDps);
  // dpsJson: los DPS a cambiar, p. ej. {"1":false}
  bool set(const String &dpsJson);

  String lastError;

private:
  typedef std::vector<uint8_t> Bytes;

  String _id, _ip;
  uint8_t _localKey[16];
  uint8_t _sessionKey[16];
  const uint8_t *_key = _localKey;   // clave activa: la local hasta negociar la de sesion
  int _version = 33;
  uint32_t _seq = 0;
  bool _autoVersion = false, _detected = false;
  bool _reached = false;      // la ultima conexion TCP llego a abrirse
  bool _device22 = false;     // responde "data unvalid" a DP_QUERY
  bool _dataUnvalid = false;
  WiFiClient _client;

  bool queryOnce(String &dpsJson, const String &requestDps);
  bool open();
  void close();
  bool negotiate();
  bool send(uint32_t cmd, const String &payload);
  bool send(uint32_t cmd, const uint8_t *payload, size_t len);
  bool receive(uint32_t &cmd, Bytes &payload);
  // Lee mensajes hasta uno con JSON que traiga "dps" (o se acabe el tiempo)
  bool receiveDps(String &dpsJson);
  bool readExact(uint8_t *buf, size_t len);
  String timestamp();
};
