using System;
using System.IO;
using System.Text.Json;

namespace Vento
{
    // %APPDATA%\Vento\config.json
    public sealed class Config
    {
        // DEVICE_HOSTNAME del firmware: se usa para http://<Host>.local y como nombre Bluetooth
        public string Host { get; set; } = "vento";
        public bool AutostartSetup { get; set; }
        // Modo que se aplica al iniciar la app (0-7); -1 lo desactiva
        public int StartupMode { get; set; } = 5;
        // Modo que se aplica al apagar Windows o cerrar sesión (0-7); -1 lo desactiva
        public int ShutdownMode { get; set; } = 0;

        private static string Dir =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Vento");
        private static string FilePath => Path.Combine(Dir, "config.json");

        public static Config Load()
        {
            try
            {
                if (File.Exists(FilePath))
                {
                    var cfg = JsonSerializer.Deserialize<Config>(File.ReadAllText(FilePath));
                    if (cfg != null && !string.IsNullOrWhiteSpace(cfg.Host)) return cfg;
                }
            }
            catch { }
            var def = new Config();
            def.Save();
            return def;
        }

        public void Save()
        {
            try
            {
                Directory.CreateDirectory(Dir);
                File.WriteAllText(FilePath,
                    JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
            }
            catch { }
        }
    }
}
