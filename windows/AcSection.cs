using System;
using System.Collections.Generic;
using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;

namespace Vento
{
    // Bloque del panel para un aire acondicionado: encendido, temperatura, modo y ventilador
    public sealed class AcSection : StackPanel
    {
        private static readonly Brush White = Brushes.White;
        private static readonly Brush ErrorBrush = new SolidColorBrush(Color.FromRgb(0xFC, 0xA5, 0xA5));

        private readonly AirConditioner _ac;
        private readonly DispatcherTimer _debounce;
        private readonly TextBlock _temp, _status, _setpoint, _error;
        private readonly Button _power;
        private readonly StackPanel _controls, _modeBox, _fanBox;
        private readonly Dictionary<string, Button> _modeButtons = new Dictionary<string, Button>();
        private readonly Dictionary<string, Button> _fanButtons = new Dictionary<string, Button>();
        private readonly Dictionary<int, Button> _toggleButtons = new Dictionary<int, Button>();
        private double? _pendingSetpoint;

        public AcSection(AirConditioner ac)
        {
            _ac = ac;
            Margin = new Thickness(0, 18, 0, 0);
            Children.Add(new Border
            {
                Height = 1, Margin = new Thickness(0, 0, 0, 14),
                Background = new SolidColorBrush(Color.FromArgb(0x26, 0xFF, 0xFF, 0xFF)),
            });

            // Nombre y botón de encendido
            var head = new Grid();
            var title = new StackPanel();
            title.Children.Add(Label("AIRE ACONDICIONADO"));
            title.Children.Add(new TextBlock { Text = ac.Name, FontSize = 15, FontWeight = FontWeights.SemiBold, Foreground = White });
            head.Children.Add(title);
            _power = new Button
            {
                Style = (Style)Application.Current.FindResource("ModeButton"),
                HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Center,
                ToolTip = "Encender / apagar",
                Content = new TextBlock { Text = "", FontFamily = new FontFamily("Segoe Fluent Icons, Segoe MDL2 Assets"), FontSize = 16 },
            };
            _power.Click += async (s, e) => await _ac.SetPowerAsync(!_ac.Power);
            head.Children.Add(_power);
            Children.Add(head);

            // Temperatura ambiente y objetivo
            var temps = new Grid { Margin = new Thickness(0, 10, 0, 0) };
            var room = new StackPanel();
            _temp = new TextBlock { Text = "--°", FontSize = 30, FontWeight = FontWeights.Bold, Foreground = White };
            _status = new TextBlock { FontSize = 12, Foreground = (Brush)Application.Current.FindResource("Muted") };
            room.Children.Add(_temp);
            room.Children.Add(_status);
            temps.Children.Add(room);

            var stepper = new StackPanel
            {
                Orientation = Orientation.Horizontal,
                HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Center,
            };
            _setpoint = new TextBlock
            {
                Text = "--°", FontSize = 20, FontWeight = FontWeights.SemiBold, Foreground = White,
                MinWidth = 52, TextAlignment = TextAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
            };
            stepper.Children.Add(StepButton("−", -1));
            stepper.Children.Add(_setpoint);
            stepper.Children.Add(StepButton("+", 1));
            temps.Children.Add(stepper);
            Children.Add(temps);

            // Modo y velocidad del ventilador
            _controls = new StackPanel();
            _modeBox = Options("MODO", ac.Config.Modes, _modeButtons, async v => await _ac.SetModeAsync(v));
            _fanBox = Options("VENTILADOR", ac.Config.Fans, _fanButtons, async v => await _ac.SetFanAsync(v));
            _modeBox.Visibility = ac.Config.Dps.Mode > 0 && ac.Config.Modes.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
            _fanBox.Visibility = ac.Config.Dps.Fan > 0 && ac.Config.Fans.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
            _controls.Children.Add(_modeBox);
            _controls.Children.Add(_fanBox);

            // Interruptores extra (oscilación, sueño...)
            var toggles = new WrapPanel { Margin = new Thickness(0, 8, 0, 0) };
            foreach (var (dp, name) in ac.Toggles())
            {
                var b = new Button
                {
                    Style = (Style)Application.Current.FindResource("ModeButton"),
                    Margin = new Thickness(0, 0, 6, 6),
                    Content = new TextBlock { Text = name, FontSize = 13, FontWeight = FontWeights.SemiBold },
                };
                b.Click += async (s, e) => await _ac.SetToggleAsync(dp, !_ac.IsToggleOn(dp));
                _toggleButtons[dp] = b;
                toggles.Children.Add(b);
            }
            if (_toggleButtons.Count > 0) _controls.Children.Add(toggles);
            Children.Add(_controls);

            _error = new TextBlock { FontSize = 11, Foreground = ErrorBrush, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 8, 0, 0) };
            Children.Add(_error);

            // Espera a que se deje de pulsar +/- para mandar una sola orden
            _debounce = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(600) };
            _debounce.Tick += async (s, e) =>
            {
                _debounce.Stop();
                if (_pendingSetpoint is double t)
                {
                    _pendingSetpoint = null;
                    await _ac.SetSetpointAsync(t);
                }
            };

            _ac.Changed += Refresh;
            _ac.Watch();
            Refresh();
        }

        // Al cerrar el panel
        public void Detach()
        {
            _ac.Changed -= Refresh;
            _ac.Unwatch();
            _debounce.Stop();
        }

        private void Refresh()
        {
            _power.Tag = _ac.Power ? "sel" : null;
            _temp.Text = _ac.Temp.HasValue
                ? _ac.Temp.Value.ToString("0.0", CultureInfo.CurrentCulture) + "°"
                : "--°";
            _status.Text = (_ac.Temp.HasValue && _ac.Online ? "Ambiente · " : "") + _ac.Summary();
            var sp = _pendingSetpoint ?? _ac.Setpoint;
            _setpoint.Text = sp.HasValue ? _ac.FormatTemp(sp.Value) : "--°";
            foreach (var kv in _modeButtons) kv.Value.Tag = kv.Key == _ac.Mode ? "sel" : null;
            foreach (var kv in _fanButtons) kv.Value.Tag = kv.Key == _ac.Fan ? "sel" : null;
            foreach (var kv in _toggleButtons) kv.Value.Tag = _ac.IsToggleOn(kv.Key) ? "sel" : null;
            _controls.Opacity = _ac.Online && _ac.Power ? 1 : 0.45;
            _error.Text = _ac.Error ?? "";
            _error.Visibility = string.IsNullOrEmpty(_error.Text) ? Visibility.Collapsed : Visibility.Visible;
        }

        private Button StepButton(string text, int direction)
        {
            var b = new Button
            {
                Style = (Style)Application.Current.FindResource("ModeButton"),
                Content = new TextBlock { Text = text, FontSize = 16, FontWeight = FontWeights.SemiBold, Width = 14, TextAlignment = TextAlignment.Center },
            };
            b.Click += (s, e) =>
            {
                var cfg = _ac.Config;
                double t = _pendingSetpoint ?? _ac.Setpoint ?? cfg.Min;
                t = Math.Clamp(Math.Round((t + direction * cfg.Step) * 10) / 10, cfg.Min, cfg.Max);
                _pendingSetpoint = t;
                _setpoint.Text = _ac.FormatTemp(t);
                _debounce.Stop();
                _debounce.Start();
            };
            return b;
        }

        private static StackPanel Options(string label, IEnumerable<KeyValuePair<string, string>> options,
                                          Dictionary<string, Button> buttons, Action<string> pick)
        {
            var box = new StackPanel();
            box.Children.Add(Label(label, new Thickness(0, 14, 0, 6)));
            var wrap = new WrapPanel();
            foreach (var kv in options)
            {
                string value = kv.Key;
                var b = new Button
                {
                    Style = (Style)Application.Current.FindResource("ModeButton"),
                    Margin = new Thickness(0, 0, 6, 6),
                    Content = new TextBlock { Text = kv.Value, FontSize = 13, FontWeight = FontWeights.SemiBold },
                };
                b.Click += (s, e) => pick(value);
                buttons[value] = b;
                wrap.Children.Add(b);
            }
            box.Children.Add(wrap);
            return box;
        }

        private static TextBlock Label(string text, Thickness? margin = null) => new TextBlock
        {
            Text = text,
            Style = (Style)Application.Current.FindResource("Label"),
            Margin = margin ?? new Thickness(0),
        };
    }
}
