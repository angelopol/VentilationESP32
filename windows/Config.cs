using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Vento
{
    // %APPDATA%\Vento\config.json
    public sealed class Config
    {
        // DEVICE_HOSTNAME del firmware: se usa para http://<Host>.local y como nombre Bluetooth
        public string Host { get; set; } = "vento";
        public bool AutostartSetup { get; set; }
        // Sistema de ventilación al iniciar la app: modo del ventilador (0-7) y encender los aires;
        // -1 lo desactiva
        public int StartupMode { get; set; } = 5;
        // Sistema de ventilación al apagar Windows o cerrar sesión: modo del ventilador (0-7) y
        // apagar los aires; -1 lo desactiva
        public int ShutdownMode { get; set; } = 0;
        // Aires acondicionados Tuya controlados por la red local (ver README)
        public List<AcConfig> AirConditioners { get; set; } = new List<AcConfig>();

        // config.json existe pero no se pudo leer: no se guarda nada encima
        [JsonIgnore] public bool LoadFailed { get; private set; }

        private static string Dir =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Vento");
        private static string FilePath => Path.Combine(Dir, "config.json");

        public static Config Load()
        {
            bool exists = File.Exists(FilePath);
            try
            {
                if (exists)
                {
                    var cfg = JsonSerializer.Deserialize<Config>(File.ReadAllText(FilePath), new JsonSerializerOptions
                    {
                        ReadCommentHandling = JsonCommentHandling.Skip,
                        AllowTrailingCommas = true,
                    });
                    if (cfg != null && !string.IsNullOrWhiteSpace(cfg.Host))
                    {
                        cfg.AirConditioners ??= new List<AcConfig>();
                        return cfg;
                    }
                }
            }
            catch { }
            // Un archivo con errores (p. ej. editado a mano) no se sobrescribe: se usan los valores
            // por defecto hasta que se corrija
            var def = new Config();
            if (!exists) def.Save();
            else def.LoadFailed = true;
            return def;
        }

        public void Save()
        {
            if (LoadFailed) return;
            try
            {
                Directory.CreateDirectory(Dir);
                File.WriteAllText(FilePath,
                    JsonSerializer.Serialize(this, new JsonSerializerOptions
                    {
                        WriteIndented = true,
                        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,   // "Frío" legible
                    }));
            }
            catch { }
        }
    }
}
