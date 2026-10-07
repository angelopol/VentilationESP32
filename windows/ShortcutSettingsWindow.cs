using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace Vento
{
    public sealed class ShortcutSettingsWindow : Window
    {
        private readonly Config _config;
        private readonly GlobalShortcuts _shortcuts;
        private readonly List<ShortcutBinding> _draft;
        private readonly Dictionary<string, Button> _buttons = new();
        private readonly List<(AcConfig Config, ComboBox Cool, ComboBox Fan)> _modes = new();
        private readonly TextBlock _status = new() { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 12, 0, 12) };
        private ShortcutBinding _recording;

        public ShortcutSettingsWindow(Config config, GlobalShortcuts shortcuts, IReadOnlyList<ShortcutAction> actions,
                                      IReadOnlyList<AirConditioner> acs)
        {
            _config = config;
            _shortcuts = shortcuts;
            Title = "Vento · Configuración de atajos";
            Width = 850; Height = 700; MinWidth = 620; MinHeight = 400;
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
            Background = new SolidColorBrush(Color.FromRgb(245, 247, 250));
            _draft = actions.Select(a => new ShortcutBinding { Action = a.Id,
                Keys = config.Shortcuts.FirstOrDefault(b => b?.Action == a.Id)?.Keys?.ToList() ?? new() }).ToList();
            // Preserve assignments for devices that are temporarily absent from the configuration.
            foreach (var b in config.Shortcuts.Where(b => b != null && !string.IsNullOrEmpty(b.Action) && !actions.Any(a => a.Id == b.Action)))
                _draft.Add(new ShortcutBinding { Action = b.Action, Keys = b.Keys?.ToList() ?? new() });

            var root = new DockPanel { Margin = new Thickness(22) };
            Content = root;
            var footer = new StackPanel();
            DockPanel.SetDock(footer, Dock.Bottom);
            root.Children.Add(footer);
            footer.Children.Add(_status);
            var commands = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right };
            var cancel = MakeButton("Cancelar", () => Close());
            var save = MakeButton("Guardar", Save);
            save.IsEnabled = !config.LoadFailed;
            commands.Children.Add(cancel); commands.Children.Add(save);
            footer.Children.Add(commands);
            var content = new StackPanel();
            root.Children.Add(new ScrollViewer { Content = content, VerticalScrollBarVisibility = ScrollBarVisibility.Auto });
            content.Children.Add(new TextBlock { Text = "Atajos de teclado", FontSize = 25, FontWeight = FontWeights.SemiBold });
            content.Children.Add(new TextBlock
            {
                Text = "Pulsa Asignar y luego pulsa las teclas de la combinación una a una (no hace falta mantenerlas): cada tecla se suma. Enter guarda la combinación y Esc cancela. Usa Ctrl, Alt, Shift o Win junto con una o varias teclas (ej.: Ctrl + Alt + Shift + F8 + A).\n\nFuncionan con Vento en la bandeja. Se ejecutan una vez por pulsación; suelta toda la combinación antes de repetir. El teclado puede limitar cuántas teclas reconoce a la vez. Evita atajos reservados por Windows u otras apps: las teclas también llegan a ellas. Los atajos están pausados mientras esta ventana está abierta.",
                TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 12, 8, 18),
            });
            foreach (var ac in acs.Where(ac => ac.Config.Dps.Mode > 0))
            {
                content.Children.Add(new TextBlock { Text = ac.Name + " · Modos del equipo", FontWeight = FontWeights.SemiBold, Margin = new Thickness(0, 12, 0, 6) });
                var row = new WrapPanel();
                ComboBox ModeBox(string label, string selected)
                {
                    row.Children.Add(new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 8, 0) });
                    var box = new ComboBox { Width = 180, Margin = new Thickness(0, 0, 16, 0),
                        DisplayMemberPath = "Value", SelectedValuePath = "Key" };
                    box.Items.Add(new KeyValuePair<string, string>("", "Sin configurar"));
                    foreach (var kv in ac.Config.Modes) box.Items.Add(kv);
                    box.SelectedValue = selected ?? "";
                    row.Children.Add(box);
                    return box;
                }
                var cool = ModeBox("Frío:", ac.Config.ResolveMode(true));
                var fan = ModeBox("Ventilador:", ac.Config.ResolveMode(false));
                _modes.Add((ac.Config, cool, fan));
                content.Children.Add(row);
            }
            content.Children.Add(new TextBlock { Text = "Acciones", FontSize = 18, Margin = new Thickness(0, 20, 0, 10) });
            foreach (var binding in _draft)
            {
                var row = new Grid { Margin = new Thickness(0, 0, 0, 7) };
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(260) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                row.Children.Add(new TextBlock { Text = actions.FirstOrDefault(a => a.Id == binding.Action)?.Label ?? "No disponible · " + binding.Action,
                    VerticalAlignment = VerticalAlignment.Center, TextWrapping = TextWrapping.Wrap });
                var assign = MakeButton("", () => Record(binding));
                _buttons[binding.Action] = assign;
                Grid.SetColumn(assign, 1); row.Children.Add(assign);
                var clear = MakeButton("Quitar", () => { StopRecording(); binding.Keys.Clear(); RefreshButtons(); });
                Grid.SetColumn(clear, 2); row.Children.Add(clear);
                content.Children.Add(row);
            }
            _shortcuts.Suspended = true;
            _shortcuts.Captured += Captured;
            _shortcuts.CaptureChanged += CaptureChanged;
            Deactivated += (s, e) => StopRecording();
            Closed += (s, e) =>
            {
                StopRecording();
                _shortcuts.Captured -= Captured;
                _shortcuts.CaptureChanged -= CaptureChanged;
                _shortcuts.Suspended = false;
            };
            _status.Text = config.LoadFailed ? "config.json tiene errores. Corrígelo y reinicia Vento para guardar." : "Sin atajo significa que la acción solo se ejecuta desde sus controles habituales.";
            RefreshButtons();
        }
        private static Button MakeButton(string label, Action action)
        {
            var b = new Button { Content = label, Padding = new Thickness(10, 7, 10, 7), Margin = new Thickness(4, 0, 0, 0) };
            b.Click += (s, e) => action();
            return b;
        }
        private void RefreshButtons()
        {
            foreach (var b in _draft)
                _buttons[b.Action].Content = new TextBlock { Text = b.Keys.Count == 0 ? "Asignar…" : ShortcutMatcher.Format(b.Keys), TextWrapping = TextWrapping.Wrap };
        }
        private void StopRecording()
        {
            _recording = null;
            _shortcuts.CancelCapture();
            RefreshButtons();
        }
        private void Record(ShortcutBinding binding)
        {
            StopRecording();
            _recording = binding;
            _buttons[binding.Action].Content = "Pulsa la combinación…";
            _status.Text = "Pulsa las teclas una a una; se van sumando a la combinación. Enter guarda, Esc cancela.";
            _shortcuts.BeginCapture();
        }
        private void CaptureChanged(string text)
        {
            if (_recording != null) _buttons[_recording.Action].Content = text;
        }
        private void Captured(List<int> keys)
        {
            if (_recording == null) return;
            var binding = _recording;
            _recording = null;
            if (keys != null)
            {
                var previous = binding.Keys;
                binding.Keys = keys;
                string error = ShortcutMatcher.Validate(_draft);
                if (error != null) binding.Keys = previous;
                _status.Text = error ?? "Atajo asignado. Pulsa Guardar para aplicarlo.";
            }
            else _status.Text = "Grabación cancelada.";
            RefreshButtons();
        }
        private void Save()
        {
            StopRecording();
            string error = ShortcutMatcher.Validate(_draft);
            if (error != null) { _status.Text = error; return; }
            foreach (var mode in _modes)
            {
                bool Assigned(string suffix) => _draft.Any(b => b.Action == "ac." + mode.Config.Id + "." + suffix && b.Keys.Count > 0);
                if ((Assigned("temp.up") || Assigned("temp.down")) && string.IsNullOrEmpty(mode.Cool.SelectedValue as string) ||
                    Assigned("fanMode") && string.IsNullOrEmpty(mode.Fan.SelectedValue as string))
                { _status.Text = "Selecciona los modos frío y ventilador necesarios para «" + mode.Config.Name + "»."; return; }
            }
            var oldBindings = _config.Shortcuts;
            var oldModes = _modes.Select(m => (m.Config, m.Config.CoolMode, m.Config.FanMode)).ToList();
            foreach (var mode in _modes)
            {
                mode.Config.CoolMode = mode.Cool.SelectedValue as string;
                mode.Config.FanMode = mode.Fan.SelectedValue as string;
            }
            _config.Shortcuts = _draft.Where(b => b.Keys.Count > 0).ToList();
            if (!_config.TrySave(out error))
            {
                _config.Shortcuts = oldBindings;
                foreach (var old in oldModes) { old.Config.CoolMode = old.CoolMode; old.Config.FanMode = old.FanMode; }
                _status.Text = "No se pudo guardar: " + error;
                return;
            }
            _shortcuts.SetBindings(_config.Shortcuts);
            Close();
        }
    }
}
