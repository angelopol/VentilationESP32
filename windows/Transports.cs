using System;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32;

namespace Vento
{
    public interface ITransport : IDisposable
    {
        Task<VentoState> GetStateAsync();
        Task<VentoState> SetModeAsync(int mode);
        Task<VentoState> SetSetpointAsync(int degrees);
    }

    // ------------------------------------------------------------------ WiFi
    public sealed class HttpTransport : ITransport
    {
        private readonly HttpClient _http;

        public HttpTransport(string host)
        {
            _http = new HttpClient
            {
                BaseAddress = new Uri("http://" + host + ".local"),
                Timeout = TimeSpan.FromSeconds(4),
            };
        }

        public async Task<VentoState> GetStateAsync() =>
            VentoState.Parse(await _http.GetStringAsync("/api/state"));

        public Task<VentoState> SetModeAsync(int mode) => PostAsync("/api/mode?v=" + mode);

        public Task<VentoState> SetSetpointAsync(int degrees) => PostAsync("/api/setpoint?v=" + degrees);

        private async Task<VentoState> PostAsync(string path)
        {
            using var r = await _http.PostAsync(path, null);
            r.EnsureSuccessStatusCode();
            return VentoState.Parse(await r.Content.ReadAsStringAsync());
        }

        public void Dispose() => _http.Dispose();
    }

    // ------------------------------------------------------------- Bluetooth
    // Bluetooth Classic (SPP) con el protocolo de un carácter del firmware:
    // '0'-'7' modo, 'a'-'s' temperatura 16..70, '?' responde con el JSON de estado.
    public sealed class BluetoothTransport : ITransport
    {
        private static readonly Guid SerialPortService = new Guid("00001101-0000-1000-8000-00805F9B34FB");
        private const AddressFamily AF_BTH = (AddressFamily)32;
        private const ProtocolType BTHPROTO_RFCOMM = (ProtocolType)3;

        private readonly Socket _sock;
        private readonly SemaphoreSlim _lock = new SemaphoreSlim(1, 1);
        private TaskCompletionSource<VentoState> _pending;
        private volatile bool _closed;

        private BluetoothTransport()
        {
            _sock = new Socket(AF_BTH, SocketType.Stream, BTHPROTO_RFCOMM);
        }

        public static async Task<BluetoothTransport> ConnectAsync(ulong address, TimeSpan timeout)
        {
            var t = new BluetoothTransport();
            var connect = Task.Run(() => t._sock.Connect(new BluetoothEndPoint(address, SerialPortService)));
            if (await Task.WhenAny(connect, Task.Delay(timeout)) != connect)
            {
                t.Dispose(); // cierra el socket y aborta el Connect
                throw new TimeoutException("Bluetooth: tiempo de conexión agotado");
            }
            try { await connect; }
            catch { t.Dispose(); throw; }

            new Thread(t.ReadLoop) { IsBackground = true, Name = "Vento BT" }.Start();
            return t;
        }

        public Task<VentoState> GetStateAsync() => SendAsync("?");

        public Task<VentoState> SetModeAsync(int mode) => SendAsync(mode + "?");

        public Task<VentoState> SetSetpointAsync(int degrees)
        {
            int step = (int)Math.Round((Math.Clamp(degrees, 16, 70) - 16) / 3.0);
            return SendAsync((char)('a' + step) + "?");
        }

        private async Task<VentoState> SendAsync(string command)
        {
            await _lock.WaitAsync();
            try
            {
                if (_closed) throw new IOException("Bluetooth desconectado");
                var tcs = new TaskCompletionSource<VentoState>(TaskCreationOptions.RunContinuationsAsynchronously);
                _pending = tcs;
                var bytes = Encoding.ASCII.GetBytes(command);
                await Task.Run(() => _sock.Send(bytes));
                if (await Task.WhenAny(tcs.Task, Task.Delay(3000)) != tcs.Task)
                    throw new TimeoutException("Bluetooth: sin respuesta de Vento");
                return await tcs.Task;
            }
            finally
            {
                _pending = null;
                _lock.Release();
            }
        }

        private void ReadLoop()
        {
            var buf = new byte[512];
            var line = new StringBuilder();
            try
            {
                while (true)
                {
                    int n = _sock.Receive(buf);
                    if (n <= 0) break;
                    for (int i = 0; i < n; i++)
                    {
                        char c = (char)buf[i];
                        if (c != '\n') { line.Append(c); continue; }
                        var text = line.ToString().Trim();
                        line.Clear();
                        // En los modos 6 y 7 también llegan lecturas sueltas; solo interesa el JSON
                        if (!text.StartsWith("{")) continue;
                        try { _pending?.TrySetResult(VentoState.Parse(text)); }
                        catch { }
                    }
                }
            }
            catch { }
            finally
            {
                _closed = true;
                _pending?.TrySetException(new IOException("Bluetooth desconectado"));
            }
        }

        public void Dispose()
        {
            _closed = true;
            try { _sock.Close(); } catch { }
        }

        // Dispositivos emparejados en Windows: HKLM\...\BTHPORT\Parameters\Devices\<direccion>\Name
        public static ulong? FindPaired(string name)
        {
            try
            {
                using var root = Registry.LocalMachine.OpenSubKey(
                    @"SYSTEM\CurrentControlSet\Services\BTHPORT\Parameters\Devices");
                if (root == null) return null;
                foreach (var key in root.GetSubKeyNames())
                {
                    using var dev = root.OpenSubKey(key);
                    if (dev?.GetValue("Name") is not byte[] raw) continue;
                    var devName = Encoding.UTF8.GetString(raw).TrimEnd('\0');
                    if (string.Equals(devName, name, StringComparison.OrdinalIgnoreCase))
                        return Convert.ToUInt64(key, 16);
                }
            }
            catch { }
            return null;
        }
    }

    // Bluetooth bajo demanda: cada operación conecta, envía el comando, lee el estado y desconecta,
    // así el Bluetooth de Vento queda libre (p. ej. para el móvil) el resto del tiempo.
    public sealed class BluetoothOnDemand : ITransport
    {
        private static readonly TimeSpan ConnectTimeout = TimeSpan.FromSeconds(12);

        private readonly ulong _address;
        private readonly SemaphoreSlim _lock = new SemaphoreSlim(1, 1);

        public BluetoothOnDemand(ulong address) => _address = address;

        public Task<VentoState> GetStateAsync() => ExchangeAsync(bt => bt.GetStateAsync());

        public Task<VentoState> SetModeAsync(int mode) => ExchangeAsync(bt => bt.SetModeAsync(mode));

        public Task<VentoState> SetSetpointAsync(int degrees) => ExchangeAsync(bt => bt.SetSetpointAsync(degrees));

        private async Task<VentoState> ExchangeAsync(Func<BluetoothTransport, Task<VentoState>> op)
        {
            await _lock.WaitAsync();
            try
            {
                using var bt = await BluetoothTransport.ConnectAsync(_address, ConnectTimeout);
                return await op(bt);
            }
            finally { _lock.Release(); }
        }

        public void Dispose() { }
    }

    // SOCKADDR_BTH (ws2bth.h, empaquetado a 1 byte): familia, direccion, GUID del servicio, canal.
    // Con canal 0 Windows busca por SDP el canal del servicio indicado.
    internal sealed class BluetoothEndPoint : EndPoint
    {
        private readonly ulong _address;
        private readonly Guid _service;

        public BluetoothEndPoint(ulong address, Guid service)
        {
            _address = address;
            _service = service;
        }

        public override AddressFamily AddressFamily => (AddressFamily)32;

        public override SocketAddress Serialize()
        {
            var sa = new SocketAddress(AddressFamily, 30);
            var addr = BitConverter.GetBytes(_address);
            for (int i = 0; i < 8; i++) sa[2 + i] = addr[i];
            var guid = _service.ToByteArray();
            for (int i = 0; i < 16; i++) sa[10 + i] = guid[i];
            for (int i = 0; i < 4; i++) sa[26 + i] = 0;
            return sa;
        }

        public override EndPoint Create(SocketAddress socketAddress) => this;
    }
}
