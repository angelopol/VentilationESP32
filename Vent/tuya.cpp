#include "tuya.h"
#include <ArduinoJson.h>
#include <time.h>
#include "mbedtls/aes.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md.h"

// Formato de los mensajes (el mismo que usa tinytuya):
//  3.3 / 3.4: 000055AA | seq | cmd | len | payload | CRC32 (3.3) o HMAC-SHA256 (3.4) | 0000AA55
//  3.5:       00006699 | 0000 | seq | cmd | len | IV(12) | payload AES-GCM | tag(16) | 00009966
// 3.3 cifra con AES-ECB y la local_key; 3.4 y 3.5 negocian antes una clave de sesion.

static const uint16_t TUYA_PORT = 6668;
// Con la radio compartida con el Bluetooth un paquete perdido cuesta segundos (el primer
// reintento de TCP llega a los 3 s): margen suficiente para no dar al aire por perdido
static const uint32_t CONNECT_TIMEOUT_MS = 5000;
static const uint32_t READ_TIMEOUT_MS = 4000;
static const size_t MAX_MESSAGE = 4096;

enum : uint32_t {
  CMD_SESS_KEY_NEG_START = 3,
  CMD_SESS_KEY_NEG_RESP = 4,
  CMD_SESS_KEY_NEG_FINISH = 5,
  CMD_CONTROL = 7,
  CMD_HEART_BEAT = 9,
  CMD_DP_QUERY = 10,
  CMD_CONTROL_NEW = 13,
  CMD_DP_QUERY_NEW = 16,
  CMD_UPDATEDPS = 18,
  CMD_LAN_EXT_STREAM = 64,
};

// ---------------------------------------------------------------- utilidades

static uint32_t be32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void putBe32(std::vector<uint8_t> &v, uint32_t x)
{
  v.push_back(x >> 24);
  v.push_back(x >> 16);
  v.push_back(x >> 8);
  v.push_back(x);
}

static uint32_t crc32(const uint8_t *data, size_t len)
{
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
  }
  return ~crc;
}

static void randomBytes(uint8_t *buf, size_t len)
{
  for (size_t i = 0; i < len; i += 4) {
    uint32_t r = esp_random();
    for (size_t k = 0; k < 4 && i + k < len; k++) buf[i + k] = r >> (8 * k);
  }
}

static void hmacSha256(const uint8_t *key, const uint8_t *data, size_t len, uint8_t out[32])
{
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, 16, data, len, out);
}

static std::vector<uint8_t> ecbEncrypt(const uint8_t *key, const uint8_t *data, size_t len)
{
  // Relleno PKCS#7 (siempre anade al menos un byte)
  size_t pad = 16 - len % 16;
  std::vector<uint8_t> out(data, data + len);
  out.insert(out.end(), pad, (uint8_t)pad);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 128);
  for (size_t i = 0; i < out.size(); i += 16)
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, &out[i], &out[i]);
  mbedtls_aes_free(&aes);
  return out;
}

static bool ecbDecrypt(const uint8_t *key, const uint8_t *data, size_t len, std::vector<uint8_t> &out)
{
  if (len == 0 || len % 16) return false;
  out.assign(data, data + len);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, key, 128);
  for (size_t i = 0; i < len; i += 16)
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, &out[i], &out[i]);
  mbedtls_aes_free(&aes);
  uint8_t pad = out.back();
  if (pad == 0 || pad > 16) return false;
  for (size_t i = len - pad; i < len; i++) if (out[i] != pad) return false;
  out.resize(len - pad);
  return true;
}

static bool gcm(bool encrypt, const uint8_t *key, const uint8_t *iv, const uint8_t *aad, size_t aadLen,
                const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag)
{
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  int r = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
  if (r == 0) {
    r = encrypt
      ? mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, len, iv, 12, aad, aadLen, in, out, 16, tag)
      : mbedtls_gcm_auth_decrypt(&ctx, len, iv, 12, aad, aadLen, tag, 16, in, out);
  }
  mbedtls_gcm_free(&ctx);
  return r == 0;
}

// Comandos que no llevan la cabecera de version ("3.x" + 12 ceros) delante del payload
static bool noProtocolHeader(uint32_t cmd)
{
  return cmd == CMD_DP_QUERY || cmd == CMD_DP_QUERY_NEW || cmd == CMD_UPDATEDPS || cmd == CMD_HEART_BEAT ||
         cmd == CMD_SESS_KEY_NEG_START || cmd == CMD_SESS_KEY_NEG_RESP || cmd == CMD_SESS_KEY_NEG_FINISH ||
         cmd == CMD_LAN_EXT_STREAM;
}

static bool startsWithVersion(const std::vector<uint8_t> &p, size_t off)
{
  return p.size() >= off + 15 && p[off] == '3' && p[off + 1] == '.';
}

static bool startsWithRetcode(const std::vector<uint8_t> &p, size_t off)
{
  return p.size() >= off + 4 && p[off] == 0 && p[off + 1] == 0 && p[off + 2] == 0;
}

// ---------------------------------------------------------------- API

void TuyaClient::begin(const String &id, const String &localKey, const String &ip, int version)
{
  _id = id;
  _ip = ip;
  _autoVersion = version == 0;
  _version = _autoVersion ? 33 : version;
  memset(_localKey, 0, sizeof(_localKey));
  memcpy(_localKey, localKey.c_str(), min((size_t)16, (size_t)localKey.length()));
}

bool TuyaClient::query(String &dpsJson, const String &requestDps)
{
  if (!_autoVersion || _detected) return queryOnce(dpsJson, requestDps);
  for (int v : {33, 34, 35}) {
    _version = v;
    _device22 = false;
    if (queryOnce(dpsJson, requestDps)) {
      _detected = true;
      return true;
    }
    if (!_reached) return false;   // no responde: no es cuestion de version
  }
  _version = 33;
  lastError = "No se entiende con el aparato en 3.3, 3.4 ni 3.5 (revisa la local_key)";
  return false;
}

bool TuyaClient::queryOnce(String &dpsJson, const String &requestDps)
{
  if (!open()) return false;
  bool ok;
  if (_version >= 34) {
    ok = send(CMD_DP_QUERY_NEW, String("{}")) && receiveDps(dpsJson);
  } else {
    if (!_device22) {
      ok = send(CMD_DP_QUERY, "{\"gwId\":\"" + _id + "\",\"devId\":\"" + _id + "\",\"uid\":\"" + _id +
                              "\",\"t\":\"" + timestamp() + "\"}") && receiveDps(dpsJson);
      if (!ok && _dataUnvalid) _device22 = true;
    }
    if (_device22) {
      // Estos aparatos devuelven el estado de los DPS que se piden con valor null
      ok = send(CMD_CONTROL_NEW, "{\"devId\":\"" + _id + "\",\"uid\":\"" + _id + "\",\"t\":\"" + timestamp() +
                                 "\",\"dps\":" + requestDps + "}") && receiveDps(dpsJson);
    }
  }
  close();
  return ok;
}

bool TuyaClient::set(const String &dpsJson)
{
  if (_autoVersion && !_detected) {
    // Antes de mandar una orden hay que saber que version habla
    String ignored;
    if (!query(ignored, "{\"1\":null}")) return false;
  }
  if (!open()) return false;
  bool ok;
  if (_version >= 34) {
    ok = send(CMD_CONTROL_NEW, "{\"protocol\":5,\"t\":" + timestamp() + ",\"data\":{\"dps\":" + dpsJson + "}}");
  } else {
    ok = send(CMD_CONTROL, "{\"devId\":\"" + _id + "\",\"uid\":\"" + _id + "\",\"t\":\"" + timestamp() +
                       "\",\"dps\":" + dpsJson + "}");
  }
  // El aparato confirma con un mensaje (a veces vacio); el estado nuevo se consulta despues
  uint32_t cmd;
  Bytes payload;
  ok = ok && receive(cmd, payload);
  close();
  return ok;
}

// ---------------------------------------------------------------- conexion

bool TuyaClient::open()
{
  lastError = "";
  _key = _localKey;
  _reached = false;
  IPAddress addr;
  if (!addr.fromString(_ip)) { lastError = "IP no valida"; return false; }
  if (!_client.connect(addr, TUYA_PORT, CONNECT_TIMEOUT_MS)) {
    lastError = "No responde en " + _ip;
    return false;
  }
  _client.setNoDelay(true);
  _reached = true;
  if (_version >= 34 && !negotiate()) { close(); return false; }
  return true;
}

void TuyaClient::close()
{
  _client.stop();
  _key = _localKey;
}

// Negociacion de la clave de sesion (3.4 y 3.5):
//  -> START(nonce local)   <- RESP(nonce remoto + HMAC(local_key, nonce local))
//  -> FINISH(HMAC(local_key, nonce remoto))
//  clave de sesion = cifrado con la local_key de (nonce local XOR nonce remoto)
bool TuyaClient::negotiate()
{
  uint8_t localNonce[16], remoteNonce[16], mac[32];
  randomBytes(localNonce, sizeof(localNonce));
  if (!send(CMD_SESS_KEY_NEG_START, localNonce, sizeof(localNonce))) return false;

  uint32_t cmd = 0;
  Bytes p;
  for (int i = 0; i < 3 && cmd != CMD_SESS_KEY_NEG_RESP; i++) {
    if (!receive(cmd, p)) return false;
  }
  if (cmd != CMD_SESS_KEY_NEG_RESP || p.size() < 48) {
    lastError = "El aparato rechazo la conexion (revisa la version y la local_key)";
    return false;
  }
  memcpy(remoteNonce, p.data(), 16);
  hmacSha256(_localKey, localNonce, 16, mac);
  if (memcmp(mac, p.data() + 16, 32) != 0) {
    lastError = "local_key incorrecta";
    return false;
  }

  hmacSha256(_localKey, remoteNonce, 16, mac);
  if (!send(CMD_SESS_KEY_NEG_FINISH, mac, sizeof(mac))) return false;

  uint8_t mixed[16];
  for (int i = 0; i < 16; i++) mixed[i] = localNonce[i] ^ remoteNonce[i];
  if (_version >= 35) {
    uint8_t tag[16];
    if (!gcm(true, _localKey, localNonce, nullptr, 0, mixed, 16, _sessionKey, tag)) {
      lastError = "Error de cifrado";
      return false;
    }
  } else {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, _localKey, 128);
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, mixed, _sessionKey);
    mbedtls_aes_free(&aes);
  }
  _key = _sessionKey;
  return true;
}

// ---------------------------------------------------------------- mensajes

bool TuyaClient::send(uint32_t cmd, const String &payload)
{
  return send(cmd, (const uint8_t *)payload.c_str(), payload.length());
}

bool TuyaClient::send(uint32_t cmd, const uint8_t *payload, size_t len)
{
  Bytes header;   // "3.x" + 12 ceros
  if (!noProtocolHeader(cmd)) {
    header.push_back('3');
    header.push_back('.');
    header.push_back('0' + _version % 10);
    header.insert(header.end(), 12, 0);
  }

  Bytes frame;
  if (_version >= 35) {
    Bytes plain(header);
    plain.insert(plain.end(), payload, payload + len);
    putBe32(frame, 0x00006699);
    frame.push_back(0);
    frame.push_back(0);
    putBe32(frame, ++_seq);
    putBe32(frame, cmd);
    putBe32(frame, 12 + plain.size() + 16);
    uint8_t iv[12], tag[16];
    randomBytes(iv, sizeof(iv));
    Bytes enc(plain.size());
    if (!gcm(true, _key, iv, &frame[4], 14, plain.data(), plain.size(), enc.data(), tag)) {
      lastError = "Error de cifrado";
      return false;
    }
    frame.insert(frame.end(), iv, iv + 12);
    frame.insert(frame.end(), enc.begin(), enc.end());
    frame.insert(frame.end(), tag, tag + 16);
    putBe32(frame, 0x00009966);
  } else {
    Bytes body;
    if (_version == 34) {
      // 3.4: la cabecera de version va dentro del cifrado
      Bytes plain(header);
      plain.insert(plain.end(), payload, payload + len);
      body = ecbEncrypt(_key, plain.data(), plain.size());
    } else {
      // 3.3: la cabecera de version va delante del payload cifrado
      Bytes enc = ecbEncrypt(_key, payload, len);
      body = header;
      body.insert(body.end(), enc.begin(), enc.end());
    }
    size_t trailer = (_version == 34 ? 32 : 4) + 4;
    putBe32(frame, 0x000055AA);
    putBe32(frame, ++_seq);
    putBe32(frame, cmd);
    putBe32(frame, body.size() + trailer);
    frame.insert(frame.end(), body.begin(), body.end());
    if (_version == 34) {
      uint8_t mac[32];
      hmacSha256(_key, frame.data(), frame.size(), mac);
      frame.insert(frame.end(), mac, mac + 32);
    } else {
      putBe32(frame, crc32(frame.data(), frame.size()));
    }
    putBe32(frame, 0x0000AA55);
  }

  if (_client.write(frame.data(), frame.size()) != frame.size()) {
    lastError = "Error enviando al aparato";
    return false;
  }
  return true;
}

bool TuyaClient::receive(uint32_t &cmd, Bytes &out)
{
  uint8_t hdr[18];
  if (!readExact(hdr, 4)) return false;
  uint32_t prefix = be32(hdr);
  out.clear();

  if (prefix == 0x000055AA) {
    if (!readExact(hdr + 4, 12)) return false;
    cmd = be32(hdr + 8);
    uint32_t len = be32(hdr + 12);
    size_t trailer = (_version >= 34 ? 32 : 4) + 4;
    if (len < trailer || len > MAX_MESSAGE) { lastError = "Respuesta no valida"; return false; }
    Bytes body(len);
    if (!readExact(body.data(), len)) return false;
    body.resize(len - trailer);

    // Los mensajes del aparato suelen empezar por un codigo de retorno de 4 bytes
    size_t off = 0;
    if (_version >= 34) {
      if (body.size() % 16 == 4) off = 4;
    } else {
      if (startsWithRetcode(body, 0)) off = 4;
      if (startsWithVersion(body, off)) off += 15;
    }
    size_t n = body.size() - off;
    if (n == 0) return true;
    if (_version < 34 && (n % 16 != 0 || body[off] == '{')) {
      out.assign(body.begin() + off, body.end());   // sin cifrar (p. ej. mensajes de error)
      return true;
    }
    if (!ecbDecrypt(_key, body.data() + off, n, out)) {
      lastError = "No se pudo descifrar la respuesta (revisa la local_key y la version)";
      return false;
    }
    if (startsWithVersion(out, 0)) out.erase(out.begin(), out.begin() + 15);
    return true;
  }

  if (prefix == 0x00006699) {
    if (!readExact(hdr + 4, 14)) return false;
    cmd = be32(hdr + 10);
    uint32_t len = be32(hdr + 14);
    if (len < 28 || len > MAX_MESSAGE) { lastError = "Respuesta no valida"; return false; }
    Bytes body(len);
    if (!readExact(body.data(), len)) return false;
    // El sufijo 00009966 va despues de la longitud indicada
    if (len >= 32 && be32(&body[len - 4]) == 0x00009966) body.resize(len -= 4);
    else {
      uint8_t suffix[4];
      if (!readExact(suffix, 4)) return false;
    }
    size_t n = len - 28;
    Bytes plain(n);
    if (!gcm(false, _key, body.data(), hdr + 4, 14, body.data() + 12, n, plain.data(), &body[len - 16])) {
      lastError = "No se pudo descifrar la respuesta (revisa la local_key y la version)";
      return false;
    }
    size_t off = startsWithRetcode(plain, 0) ? 4 : 0;
    if (startsWithVersion(plain, off)) off += 15;
    out.assign(plain.begin() + off, plain.end());
    return true;
  }

  lastError = "Respuesta no valida";
  return false;
}

bool TuyaClient::receiveDps(String &dpsJson)
{
  _dataUnvalid = false;
  for (int i = 0; i < 4; i++) {
    uint32_t cmd;
    Bytes p;
    if (!receive(cmd, p)) return false;
    if (p.empty()) continue;
    String text((const char *)p.data(), p.size());
    if (text.indexOf("data unvalid") >= 0) {
      _dataUnvalid = true;
      lastError = "El aparato no acepta la consulta de estado";
      return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, text)) {
      lastError = "Respuesta del aparato: " + text.substring(0, 60);
      continue;
    }
    JsonObject dps = doc["dps"];
    if (dps.isNull()) dps = doc["data"]["dps"];
    if (dps.isNull()) continue;
    dpsJson = "";
    serializeJson(dps, dpsJson);
    return true;
  }
  if (lastError.isEmpty()) lastError = "El aparato no envio su estado";
  return false;
}

bool TuyaClient::readExact(uint8_t *buf, size_t len)
{
  unsigned long start = millis();
  size_t got = 0;
  while (got < len) {
    int n = _client.read(buf + got, len - got);
    if (n > 0) {
      got += n;
      continue;
    }
    if (!_client.connected() && !_client.available()) {
      lastError = "El aparato corto la conexion (¿local_key o version incorrecta, u otra app conectada?)";
      return false;
    }
    if (millis() - start > READ_TIMEOUT_MS) {
      lastError = "Sin respuesta del aparato (¿local_key o version incorrecta?)";
      return false;
    }
    delay(5);
  }
  return true;
}

String TuyaClient::timestamp()
{
  return String((unsigned long)time(nullptr));
}
