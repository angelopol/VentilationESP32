using System;
using System.Threading.Tasks;
using System.Windows.Threading;

namespace Vento
{
    public enum LinkStatus { Connecting, Wifi, Offline }

    // Mantiene la conexión con Vento por WiFi: consulta el estado cada pocos segundos por la última
    // IP conocida o, si no responde, por http://<host>.local. Todo corre en el hilo de la interfaz
    // (async/await).
    public sealed class VentoClient : IDisposable
    {
        private static readonly TimeSpan PollInterval = TimeSpan.FromSeconds(3);
        private static readonly TimeSpan RetryInterval = TimeSpan.FromSeconds(10);

        private readonly Config _config;
        private readonly DispatcherTimer _timer;
        private HttpTransport _transport;
        private bool _busy;
        private DateTime _nextConnect = DateTime.MinValue;
        private string _ip;   // última IP conocida: vento.local (mDNS) a veces no resuelve

        public VentoState State { get; private set; }
        public LinkStatus Status { get; private set; } = LinkStatus.Connecting;
        public bool IsConnected => Status == LinkStatus.Wifi;
        public string WebUrl => "http://" + _config.Host + ".local";

        public static string StatusText(LinkStatus status) => status switch
        {
            LinkStatus.Wifi => "Conectado",
            LinkStatus.Offline => "Sin conexión",
            _ => "Conectando…",
        };

        public event Action Changed;

        public VentoClient(Config config)
        {
            _config = config;
            _timer = new DispatcherTimer { Interval = PollInterval };
            _timer.Tick += async (s, e) => await TickAsync();
        }

        public void Start()
        {
            _timer.Start();
            _ = TickAsync();
        }

        public Task<bool> SetModeAsync(int mode) => RunAsync(t => t.SetModeAsync(mode));
        public Task<bool> RefreshStateAsync() => RunAsync(t => t.GetStateAsync());

        public Task<bool> SetSetpointAsync(int degrees) => RunAsync(t => t.SetSetpointAsync(degrees));

        // Al apagar Windows: el hilo de la interfaz queda bloqueado esperando, así que la operación
        // corre en otro hilo con una conexión nueva.
        public bool SetModeBlocking(int mode, TimeSpan timeout)
        {
            _timer.Stop();
            if (!IsConnected) return false;
            string address = _ip ?? _config.Host + ".local";
            var task = Task.Run(async () =>
            {
                using var http = new HttpTransport(address);
                await http.SetModeAsync(mode);
                return true;
            });
            try { return task.Wait(timeout) && task.Result; }
            catch { return false; }
        }

        private async Task TickAsync()
        {
            if (_busy) return;
            _busy = true;
            try
            {
                if (_transport == null)
                {
                    if (DateTime.Now >= _nextConnect) await ConnectAsync();
                    return;
                }

                try { Update(await _transport.GetStateAsync()); }
                catch
                {
                    Drop();
                    await ConnectAsync();
                }
            }
            finally { _busy = false; }
        }

        private async Task<bool> RunAsync(Func<HttpTransport, Task<VentoState>> op)
        {
            while (_busy) await Task.Delay(50);
            _busy = true;
            try
            {
                if (_transport == null && !await ConnectAsync()) return false;
                try
                {
                    Update(await op(_transport));
                    return true;
                }
                catch
                {
                    // La conexión pudo caerse justo ahora: reconecta una vez y reintenta
                    Drop();
                    if (!await ConnectAsync()) return false;
                    try
                    {
                        Update(await op(_transport));
                        return true;
                    }
                    catch
                    {
                        Drop();
                        return false;
                    }
                }
            }
            finally { _busy = false; }
        }

        // Primero por la última IP conocida y, si no responde, por <host>.local
        private async Task<bool> ConnectAsync()
        {
            if (_ip != null && await TryConnectAsync(_ip)) return true;
            if (await TryConnectAsync(_config.Host + ".local")) return true;
            SetStatus(LinkStatus.Offline);
            _nextConnect = DateTime.Now + RetryInterval;
            return false;
        }

        private async Task<bool> TryConnectAsync(string address)
        {
            var http = new HttpTransport(address);
            try
            {
                var state = await http.GetStateAsync();
                _transport?.Dispose();
                _transport = http;
                SetStatus(LinkStatus.Wifi);
                Update(state);
                return true;
            }
            catch
            {
                http.Dispose();
                return false;
            }
        }

        private void Drop()
        {
            _transport?.Dispose();
            _transport = null;
            SetStatus(LinkStatus.Connecting);
        }

        private void Update(VentoState state)
        {
            if (state.Ip != null) _ip = state.Ip;
            State = state;
            Changed?.Invoke();
        }

        private void SetStatus(LinkStatus status)
        {
            if (Status == status) return;
            Status = status;
            Changed?.Invoke();
        }

        public void Dispose()
        {
            _timer.Stop();
            _transport?.Dispose();
            _transport = null;
        }
    }
}
