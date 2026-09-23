using System;
using System.Diagnostics;
using System.Globalization;
using System.Threading;
using System.Windows;
using Drawing = System.Drawing;
using WinForms = System.Windows.Forms;

namespace Vento
{
    public partial class App : Application
    {
        // El modo de inicio solo se aplica si Vento responde en este tiempo tras arrancar la app
        // (si se enciende mucho después, no se le cambia el modo por sorpresa)
        private static readonly TimeSpan StartupModeWindow = TimeSpan.FromMinutes(5);

        private Mutex _mutex;
        private Config _config;
        private VentoClient _client;
        private WinForms.NotifyIcon _tray;
        private Drawing.Icon _iconOn, _iconOff;
        private PanelWindow _panel;
        private DateTime _panelClosedAt = DateTime.MinValue;
        private DateTime _startedAt;
        private bool _startupModeDone;

        private WinForms.ToolStripMenuItem _statusItem, _webItem, _startItem, _startupModeItem;
        private readonly WinForms.ToolStripMenuItem[] _levelItems = new WinForms.ToolStripMenuItem[6];

        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);
            WinForms.Application.EnableVisualStyles();

            // Una sola instancia.
            _mutex = new Mutex(true, "Vento_SingleInstance", out bool createdNew);
            if (!createdNew) { Shutdown(); return; }

            _startedAt = DateTime.Now;
            _config = Config.Load();
            _client = new VentoClient(_config);
            _client.Changed += RefreshTray;
            _client.Changed += ApplyStartupMode;

            SetupTray();
            _client.Start();

            // Inicio con Windows activado la primera vez; luego lo decide el menú.
            if (!_config.AutostartSetup && Autostart.Enable())
            {
                _config.AutostartSetup = true;
                _config.Save();
            }
        }

        // ---------------------------------------------------------------- tray
        private void SetupTray()
        {
            LoadIcons();
            _tray = new WinForms.NotifyIcon { Icon = _iconOff, Visible = true, Text = "Vento" };

            var menu = new WinForms.ContextMenuStrip();

            _statusItem = new WinForms.ToolStripMenuItem("Conectando…") { Enabled = false };
            menu.Items.Add(_statusItem);
            menu.Items.Add(new WinForms.ToolStripSeparator());

            for (int mode = 0; mode <= 5; mode++)
            {
                int m = mode;
                var item = new WinForms.ToolStripMenuItem(VentoState.ModeName(m));
                item.Click += async (s, a) =>
                {
                    if (!await _client.SetModeAsync(m)) NotifyUnavailable();
                };
                _levelItems[m] = item;
                menu.Items.Add(item);
            }

            var autoItem = new WinForms.ToolStripMenuItem("Auto / Progresivo…");
            autoItem.Click += (s, a) => ShowPanel();
            menu.Items.Add(autoItem);
            menu.Items.Add(new WinForms.ToolStripSeparator());

            _webItem = new WinForms.ToolStripMenuItem("Abrir panel web");
            _webItem.Click += (s, a) => OpenUrl(_client.WebUrl);
            menu.Items.Add(_webItem);

            var pairItem = new WinForms.ToolStripMenuItem("Emparejar por Bluetooth…");
            pairItem.Click += (s, a) => OpenPairing();
            menu.Items.Add(pairItem);

            _startItem = new WinForms.ToolStripMenuItem("Iniciar con Windows") { CheckOnClick = true };
            _startItem.CheckedChanged += (s, a) =>
            {
                bool ok = _startItem.Checked ? Autostart.Enable() : Autostart.Disable();
                if (!ok) Notify("No se pudo cambiar el inicio con Windows.");
            };
            menu.Items.Add(_startItem);

            _startupModeItem = new WinForms.ToolStripMenuItem("Nivel 5 al iniciar")
            { Checked = _config.StartupMode >= 0, CheckOnClick = true };
            _startupModeItem.CheckedChanged += (s, a) =>
            {
                _config.StartupMode = _startupModeItem.Checked ? 5 : -1;
                _config.Save();
            };
            menu.Items.Add(_startupModeItem);

            menu.Items.Add(new WinForms.ToolStripSeparator());
            var exitItem = new WinForms.ToolStripMenuItem("Salir");
            exitItem.Click += (s, a) => ExitApp();
            menu.Items.Add(exitItem);

            menu.Opening += (s, a) =>
            {
                RefreshTray();
                _startItem.Checked = Autostart.IsEnabled();
            };

            _tray.ContextMenuStrip = menu;
            _tray.MouseClick += (s, a) =>
            {
                if (a.Button == WinForms.MouseButtons.Left) TogglePanel();
            };
        }

        private void RefreshTray()
        {
            if (_tray == null) return;
            var state = _client.State;
            bool connected = _client.IsConnected;

            _statusItem.Text = VentoClient.StatusText(_client.Status);
            for (int m = 0; m <= 5; m++)
                _levelItems[m].Checked = connected && state != null && state.Mode == m;
            _webItem.Enabled = _client.Status == LinkStatus.Wifi;

            _tray.Icon = connected ? _iconOn : _iconOff;
            string tip = "Vento";
            if (connected && state != null)
            {
                tip += " · " + VentoState.ModeName(state.Mode);
                if (state.Hic.HasValue) tip += " · " + state.Hic.Value.ToString("0.0", CultureInfo.CurrentCulture) + "°";
                tip += _client.Status == LinkStatus.Wifi ? " · WiFi" : " · Bluetooth";
            }
            else
            {
                tip += " · " + VentoClient.StatusText(_client.Status);
            }
            _tray.Text = tip.Length > 63 ? tip.Substring(0, 63) : tip;
        }

        // Al iniciar (normalmente con Windows) pone el modo configurado en cuanto hay conexión
        private void ApplyStartupMode()
        {
            if (_startupModeDone || !_client.IsConnected) return;
            _startupModeDone = true;
            int mode = _config.StartupMode;
            if (mode < 0 || mode > 7 || DateTime.Now - _startedAt > StartupModeWindow) return;
            if (_client.State != null && _client.State.Mode == mode) return;
            _ = _client.SetModeAsync(mode);
        }

        // ---------------------------------------------------------------- panel
        private void TogglePanel()
        {
            if (_panel != null) { _panel.Close(); return; }
            // El clic en el icono desactiva (y cierra) el panel justo antes: no lo reabras
            if ((DateTime.Now - _panelClosedAt).TotalMilliseconds < 300) return;
            ShowPanel();
        }

        private void ShowPanel()
        {
            if (_panel != null) { _panel.Activate(); return; }
            _panel = new PanelWindow(_client);
            _panel.Unavailable += NotifyUnavailable;
            _panel.Closed += (s, a) => { _panel = null; _panelClosedAt = DateTime.Now; };
            _panel.Show();
        }

        // ------------------------------------------------------------ pairing
        // Solo cuando el usuario pulsa una opción sin conexión (o desde el menú):
        // si Vento está apagado al arrancar no se muestra nada.
        private void OpenPairing()
        {
            OpenUrl("ms-settings:bluetooth");
            Notify($"Vento no responde por WiFi. Empareja «{_config.Host}» en Bluetooth " +
                   "(Agregar dispositivo → Bluetooth) y la app lo usará automáticamente.");
            _ = _client.ReconnectAsync();
        }

        private void NotifyUnavailable()
        {
            if (_client.Status == LinkStatus.NotPaired) OpenPairing();
            else Notify("No se pudo conectar con Vento por WiFi ni por Bluetooth.");
        }

        // -------------------------------------------------------------- helpers
        private void LoadIcons()
        {
            using var stream = typeof(App).Assembly.GetManifestResourceStream("vento.ico");
            _iconOn = new Drawing.Icon(stream, WinForms.SystemInformation.SmallIconSize);
            _iconOff = Grayscale(_iconOn);
        }

        // Icono gris y semitransparente cuando no hay conexión
        private static Drawing.Icon Grayscale(Drawing.Icon icon)
        {
            using var src = icon.ToBitmap();
            using var dst = new Drawing.Bitmap(src.Width, src.Height);
            var matrix = new Drawing.Imaging.ColorMatrix(new[]
            {
                new[] { 0.30f, 0.30f, 0.30f, 0, 0 },
                new[] { 0.59f, 0.59f, 0.59f, 0, 0 },
                new[] { 0.11f, 0.11f, 0.11f, 0, 0 },
                new[] { 0f, 0, 0, 0.7f, 0 },
                new[] { 0f, 0, 0, 0, 1 },
            });
            using (var g = Drawing.Graphics.FromImage(dst))
            using (var attrs = new Drawing.Imaging.ImageAttributes())
            {
                attrs.SetColorMatrix(matrix);
                g.DrawImage(src, new Drawing.Rectangle(0, 0, src.Width, src.Height),
                    0, 0, src.Width, src.Height, Drawing.GraphicsUnit.Pixel, attrs);
            }
            return Drawing.Icon.FromHandle(dst.GetHicon());
        }

        private static void OpenUrl(string url)
        {
            try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); }
            catch { }
        }

        private void Notify(string message)
        {
            try { _tray?.ShowBalloonTip(4000, "Vento", message, WinForms.ToolTipIcon.Info); }
            catch { }
        }

        private void ExitApp()
        {
            _panel?.Close();
            _client?.Dispose();
            if (_tray != null) { _tray.Visible = false; _tray.Dispose(); }
            Shutdown();
        }
    }
}
