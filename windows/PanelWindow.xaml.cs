using System;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;

namespace Vento
{
    // Ventana pequeña junto a la bandeja: lecturas, Auto / Progresivo y temperatura objetivo
    public partial class PanelWindow : Window
    {
        public event Action Unavailable;

        private readonly VentoClient _client;
        private readonly DispatcherTimer _setpointDebounce;
        private bool _updating;
        private bool _allowClose;

        public PanelWindow(VentoClient client)
        {
            InitializeComponent();
            _client = client;
            _setpointDebounce = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
            _setpointDebounce.Tick += async (s, e) =>
            {
                _setpointDebounce.Stop();
                if (!await _client.SetSetpointAsync((int)SetpointSlider.Value)) Unavailable?.Invoke();
            };
            _client.Changed += Refresh;
            Closed += (s, e) => _client.Changed -= Refresh;
            Refresh();
            // Por Bluetooth el estado no se actualiza solo: se pide al abrir la ventana
            if (_client.Status == LinkStatus.Bluetooth) _ = _client.RefreshAsync();
        }

        private void Refresh()
        {
            var state = _client.State;
            bool connected = _client.IsConnected;

            StatusText.Text = VentoClient.StatusText(_client.Status);
            Dot.Fill = new SolidColorBrush(connected
                ? Color.FromRgb(0x22, 0xC5, 0x5E)
                : Color.FromRgb(0xEF, 0x44, 0x44));

            if (state == null)
            {
                ModeText.Text = "";
                return;
            }

            HicText.Text = state.Hic.HasValue ? state.Hic.Value.ToString("0.0", CultureInfo.CurrentCulture) + "°" : "--°";
            SubText.Text = Fmt(state.Temp, " °C") + " · " + Fmt(state.Hum, " %");
            AutoButton.Tag = state.Mode == 6 ? "sel" : null;
            ProgressiveButton.Tag = state.Mode == 7 ? "sel" : null;
            ModeText.Text = "Modo actual: " + VentoState.ModeName(state.Mode) +
                            " · " + Math.Round(state.Pwm / 2.55) + " %";

            // No pisa el valor mientras el usuario lo está cambiando
            if (!_setpointDebounce.IsEnabled && !SetpointSlider.IsMouseCaptureWithin)
            {
                _updating = true;
                SetpointSlider.Value = state.Setpoint;
                _updating = false;
                SetpointText.Text = state.Setpoint + "°";
            }
        }

        private static string Fmt(double? v, string unit) =>
            v.HasValue ? v.Value.ToString("0.0", CultureInfo.CurrentCulture) + unit : "--" + unit;

        private async void OnAuto(object sender, RoutedEventArgs e)
        {
            if (!await _client.SetModeAsync(6)) Unavailable?.Invoke();
        }

        private async void OnProgressive(object sender, RoutedEventArgs e)
        {
            if (!await _client.SetModeAsync(7)) Unavailable?.Invoke();
        }

        private async void OnOff(object sender, RoutedEventArgs e)
        {
            if (!await _client.SetModeAsync(0)) Unavailable?.Invoke();
        }

        private void OnSetpointChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (_setpointDebounce == null) return; // durante InitializeComponent
            SetpointText.Text = (int)e.NewValue + "°";
            if (_updating) return;
            _setpointDebounce.Stop();
            _setpointDebounce.Start();
        }

        // ------------------------------------------------------------ ventana
        private void OnLoaded(object sender, RoutedEventArgs e)
        {
            PositionNearTray();
            var hwnd = new WindowInteropHelper(this).Handle;
            SetForegroundWindow(hwnd);
            Activate();

            // Evita que un "deactivate" transitorio al abrirse la cierre de inmediato
            var t = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
            t.Tick += (s, a) => { t.Stop(); _allowClose = true; };
            t.Start();
        }

        private void PositionNearTray()
        {
            GetCursorPos(out var p);
            var dpi = VisualTreeHelper.GetDpi(this);
            double x = p.X / dpi.DpiScaleX;
            double y = p.Y / dpi.DpiScaleY;
            var area = SystemParameters.WorkArea;

            // Esquina inferior derecha junto al cursor, dentro del área de trabajo
            double left = x - ActualWidth + 40;
            double top = y - ActualHeight + 14;
            Left = Math.Max(area.Left, Math.Min(left, area.Right - ActualWidth));
            Top = Math.Max(area.Top, Math.Min(top, area.Bottom - ActualHeight));
        }

        private void OnDeactivated(object sender, EventArgs e)
        {
            if (_allowClose) Close();
        }

        private void OnPreviewKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Escape) Close();
        }

        private void OnHeaderMouseDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ButtonState == MouseButtonState.Pressed) DragMove();
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct POINT { public int X, Y; }

        [DllImport("user32.dll")]
        private static extern bool GetCursorPos(out POINT p);

        [DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(IntPtr hWnd);
    }
}
