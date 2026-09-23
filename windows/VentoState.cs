using System.Text.Json;

namespace Vento
{
    // Estado que devuelve el ESP32 (/api/state por WiFi, comando '?' por Bluetooth)
    public sealed class VentoState
    {
        public int Mode { get; private set; }
        public int Setpoint { get; private set; }
        public int Pwm { get; private set; }
        public double? Temp { get; private set; }
        public double? Hum { get; private set; }
        public double? Hic { get; private set; }

        public static VentoState Parse(string json)
        {
            using var doc = JsonDocument.Parse(json);
            var r = doc.RootElement;
            return new VentoState
            {
                Mode = r.GetProperty("mode").GetInt32(),
                Setpoint = r.GetProperty("setpoint").GetInt32(),
                Pwm = r.TryGetProperty("pwm", out var p) ? p.GetInt32() : 0,
                Temp = Num(r, "temp"),
                Hum = Num(r, "hum"),
                Hic = Num(r, "hic"),
            };
        }

        private static double? Num(JsonElement r, string name) =>
            r.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Number ? v.GetDouble() : null;

        public static string ModeName(int mode) => mode switch
        {
            0 => "Apagado",
            6 => "Auto",
            7 => "Progresivo",
            _ => "Nivel " + mode,
        };
    }
}
