using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace Vento
{
    public class TuyaException : Exception
    {
        public TuyaException(string message) : base(message) { }
    }

    // Protocolo local de Tuya (3.3, 3.4 y 3.5) por TCP 6668, el mismo que usa tinytuya.
    // Cada operación abre una conexión, negocia la clave de sesión si hace falta y la cierra,
    // así el aparato queda libre para Vento (ESP32) u otras apps.
    //  3.3 / 3.4: 000055AA | seq | cmd | len | payload | CRC32 (3.3) o HMAC-SHA256 (3.4) | 0000AA55
    //  3.5:       00006699 | 0000 | seq | cmd | len | IV(12) | payload AES-GCM | tag(16) | 00009966
    public sealed class TuyaClient
    {
        private const int Port = 6668;
        private const int MaxMessage = 4096;
        private static readonly TimeSpan ConnectTimeout = TimeSpan.FromSeconds(3);
        private static readonly TimeSpan ReadTimeout = TimeSpan.FromSeconds(3);

        private const uint SessKeyNegStart = 3, SessKeyNegResp = 4, SessKeyNegFinish = 5;
        private const uint Control = 7, HeartBeat = 9, DpQuery = 10, ControlNew = 13, DpQueryNew = 16;
        private const uint UpdateDps = 18, LanExtStream = 64;

        private readonly string _id, _ip;
        private readonly byte[] _localKey;
        private int _version;   // 33, 34 o 35
        private readonly bool _autoVersion;   // se creó con 0: se averigua probando
        private bool _detected, _reached;
        private uint _seq;
        private bool _device22;   // 3.3 que responde "data unvalid" a DP_QUERY (como en tinytuya)

        public TuyaClient(string id, string localKey, string ip, int version)
        {
            _id = id;
            _ip = ip;
            _localKey = Encoding.UTF8.GetBytes(localKey);
            if (_localKey.Length != 16) throw new TuyaException("La local_key debe tener 16 caracteres");
            _autoVersion = version == 0;
            _version = _autoVersion ? 33 : version;
        }

        // 0 mientras no se sepa (versión automática sin detectar todavía)
        public int Version => _autoVersion && !_detected ? 0 : _version;

        // Estado de los DPS: {"1":true,"2":24,...}. requestDps: los DPS que se piden a los
        // aparatos 3.3 que no aceptan la consulta normal.
        public async Task<JsonElement> QueryAsync(IEnumerable<int> requestDps)
        {
            if (!_autoVersion || _detected) return await QueryOnceAsync(requestDps);
            foreach (var v in new[] { 33, 34, 35 })
            {
                _version = v;
                _device22 = false;
                try
                {
                    var dps = await QueryOnceAsync(requestDps);
                    _detected = true;
                    return dps;
                }
                catch (TuyaException) when (_reached) { }   // conectó pero no se entienden: otra versión
            }
            _version = 33;
            throw new TuyaException("No se entiende con el aparato en 3.3, 3.4 ni 3.5 (revisa la local_key)");
        }

        private async Task<JsonElement> QueryOnceAsync(IEnumerable<int> requestDps)
        {
            using var s = await OpenAsync();
            if (_version >= 34)
            {
                await SendAsync(s, DpQueryNew, Encoding.UTF8.GetBytes("{}"));
                return await ReceiveDpsAsync(s);
            }
            if (!_device22)
            {
                await SendAsync(s, DpQuery, JsonSerializer.SerializeToUtf8Bytes(new
                {
                    gwId = _id, devId = _id, uid = _id, t = Now().ToString(),
                }));
                try { return await ReceiveDpsAsync(s); }
                catch (DataUnvalidException) { _device22 = true; }
            }
            // Estos aparatos devuelven el estado de los DPS que se piden con valor null
            await SendAsync(s, ControlNew, JsonSerializer.SerializeToUtf8Bytes(new
            {
                devId = _id, uid = _id, t = Now().ToString(),
                dps = requestDps.Where(dp => dp > 0).ToDictionary(dp => dp.ToString(), dp => (object)null),
            }));
            try { return await ReceiveDpsAsync(s); }
            catch (DataUnvalidException e) { throw new TuyaException(e.Message); }
        }

        private sealed class DataUnvalidException : TuyaException
        {
            public DataUnvalidException() : base("El aparato no acepta la consulta de estado") { }
        }

        private async Task<JsonElement> ReceiveDpsAsync(Session s)
        {
            string error = null;
            for (int i = 0; i < 4; i++)
            {
                var (_, payload) = await ReceiveAsync(s);
                if (payload.Length == 0) continue;
                if (Encoding.UTF8.GetString(payload).Contains("data unvalid")) throw new DataUnvalidException();
                try
                {
                    using var doc = JsonDocument.Parse(payload);
                    var root = doc.RootElement;
                    if (root.TryGetProperty("dps", out var dps) ||
                        (root.TryGetProperty("data", out var data) && data.ValueKind == JsonValueKind.Object &&
                         data.TryGetProperty("dps", out dps)))
                        return dps.Clone();
                }
                catch (JsonException)
                {
                    var text = Encoding.UTF8.GetString(payload);
                    error = "Respuesta del aparato: " + (text.Length > 60 ? text.Substring(0, 60) : text);
                }
            }
            throw new TuyaException(error ?? "El aparato no envió su estado");
        }

        // dps: los datos a cambiar, p. ej. {"1": false}
        public async Task SetAsync(IDictionary<string, object> dps)
        {
            // Antes de mandar una orden hay que saber qué versión habla
            if (_autoVersion && !_detected) await QueryAsync(new[] { 1 });
            using var s = await OpenAsync();
            if (_version >= 34)
                await SendAsync(s, ControlNew, JsonSerializer.SerializeToUtf8Bytes(new
                {
                    protocol = 5, t = Now(), data = new { dps },
                }));
            else
                await SendAsync(s, Control, JsonSerializer.SerializeToUtf8Bytes(new
                {
                    devId = _id, uid = _id, t = Now().ToString(), dps,
                }));
            // El aparato confirma con un mensaje (a veces vacío)
            await ReceiveAsync(s);
        }

        private static long Now() => DateTimeOffset.UtcNow.ToUnixTimeSeconds();

        // ------------------------------------------------------------ conexión
        private sealed class Session : IDisposable
        {
            public TcpClient Tcp;
            public NetworkStream Stream;
            public byte[] Key;
            public void Dispose() => Tcp.Dispose();
        }

        private async Task<Session> OpenAsync()
        {
            _reached = false;
            var tcp = new TcpClient { NoDelay = true };
            try
            {
                using (var cts = new CancellationTokenSource(ConnectTimeout))
                    await tcp.ConnectAsync(_ip, Port, cts.Token);
            }
            catch (Exception)
            {
                tcp.Dispose();
                throw new TuyaException("No responde en " + _ip);
            }
            _reached = true;
            var s = new Session { Tcp = tcp, Stream = tcp.GetStream(), Key = _localKey };
            try
            {
                if (_version >= 34) await NegotiateAsync(s);
                return s;
            }
            catch
            {
                s.Dispose();
                throw;
            }
        }

        // -> START(nonce local)   <- RESP(nonce remoto + HMAC(local_key, nonce local))
        // -> FINISH(HMAC(local_key, nonce remoto))
        // clave de sesión = cifrado con la local_key de (nonce local XOR nonce remoto)
        private async Task NegotiateAsync(Session s)
        {
            var localNonce = RandomNumberGenerator.GetBytes(16);
            await SendAsync(s, SessKeyNegStart, localNonce);

            uint cmd = 0;
            byte[] p = Array.Empty<byte>();
            for (int i = 0; i < 3 && cmd != SessKeyNegResp; i++)
                (cmd, p) = await ReceiveAsync(s);
            if (cmd != SessKeyNegResp || p.Length < 48)
                throw new TuyaException("El aparato rechazó la conexión (revisa la versión y la local_key)");

            var remoteNonce = p.AsSpan(0, 16).ToArray();
            if (!HMACSHA256.HashData(_localKey, localNonce).AsSpan().SequenceEqual(p.AsSpan(16, 32)))
                throw new TuyaException("local_key incorrecta");

            await SendAsync(s, SessKeyNegFinish, HMACSHA256.HashData(_localKey, remoteNonce));

            var mixed = new byte[16];
            for (int i = 0; i < 16; i++) mixed[i] = (byte)(localNonce[i] ^ remoteNonce[i]);
            if (_version >= 35)
            {
                var key = new byte[16];
                using var gcm = new AesGcm(_localKey, 16);
                gcm.Encrypt(localNonce.AsSpan(0, 12), mixed, key, new byte[16]);
                s.Key = key;
            }
            else
            {
                using var aes = Aes.Create();
                aes.Key = _localKey;
                s.Key = aes.EncryptEcb(mixed, PaddingMode.None);
            }
        }

        // ------------------------------------------------------------ mensajes
        private static bool NoProtocolHeader(uint cmd) =>
            cmd is DpQuery or DpQueryNew or UpdateDps or HeartBeat or
                   SessKeyNegStart or SessKeyNegResp or SessKeyNegFinish or LanExtStream;

        private async Task SendAsync(Session s, uint cmd, byte[] payload)
        {
            // Cabecera de versión: "3.x" + 12 ceros
            var header = NoProtocolHeader(cmd)
                ? Array.Empty<byte>()
                : Encoding.ASCII.GetBytes(_version / 10 + "." + _version % 10).Concat(new byte[12]).ToArray();

            var frame = new List<byte>();
            if (_version >= 35)
            {
                var plain = header.Concat(payload).ToArray();
                PutBe32(frame, 0x00006699);
                frame.Add(0);
                frame.Add(0);
                PutBe32(frame, ++_seq);
                PutBe32(frame, cmd);
                PutBe32(frame, (uint)(12 + plain.Length + 16));
                var iv = RandomNumberGenerator.GetBytes(12);
                var enc = new byte[plain.Length];
                var tag = new byte[16];
                using (var gcm = new AesGcm(s.Key, 16))
                    gcm.Encrypt(iv, plain, enc, tag, frame.Skip(4).ToArray());
                frame.AddRange(iv);
                frame.AddRange(enc);
                frame.AddRange(tag);
                PutBe32(frame, 0x00009966);
            }
            else
            {
                byte[] body;
                if (_version == 34)
                    body = EcbEncrypt(s.Key, header.Concat(payload).ToArray());   // cabecera dentro del cifrado
                else
                    body = header.Concat(EcbEncrypt(s.Key, payload)).ToArray();   // cabecera delante
                int trailer = (_version == 34 ? 32 : 4) + 4;
                PutBe32(frame, 0x000055AA);
                PutBe32(frame, ++_seq);
                PutBe32(frame, cmd);
                PutBe32(frame, (uint)(body.Length + trailer));
                frame.AddRange(body);
                if (_version == 34) frame.AddRange(HMACSHA256.HashData(s.Key, frame.ToArray()));
                else PutBe32(frame, Crc32(frame.ToArray()));
                PutBe32(frame, 0x0000AA55);
            }

            try { await s.Stream.WriteAsync(frame.ToArray()); }
            catch (Exception) { throw new TuyaException("Error enviando al aparato"); }
        }

        private async Task<(uint cmd, byte[] payload)> ReceiveAsync(Session s)
        {
            var prefix = await ReadAsync(s, 4);
            switch (Be32(prefix, 0))
            {
                case 0x000055AA:
                {
                    var hdr = await ReadAsync(s, 12);
                    uint cmd = Be32(hdr, 4);
                    int len = (int)Be32(hdr, 8);
                    int trailer = (_version >= 34 ? 32 : 4) + 4;
                    if (len < trailer || len > MaxMessage) throw new TuyaException("Respuesta no válida");
                    var body = (await ReadAsync(s, len)).AsSpan(0, len - trailer).ToArray();

                    // Los mensajes del aparato suelen empezar por un código de retorno de 4 bytes
                    int off = 0;
                    if (_version >= 34)
                    {
                        if (body.Length % 16 == 4) off = 4;
                    }
                    else
                    {
                        if (StartsWithRetcode(body, 0)) off = 4;
                        if (StartsWithVersion(body, off)) off += 15;
                    }
                    int n = body.Length - off;
                    if (n == 0) return (cmd, Array.Empty<byte>());
                    if (_version < 34 && (n % 16 != 0 || body[off] == '{'))
                        return (cmd, body.AsSpan(off).ToArray());   // sin cifrar (p. ej. errores)

                    byte[] plain;
                    try
                    {
                        using var aes = Aes.Create();
                        aes.Key = s.Key;
                        plain = aes.DecryptEcb(body.AsSpan(off), PaddingMode.PKCS7);
                    }
                    catch (CryptographicException)
                    {
                        throw new TuyaException("No se pudo descifrar la respuesta (revisa la local_key y la versión)");
                    }
                    if (StartsWithVersion(plain, 0)) plain = plain.AsSpan(15).ToArray();
                    return (cmd, plain);
                }
                case 0x00006699:
                {
                    var hdr = await ReadAsync(s, 14);
                    uint cmd = Be32(hdr, 6);
                    int len = (int)Be32(hdr, 10);
                    if (len < 28 || len > MaxMessage) throw new TuyaException("Respuesta no válida");
                    var body = await ReadAsync(s, len);
                    // El sufijo 00009966 va después de la longitud indicada
                    if (Be32(body, len - 4) == 0x00009966) len -= 4;
                    else await ReadAsync(s, 4);

                    var plain = new byte[len - 28];
                    try
                    {
                        using var gcm = new AesGcm(s.Key, 16);
                        gcm.Decrypt(body.AsSpan(0, 12), body.AsSpan(12, len - 28), body.AsSpan(len - 16, 16), plain, hdr);
                    }
                    catch (CryptographicException)
                    {
                        throw new TuyaException("No se pudo descifrar la respuesta (revisa la local_key y la versión)");
                    }
                    int off = StartsWithRetcode(plain, 0) ? 4 : 0;
                    if (StartsWithVersion(plain, off)) off += 15;
                    return (cmd, plain.AsSpan(off).ToArray());
                }
                default:
                    throw new TuyaException("Respuesta no válida");
            }
        }

        private static async Task<byte[]> ReadAsync(Session s, int count)
        {
            var buf = new byte[count];
            try
            {
                using var cts = new CancellationTokenSource(ReadTimeout);
                await s.Stream.ReadExactlyAsync(buf, cts.Token);
            }
            catch (OperationCanceledException)
            {
                throw new TuyaException("Sin respuesta del aparato (¿local_key o versión incorrecta?)");
            }
            catch (Exception)
            {
                throw new TuyaException("El aparato cortó la conexión (¿local_key o versión incorrecta, u otra app conectada?)");
            }
            return buf;
        }

        // ------------------------------------------------------------ utilidades
        private static byte[] EcbEncrypt(byte[] key, byte[] data)
        {
            using var aes = Aes.Create();
            aes.Key = key;
            return aes.EncryptEcb(data, PaddingMode.PKCS7);
        }

        private static bool StartsWithVersion(byte[] p, int off) =>
            p.Length >= off + 15 && p[off] == '3' && p[off + 1] == '.';

        private static bool StartsWithRetcode(byte[] p, int off) =>
            p.Length >= off + 4 && p[off] == 0 && p[off + 1] == 0 && p[off + 2] == 0;

        private static uint Be32(byte[] p, int off) =>
            (uint)(p[off] << 24 | p[off + 1] << 16 | p[off + 2] << 8 | p[off + 3]);

        private static void PutBe32(List<byte> v, uint x)
        {
            v.Add((byte)(x >> 24));
            v.Add((byte)(x >> 16));
            v.Add((byte)(x >> 8));
            v.Add((byte)x);
        }

        private static uint Crc32(byte[] data)
        {
            uint crc = 0xFFFFFFFF;
            foreach (var b in data)
            {
                crc ^= b;
                for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
            }
            return ~crc;
        }
    }
}
