using System;
using System.Threading.Tasks;
using System.Windows.Threading;

namespace Vento
{
    public enum LinkStatus { Connecting, Wifi, Bluetooth, NotPaired, Offline }

    // Mantiene la conexión con Vento: primero http://<host>.local (consulta el estado cada pocos
    // segundos) y, si no responde, el Bluetooth emparejado, que solo se conecta para cada operación.
    // Todo corre en el hilo de la interfaz (async/await).
    public sealed class VentoClient : IDisposable
    {
        private static readonly TimeSpan PollInterval = TimeSpan.FromSeconds(3);
        private static readonly TimeSpan WifiProbeInterval = TimeSpan.FromSeconds(15);

        private readonly Config _config;
        private readonly DispatcherTimer _timer;
        private ITransport _transport;
        private bool _busy;
        private DateTime _nextConnect = DateTime.MinValue;
        private DateTime _nextWifiProbe = DateTime.MinValue;

        public VentoState State { get; private set; }
        public LinkStatus Status { get; private set; } = LinkStatus.Connecting;
        public bool IsConnected => Status == LinkStatus.Wifi || Status == LinkStatus.Bluetooth;
        public string WebUrl => "http://" + _config.Host + ".local";

        public static string StatusText(LinkStatus status) => status switch
        {
            LinkStatus.Wifi => "Conectado por WiFi",
            LinkStatus.Bluetooth => "Por Bluetooth",
            LinkStatus.NotPaired => "Sin WiFi y sin emparejar",
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

        public Task<bool> SetSetpointAsync(int degrees) => RunAsync(t => t.SetSetpointAsync(degrees));

        // Pide el estado actual (por Bluetooth no se consulta periódicamente)
        public Task<bool> RefreshAsync() => RunAsync(t => t.GetStateAsync());

        // Reintenta ya (p. ej. tras emparejar)
        public Task<bool> ReconnectAsync()
        {
            _nextConnect = DateTime.MinValue;
            return RunAsync(null);
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

                // Por Bluetooth no se mantiene la conexión: solo se comprueba si el WiFi ha vuelto
                if (_transport is BluetoothOnDemand)
                {
                    if (DateTime.Now >= _nextWifiProbe)
                    {
                        _nextWifiProbe = DateTime.Now + WifiProbeInterval;
                        await TryWifiAsync();
                    }
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

        private async Task<bool> RunAsync(Func<ITransport, Task<VentoState>> op)
        {
            while (_busy) await Task.Delay(50);
            _busy = true;
            try
            {
                if (_transport == null && !await ConnectAsync()) return false;
                if (op == null) return true;
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

        private async Task<bool> ConnectAsync()
        {
            if (await TryWifiAsync()) return true;

            ulong? address = BluetoothTransport.FindPaired(_config.Host);
            if (address == null)
            {
                SetStatus(LinkStatus.NotPaired);
                _nextConnect = DateTime.Now.AddSeconds(10);
                return false;
            }

            if (Status != LinkStatus.Offline) SetStatus(LinkStatus.Connecting);
            try
            {
                // Una consulta para comprobar que responde; luego se desconecta
                var bt = new BluetoothOnDemand(address.Value);
                var state = await bt.GetStateAsync();
                Replace(bt, LinkStatus.Bluetooth);
                _nextWifiProbe = DateTime.Now + WifiProbeInterval;
                Update(state);
                return true;
            }
            catch
            {
                SetStatus(LinkStatus.Offline);
                _nextConnect = DateTime.Now.AddSeconds(15);
                return false;
            }
        }

        private async Task<bool> TryWifiAsync()
        {
            var http = new HttpTransport(_config.Host);
            try
            {
                var state = await http.GetStateAsync();
                Replace(http, LinkStatus.Wifi);
                Update(state);
                return true;
            }
            catch
            {
                http.Dispose();
                return false;
            }
        }

        private void Replace(ITransport transport, LinkStatus status)
        {
            if (!ReferenceEquals(_transport, transport)) _transport?.Dispose();
            _transport = transport;
            SetStatus(status);
        }

        private void Drop()
        {
            _transport?.Dispose();
            _transport = null;
            SetStatus(LinkStatus.Connecting);
        }

        private void Update(VentoState state)
        {
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
