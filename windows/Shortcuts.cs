using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Threading;
using Keys = System.Windows.Forms.Keys;

namespace Vento
{
    public sealed class ShortcutBinding
    {
        public string Action { get; set; }
        public List<int> Keys { get; set; } = new();
    }

    // Pure chord matching, shared by configuration validation and the global keyboard hook.
    public sealed class ShortcutMatcher
    {
        private readonly HashSet<int> _down = new();
        private bool _fired;
        public IReadOnlyCollection<int> Down => _down;
        public List<ShortcutBinding> Bindings { get; set; } = new();
        public static int Normalize(int key) => key switch
        {
            160 or 161 => 16, 162 or 163 => 17, 164 or 165 => 18, 92 => 91, _ => key,
        };
        public static bool Modifier(int key) => key is 16 or 17 or 18 or 91;
        public static string Format(IEnumerable<int> keys) => string.Join(" + ", keys.OrderBy(k => Modifier(k) ? 0 : 1)
            .ThenBy(k => k).Select(k => k switch
            {
                16 => "Shift", 17 => "Ctrl", 18 => "Alt", 91 => "Win", _ => ((Keys)k).ToString(),
            }));
        public static string Validate(IReadOnlyList<ShortcutBinding> bindings)
        {
            for (int i = 0; i < bindings.Count; i++)
            {
                var b = bindings[i];
                if (b == null || string.IsNullOrWhiteSpace(b.Action) || b.Keys == null) return "Hay un atajo inválido.";
                var keys = b.Keys;
                if (keys.Count == 0) continue;
                if (keys.Any(k => k < 8 || k > 254 || Normalize(k) != k || k == 27) ||
                    keys.Distinct().Count() != keys.Count || !keys.Any(Modifier) || keys.All(Modifier))
                    return "Cada atajo necesita Ctrl, Alt, Shift o Win y al menos otra tecla. Esc está reservado para cancelar.";
                for (int j = 0; j < i; j++)
                {
                    var other = bindings[j];
                    if (other.Keys.Count > 0 && (keys.All(other.Keys.Contains) || other.Keys.All(keys.Contains)))
                        return $"Los atajos «{Format(keys)}» y «{Format(other.Keys)}» se solapan. Usa combinaciones independientes.";
                }
            }
            return null;
        }
        public string Update(int key, bool pressed)
        {
            if (!pressed)
            {
                _down.Remove(key);
                if (_down.Count == 0) _fired = false;
                return null;
            }
            if (!_down.Add(key) || _fired) return null;
            var match = Bindings.FirstOrDefault(b => b.Keys.Count > 0 && _down.SetEquals(b.Keys));
            if (match == null) return null;
            _fired = true;
            return match.Action;
        }
        public void Reset() { _down.Clear(); _fired = false; }
    }

    public sealed class GlobalShortcuts : IDisposable
    {
        private readonly HookProc _callback;
        private readonly Dispatcher _dispatcher;
        private readonly HashSet<int> _physical = new();
        private IntPtr _hook;
        private readonly ShortcutMatcher _matcher = new();
        private HashSet<int> _recorded;
        public bool Suspended { get; set; }
        public event Action<string> Triggered;
        public event Action<List<int>> Captured;
        public event Action<string> CaptureChanged;

        public GlobalShortcuts(Dispatcher dispatcher)
        {
            _dispatcher = dispatcher;
            _callback = Hook;
            _hook = SetWindowsHookEx(13, _callback, GetModuleHandle(null), 0);
            if (_hook == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        public void SetBindings(List<ShortcutBinding> bindings)
        {
            _matcher.Bindings = bindings;
            Reset();
        }
        private void Reset() { _physical.Clear(); _matcher.Reset(); }
        public void BeginCapture() { Reset(); _recorded = new(); }
        public void CancelCapture() { _recorded = null; Reset(); }

        private IntPtr Hook(int code, IntPtr message, IntPtr data)
        {
            if (code < 0) return CallNextHookEx(_hook, code, message, data);
            var input = Marshal.PtrToStructure<KeyboardInput>(data);
            // Ignore synthetic keystrokes (including our own or another application's).
            if ((input.Flags & 0x10) != 0) return CallNextHookEx(_hook, code, message, data);
            int msg = message.ToInt32();
            bool down = msg is 0x100 or 0x104;
            if (!down && msg is not (0x101 or 0x105)) return CallNextHookEx(_hook, code, message, data);
            int physical = (int)input.Key, key = ShortcutMatcher.Normalize(physical);
            if (down)
            {
                // Recover keys released on the lock screen or another desktop.
                foreach (int stale in _physical.Where(k => (GetAsyncKeyState(k) & 0x8000) == 0 && k != physical).ToArray())
                {
                    _physical.Remove(stale);
                    if (!_physical.Any(k => ShortcutMatcher.Normalize(k) == ShortcutMatcher.Normalize(stale)))
                        _matcher.Update(ShortcutMatcher.Normalize(stale), false);
                }
                _physical.Add(physical);
            }
            else _physical.Remove(physical);
            bool normalizedDown = down || _physical.Any(k => ShortcutMatcher.Normalize(k) == key);
            if (_recorded != null)
            {
                if (down && key == 27)
                {
                    CancelCapture();
                    _dispatcher.BeginInvoke(new Action(() => Captured?.Invoke(null)));
                }
                else
                {
                    _matcher.Update(key, normalizedDown);
                    if (down && _matcher.Down.Count > _recorded.Count) _recorded = new(_matcher.Down);
                    string label = ShortcutMatcher.Format(_recorded);
                    _dispatcher.BeginInvoke(new Action(() => CaptureChanged?.Invoke(label)));
                    if (_physical.Count == 0)
                    {
                        var result = _recorded.OrderBy(k => k).ToList();
                        CancelCapture();
                        _dispatcher.BeginInvoke(new Action(() => Captured?.Invoke(result)));
                    }
                }
                return new IntPtr(1);
            }
            if (!Suspended)
            {
                string action = _matcher.Update(key, normalizedDown);
                if (action != null) _dispatcher.BeginInvoke(new Action(() => { if (!Suspended) Triggered?.Invoke(action); }));
            }
            // Do not interfere with typing or keyboard shortcuts in other applications.
            return CallNextHookEx(_hook, code, message, data);
        }
        public void Dispose()
        {
            if (_hook != IntPtr.Zero) UnhookWindowsHookEx(_hook);
            _hook = IntPtr.Zero;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct KeyboardInput { public uint Key, ScanCode, Flags, Time; public UIntPtr ExtraInfo; }
        private delegate IntPtr HookProc(int code, IntPtr message, IntPtr data);
        [DllImport("user32.dll", SetLastError = true)] private static extern IntPtr SetWindowsHookEx(int id, HookProc proc, IntPtr module, uint thread);
        [DllImport("user32.dll")] private static extern bool UnhookWindowsHookEx(IntPtr hook);
        [DllImport("user32.dll")] private static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr message, IntPtr data);
        [DllImport("user32.dll")] private static extern short GetAsyncKeyState(int key);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr GetModuleHandle(string name);
    }
}
