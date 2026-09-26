using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Globalization;
using System.Threading;
using System.Threading.Tasks;
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
        // Windows avisa de que una app bloquea el apagado a los ~5 s: no esperar más que eso
        private static readonly TimeSpan ShutdownModeTimeout = TimeSpan.FromSeconds(4);

        private Mutex _mutex;
        private Config _config;
        private VentoClient _client;
        private List<AirConditioner> _acs;
        private WinForms.NotifyIcon _tray;
        private Drawing.Icon _iconOn, _iconOff;
        private PanelWindow _panel;
        private DateTime _panelClosedAt = DateTime.MinValue;
        private DateTime _startedAt;
        private bool _startupModeDone;

        private WinForms.ToolStripMenuItem _statusItem, _webItem, _startItem, _startupModeItem, _shutdownModeItem;
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
            _acs = _config.AirConditioners.Where(c => c != null && c.IsValid())
                                          .Select(c => new AirConditioner(c)).ToList();

            SetupTray();
            _client.Start();
            if (_config.StartupMode >= 0)
                foreach (var ac in _acs) _ = TurnOnAtStartupAsync(ac);
            SessionEnding += (s, a) => ApplyShutdownMode();

            if (_config.LoadFailed)
                Notify("config.json tiene un error y no se pudo leer. Corrígelo y reinicia Vento.");
            else if (_acs.Count < _config.AirConditioners.Count)
                Notify("Algún aire de config.json no tiene id, ip o una key de 16 caracteres y se ignoró.");

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

            if (_acs.Count > 0)
            {
                foreach (var ac in _acs) menu.Items.Add(AcMenu(ac));
                menu.Items.Add(new WinForms.ToolStripSeparator());
            }

            _webItem = new WinForms.ToolStripMenuItem("Abrir panel web");
            _webItem.Click += (s, a) => OpenUrl(_client.WebUrl);
            menu.Items.Add(_webItem);


            _startItem = new WinForms.ToolStripMenuItem("Iniciar con Windows") { CheckOnClick = true };
            _startItem.CheckedChanged += (s, a) =>
            {
                bool ok = _startItem.Checked ? Autostart.Enable() : Autostart.Disable();
                if (!ok) Notify("No se pudo cambiar el inicio con Windows.");
            };
            menu.Items.Add(_startItem);

            // Sistema de ventilación: el ventilador y los aires se encienden con Windows y se apagan con el PC
            var systemItem = new WinForms.ToolStripMenuItem("Sistema de ventilación");
            _startupModeItem = new WinForms.ToolStripMenuItem("Encender al iniciar Windows")
            {
                Checked = _config.StartupMode >= 0, CheckOnClick = true,
                ToolTipText = _acs.Count > 0 ? "Ventilador en modo Auto y aires encendidos" : "Ventilador en modo Auto",
            };
            _startupModeItem.CheckedChanged += (s, a) =>
            {
                _config.StartupMode = _startupModeItem.Checked ? 6 : -1;
                _config.Save();
            };
            systemItem.DropDownItems.Add(_startupModeItem);

            _shutdownModeItem = new WinForms.ToolStripMenuItem("Apagar al apagar el PC")
            {
                Checked = _config.ShutdownMode >= 0, CheckOnClick = true,
                ToolTipText = _acs.Count > 0 ? "Apaga el ventilador y los aires" : "Apaga el ventilador",
            };
            _shutdownModeItem.CheckedChanged += (s, a) =>
            {
                _config.ShutdownMode = _shutdownModeItem.Checked ? 0 : -1;
                _config.Save();
            };
            systemItem.DropDownItems.Add(_shutdownModeItem);
            menu.Items.Add(systemItem);

            menu.Items.Add(new WinForms.ToolStripSeparator());
            var exitItem = new WinForms.ToolStripMenuItem("Salir");
            exitItem.Click += (s, a) => ExitApp();
            menu.Items.Add(exitItem);

            menu.Opening += (s, a) =>
            {
                RefreshTray();
                foreach (var ac in _acs) _ = ac.RefreshAsync();
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
            }
            else
            {
                tip += " · " + VentoClient.StatusText(_client.Status);
            }
            _tray.Text = tip.Length > 63 ? tip.Substring(0, 63) : tip;
        }

        // Submenú de un aire: encendido, temperatura, modo y ventilador
        private WinForms.ToolStripMenuItem AcMenu(AirConditioner ac)
        {
            var root = new WinForms.ToolStripMenuItem(ac.Name);
            var cfg = ac.Config;

            async void Run(System.Threading.Tasks.Task<bool> op)
            {
                if (!await op) Notify($"No se pudo contactar con «{ac.Name}»: {ac.Error}");
            }

            var power = new WinForms.ToolStripMenuItem("Encendido");
            power.Click += (s, a) => Run(ac.SetPowerAsync(!ac.Power));
            root.DropDownItems.Add(power);

            var temp = new WinForms.ToolStripMenuItem("Objetivo") { Enabled = false };
            var up = new WinForms.ToolStripMenuItem($"Subir {cfg.Step:0.#}°");
            up.Click += (s, a) => Run(ac.SetSetpointAsync((ac.Setpoint ?? cfg.Min) + cfg.Step));
            var down = new WinForms.ToolStripMenuItem($"Bajar {cfg.Step:0.#}°");
            down.Click += (s, a) => Run(ac.SetSetpointAsync((ac.Setpoint ?? cfg.Min) - cfg.Step));
            if (cfg.Dps.Setpoint > 0)
            {
                root.DropDownItems.Add(new WinForms.ToolStripSeparator());
                root.DropDownItems.AddRange(new WinForms.ToolStripItem[] { temp, up, down });
            }

            var modes = new Dictionary<string, WinForms.ToolStripMenuItem>();
            var fans = new Dictionary<string, WinForms.ToolStripMenuItem>();
            void AddOptions(int dp, IEnumerable<KeyValuePair<string, string>> options,
                            Dictionary<string, WinForms.ToolStripMenuItem> items, Func<string, System.Threading.Tasks.Task<bool>> set)
            {
                if (dp <= 0 || !options.Any()) return;
                root.DropDownItems.Add(new WinForms.ToolStripSeparator());
                foreach (var kv in options)
                {
                    string value = kv.Key;
                    var item = new WinForms.ToolStripMenuItem(kv.Value);
                    item.Click += (s, a) => Run(set(value));
                    items[value] = item;
                    root.DropDownItems.Add(item);
                }
            }
            AddOptions(cfg.Dps.Mode, cfg.Modes, modes, ac.SetModeAsync);
            AddOptions(cfg.Dps.Fan, cfg.Fans, fans, v => ac.SetFanAsync(v));

            var toggles = new Dictionary<int, WinForms.ToolStripMenuItem>();
            foreach (var (dp, name) in ac.Toggles())
            {
                if (toggles.Count == 0) root.DropDownItems.Add(new WinForms.ToolStripSeparator());
                var item = new WinForms.ToolStripMenuItem(name);
                item.Click += (s, a) => Run(ac.SetToggleAsync(dp, !ac.IsToggleOn(dp)));
                toggles[dp] = item;
                root.DropDownItems.Add(item);
            }

            void Refresh()
            {
                root.Text = ac.Name + " · " + ac.Summary();
                power.Checked = ac.Power;
                temp.Text = "Objetivo: " + (ac.Setpoint.HasValue ? ac.FormatTemp(ac.Setpoint.Value) : "--°") +
                            (ac.Temp.HasValue ? " · ambiente " + ac.Temp.Value.ToString("0.#", CultureInfo.CurrentCulture) + "°" : "");
                foreach (var kv in modes) kv.Value.Checked = kv.Key == ac.Mode;
                foreach (var kv in fans) kv.Value.Checked = kv.Key == ac.Fan;
                foreach (var kv in toggles) kv.Value.Checked = ac.IsToggleOn(kv.Key);
            }
            ac.Changed += Refresh;
            Refresh();
            return root;
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

        // Sistema de ventilación al iniciar: enciende cada aire en cuanto responde, si eso pasa
        // dentro de StartupModeWindow (como el nivel del ventilador)
        private async Task TurnOnAtStartupAsync(AirConditioner ac)
        {
            while (DateTime.Now - _startedAt < StartupModeWindow)
            {
                await ac.RefreshAsync();
                if (ac.Online)
                {
                    if (!ac.Power) await ac.SetPowerAsync(true);
                    return;
                }
                await Task.Delay(TimeSpan.FromSeconds(30));
            }
        }

        // Al apagar Windows o cerrar sesión: el ventilador pasa al modo configurado (por defecto,
        // apagado) y los aires se apagan, todo a la vez y sin pasar de ShutdownModeTimeout
        private void ApplyShutdownMode()
        {
            int mode = _config.ShutdownMode;
            if (mode < 0 || mode > 7) return;
            var deadline = DateTime.Now + ShutdownModeTimeout;
            var acs = _acs.Select(ac => ac.SetPowerDetachedAsync(false)).ToArray();
            if (_client.State == null || _client.State.Mode != mode)
                _client.SetModeBlocking(mode, ShutdownModeTimeout);
            var left = deadline - DateTime.Now;
            if (acs.Length > 0 && left > TimeSpan.Zero)
            {
                try { Task.WaitAll(acs, left); } catch { }
            }
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
            _panel = new PanelWindow(_client, _acs);
            _panel.Unavailable += NotifyUnavailable;
            _panel.Closed += (s, a) => { _panel = null; _panelClosedAt = DateTime.Now; };
            _panel.Show();
        }

        private void NotifyUnavailable()
        {
            Notify($"No se pudo conectar con Vento. Comprueba que está encendido y en la misma red WiFi ({_client.WebUrl}).");
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
