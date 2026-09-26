using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Threading;

namespace Vento
{
    // Un aire acondicionado Tuya en config.json ("AirConditioners"). Mismo formato que cada
    // objeto de AC_DEVICES_JSON en el secrets.h del ESP32.
    public sealed class AcConfig
    {
        [JsonPropertyName("name")] public string Name { get; set; } = "Aire";
        [JsonPropertyName("id")] public string Id { get; set; }
        [JsonPropertyName("key")] public string Key { get; set; }
        [JsonPropertyName("ip")] public string Ip { get; set; }
        // "3.3", "3.4", "3.5" (también como número) o "auto" para averiguarla probando
        [JsonPropertyName("version"), JsonConverter(typeof(TextOrNumberConverter))]
        public string Version { get; set; } = "auto";
        [JsonPropertyName("dps")] public AcDps Dps { get; set; } = new AcDps();
        // 10 si el aire manda 240 para 24,0 °C
        [JsonPropertyName("scale")] public double Scale { get; set; } = 1;
        [JsonPropertyName("min")] public double Min { get; set; } = 16;
        [JsonPropertyName("max")] public double Max { get; set; } = 31;
        [JsonPropertyName("step")] public double Step { get; set; } = 1;
        // valor que usa el aparato -> nombre en la app (valores típicos de la categoría "kt" de Tuya)
        [JsonPropertyName("modes")] public OrderedDictionary<string, string> Modes { get; set; } = new()
        {
            ["cold"] = "Frío", ["wet"] = "Seco", ["wind"] = "Ventilador", ["hot"] = "Calor", ["auto"] = "Auto",
        };
        [JsonPropertyName("fans")] public OrderedDictionary<string, string> Fans { get; set; } = new()
        {
            ["low"] = "Baja", ["mid"] = "Media", ["high"] = "Alta", ["auto"] = "Auto",
        };
        // Interruptores extra: número de DP -> nombre, p. ej. {"15": "Oscilación", "101": "Sueño"}
        [JsonPropertyName("toggles")] public OrderedDictionary<string, string> Toggles { get; set; } = new();

        // 33, 34, 35; 0 = "auto" (se averigua probando las tres)
        public int ProtocolVersion() =>
            double.TryParse(Version, NumberStyles.Float, CultureInfo.InvariantCulture, out var v)
                ? (int)Math.Round(v * 10) : 0;

        // Valores sin sentido en el JSON (0 o negativos) harían dividir por cero
        public void Normalize()
        {
            if (Scale <= 0) Scale = 1;
            if (Step <= 0) Step = 1;
            if (Max < Min) Max = Min;
            Dps ??= new AcDps();
            Modes ??= new();
            Fans ??= new();
            Toggles ??= new();
        }

        public bool IsValid() =>
            !string.IsNullOrWhiteSpace(Id) && Key?.Length == 16 && !string.IsNullOrWhiteSpace(Ip);
    }

    // Acepta "3.3" o 3.3
    internal sealed class TextOrNumberConverter : JsonConverter<string>
    {
        public override string Read(ref Utf8JsonReader reader, Type type, JsonSerializerOptions options) =>
            reader.TokenType == JsonTokenType.Number
                ? System.Text.Encoding.UTF8.GetString(reader.ValueSpan)
                : reader.GetString();

        public override void Write(Utf8JsonWriter writer, string value, JsonSerializerOptions options) =>
            writer.WriteStringValue(value);
    }

    // Número de cada dato (DP) en el modelo del aire; 0 si no lo tiene
    public sealed class AcDps
    {
        [JsonPropertyName("power")] public int Power { get; set; } = 1;
        [JsonPropertyName("setpoint")] public int Setpoint { get; set; } = 2;
        [JsonPropertyName("temp")] public int Temp { get; set; } = 3;
        [JsonPropertyName("mode")] public int Mode { get; set; } = 4;
        [JsonPropertyName("fan")] public int Fan { get; set; } = 5;
    }

    // Estado y órdenes de un aire. Solo se consulta mientras alguien lo mira (panel abierto)
    // o al abrir el menú, para no ocupar la conexión local del aparato.
    public sealed class AirConditioner
    {
        private static readonly TimeSpan PollInterval = TimeSpan.FromSeconds(5);
        // Tras una orden el aire tarda unos segundos en aplicarla: mientras tanto (y como mucho Hold)
        // se sigue mostrando el valor pedido aunque la consulta devuelva todavía el anterior
        private static readonly TimeSpan Hold = TimeSpan.FromSeconds(15);

        private readonly TuyaClient _tuya;
        private readonly SemaphoreSlim _lock = new SemaphoreSlim(1, 1);
        private readonly DispatcherTimer _timer;
        private int _watchers;
        private readonly Dictionary<int, bool> _toggleOn = new Dictionary<int, bool>();
        private readonly Dictionary<int, (string Json, DateTime Until)> _expect = new Dictionary<int, (string, DateTime)>();

        public AcConfig Config { get; }
        public string Name => Config.Name;
        public bool Polled { get; private set; }
        public bool Online { get; private set; }
        public string Error { get; private set; }
        public bool Power { get; private set; }
        public double? Setpoint { get; private set; }
        public double? Temp { get; private set; }
        public string Mode { get; private set; }
        public string Fan { get; private set; }

        public event Action Changed;

        // Interruptores configurados (oscilación, sueño...) con su DP
        public IEnumerable<(int Dp, string Name)> Toggles()
        {
            foreach (var kv in Config.Toggles)
                if (int.TryParse(kv.Key, out int dp) && dp > 0) yield return (dp, kv.Value);
        }

        public bool IsToggleOn(int dp) => _toggleOn.TryGetValue(dp, out var on) && on;

        public AirConditioner(AcConfig config)
        {
            Config = config;
            Config.Normalize();
            _tuya = new TuyaClient(config.Id, config.Key, config.Ip, config.ProtocolVersion());
            _timer = new DispatcherTimer { Interval = PollInterval };
            _timer.Tick += async (s, e) => await RefreshAsync();
        }

        public string ModeName(string mode) =>
            mode != null && Config.Modes.TryGetValue(mode, out var name) ? name : mode;

        // Resumen para el menú y el panel: "Frío · 24°", "Apagado", "Sin conexión"...
        public string Summary()
        {
            if (!Polled) return "Conectando…";
            if (!Online) return "Sin conexión";
            if (!Power) return "Apagado";
            var text = ModeName(Mode) ?? "Encendido";
            if (Setpoint.HasValue) text += " · " + FormatTemp(Setpoint.Value);
            return text;
        }

        public string FormatTemp(double t) =>
            t.ToString(Config.Step < 1 ? "0.0" : "0", CultureInfo.CurrentCulture) + "°";

        // El panel llama a Watch al abrirse y a Unwatch al cerrarse
        public void Watch()
        {
            if (_watchers++ == 0) _timer.Start();
            _ = RefreshAsync();
        }

        public void Unwatch()
        {
            if (_watchers > 0 && --_watchers == 0) _timer.Stop();
        }

        public async Task RefreshAsync()
        {
            if (!await _lock.WaitAsync(0)) return;   // ya hay una operación en curso
            try { await QueryAsync(); }
            finally { _lock.Release(); }
        }

        public Task<bool> SetPowerAsync(bool on) =>
            SendAsync(Config.Dps.Power, on, () => Power = on);

        public Task<bool> SetSetpointAsync(double t)
        {
            t = Math.Clamp(t, Config.Min, Config.Max);
            t = Config.Min + Math.Round((t - Config.Min) / Config.Step) * Config.Step;
            return SendAsync(Config.Dps.Setpoint, (int)Math.Round(t * Config.Scale), () => Setpoint = t);
        }

        public Task<bool> SetModeAsync(string mode) =>
            SendAsync(Config.Dps.Mode, mode, () => Mode = mode);

        public Task<bool> SetFanAsync(string fan) =>
            SendAsync(Config.Dps.Fan, fan, () => Fan = fan);

        public Task<bool> SetToggleAsync(int dp, bool on) =>
            SendAsync(dp, on, () => _toggleOn[dp] = on);

        // Al apagar Windows el hilo de la interfaz queda bloqueado esperando: se manda desde otro
        // hilo y sin pasar por _lock (una operación en curso necesitaría ese hilo para terminar)
        public Task<bool> SetPowerDetachedAsync(bool on)
        {
            if (Config.Dps.Power <= 0) return Task.FromResult(false);
            var dps = new Dictionary<string, object> { [Config.Dps.Power.ToString()] = on };
            return Task.Run(async () =>
            {
                try { await _tuya.SetAsync(dps); return true; }
                catch (Exception) { return false; }
            });
        }

        private async Task<bool> SendAsync(int dp, object value, Action apply)
        {
            if (dp <= 0) return false;
            apply();
            _expect[dp] = (JsonSerializer.Serialize(value), DateTime.Now + Hold);
            Changed?.Invoke();

            await _lock.WaitAsync();
            bool ok = true;
            try
            {
                var dps = new Dictionary<string, object> { [dp.ToString()] = value };
                try { await _tuya.SetAsync(dps); }
                catch (TuyaException)
                {
                    // El aparato puede estar atendiendo otra conexión (p. ej. el ESP32): un reintento
                    await Task.Delay(500);
                    await _tuya.SetAsync(dps);
                }
            }
            catch (TuyaException e)
            {
                ok = false;
                Error = e.Message;
            }
            finally
            {
                await QueryAsync();
                _lock.Release();
            }
            return ok;
        }

        // Llamar con _lock tomado
        private async Task QueryAsync()
        {
            try
            {
                var d = Config.Dps;
                var ask = new List<int> { d.Power, d.Setpoint, d.Temp, d.Mode, d.Fan };
                foreach (var t in Toggles()) ask.Add(t.Dp);
                Apply(await _tuya.QueryAsync(ask));
                Online = true;
                Error = null;
            }
            catch (TuyaException e)
            {
                Online = false;
                Error = e.Message;
            }
            catch (Exception e)
            {
                Online = false;
                Error = e.Message;
            }
            Polled = true;
            Changed?.Invoke();
        }

        private void Apply(JsonElement dps)
        {
            // Los DP con una orden sin confirmar no se actualizan hasta que el aire informe del
            // valor pedido o pase Hold (si la rechazó, se vuelve a mostrar lo que dice el aparato)
            var held = new HashSet<int>();
            foreach (var kv in new List<KeyValuePair<int, (string Json, DateTime Until)>>(_expect))
            {
                string got = Get(dps, kv.Key) is { } v ? v.GetRawText() : null;
                if (got == kv.Value.Json || DateTime.Now >= kv.Value.Until) _expect.Remove(kv.Key);
                else held.Add(kv.Key);
            }
            JsonElement? Get(JsonElement all, int dp) => held.Contains(dp) ? null : AirConditioner.Get(all, dp);

            if (Get(dps, Config.Dps.Power) is { } p && (p.ValueKind == JsonValueKind.True || p.ValueKind == JsonValueKind.False))
                Power = p.GetBoolean();
            if (Get(dps, Config.Dps.Setpoint) is { } sp && sp.ValueKind == JsonValueKind.Number)
                Setpoint = sp.GetDouble() / Config.Scale;
            if (Get(dps, Config.Dps.Temp) is { } t && t.ValueKind == JsonValueKind.Number)
                Temp = t.GetDouble() / Config.Scale;
            if (Get(dps, Config.Dps.Mode) is { } m && m.ValueKind == JsonValueKind.String)
                Mode = m.GetString();
            if (Get(dps, Config.Dps.Fan) is { } f && f.ValueKind == JsonValueKind.String)
                Fan = f.GetString();
            foreach (var tg in Toggles())
                if (Get(dps, tg.Dp) is { } b && (b.ValueKind == JsonValueKind.True || b.ValueKind == JsonValueKind.False))
                    _toggleOn[tg.Dp] = b.GetBoolean();
        }

        private static JsonElement? Get(JsonElement dps, int dp) =>
            dp > 0 && dps.ValueKind == JsonValueKind.Object && dps.TryGetProperty(dp.ToString(), out var v)
                ? v : null;
    }
}
