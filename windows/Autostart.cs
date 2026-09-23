using System;
using Microsoft.Win32;

namespace Vento
{
    // Arranque con Windows mediante la clave Run del registro (sesión de usuario, sin admin).
    public static class Autostart
    {
        private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
        private const string ValueName = "Vento";

        public static bool IsEnabled()
        {
            try
            {
                using var key = Registry.CurrentUser.OpenSubKey(RunKey, false);
                return key?.GetValue(ValueName) is string s && !string.IsNullOrEmpty(s);
            }
            catch { return false; }
        }

        public static bool Enable()
        {
            try
            {
                // Environment.ProcessPath devuelve el .exe también en publish single-file.
                var exe = Environment.ProcessPath;
                if (string.IsNullOrEmpty(exe)) return false;
                using var key = Registry.CurrentUser.OpenSubKey(RunKey, true)
                                ?? Registry.CurrentUser.CreateSubKey(RunKey, true);
                key.SetValue(ValueName, "\"" + exe + "\"");
                return true;
            }
            catch { return false; }
        }

        public static bool Disable()
        {
            try
            {
                using var key = Registry.CurrentUser.OpenSubKey(RunKey, true);
                if (key?.GetValue(ValueName) != null) key.DeleteValue(ValueName, false);
                return true;
            }
            catch { return false; }
        }
    }
}
