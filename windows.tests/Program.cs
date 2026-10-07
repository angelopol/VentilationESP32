using System.Text.Json;
using Vento;

static void Check(bool condition, string message)
{
    if (!condition) throw new Exception(message);
    Console.WriteLine("PASS " + message);
}
static ShortcutBinding Binding(string action, params int[] keys) => new() { Action = action, Keys = keys.ToList() };

var large = Binding("fan.up", 16, 17, 18, 91, 65, 66, 119);
Check(ShortcutMatcher.Validate(new[] { large }) == null, "Accept seven-key chord with multiple ordinary keys");
Check(ShortcutMatcher.Validate(new[] { Binding("a", 65) }) != null, "Reject bare letter");
Check(ShortcutMatcher.Validate(new[] { Binding("a", 16, 17) }) != null, "Reject modifiers only");
Check(ShortcutMatcher.Validate(new[] { Binding("a", 17, 27) }) != null, "Reserve Escape for capture cancellation");
Check(ShortcutMatcher.Validate(new[] { large, Binding("b", 17, 65) }) != null, "Reject subset conflicts");
Check(ShortcutMatcher.Validate(new[] { Binding("b", 17, 65), large }) != null, "Reject superset conflicts regardless of order");
Check(ShortcutMatcher.Validate(new[] { Binding("a", 17, 65), Binding("b", 65, 17) }) != null, "Reject duplicate chord in different order");
Check(ShortcutMatcher.Validate(new[] { Binding("a", 17, 65), Binding("b", 17, 66) }) == null, "Allow independent combinations");
Check(ShortcutMatcher.Validate(new[] { Binding("a") }) == null, "Allow unassigned action");
Check(ShortcutMatcher.Normalize(162) == 17 && ShortcutMatcher.Normalize(163) == 17 && ShortcutMatcher.Normalize(92) == 91,
    "Normalize left and right modifiers");

var matcher = new ShortcutMatcher { Bindings = new() { large } };
foreach (int key in large.Keys.Take(large.Keys.Count - 1))
    Check(matcher.Update(key, true) == null, "Partial combination does not run action: " + key);
Check(matcher.Update(119, true) == "fan.up", "Full chord fires action");
Check(matcher.Update(119, true) == null, "Ignore keyboard autorepeat");
matcher.Update(119, false);
Check(matcher.Update(119, true) == null, "Do not retrigger until the chord is fully released");
foreach (int key in large.Keys) matcher.Update(key, false);
foreach (int key in large.Keys.AsEnumerable().Reverse().SkipLast(1)) matcher.Update(key, true);
Check(matcher.Update(16, true) == "fan.up", "Rearm after release and allow different key order");
matcher.Reset();
matcher.Update(90, true);
foreach (int key in large.Keys) Check(matcher.Update(key, true) == null, "Ignore chord with extra key: " + key);
matcher.Reset();
Check(matcher.Down.Count == 0, "Reset clears stuck keys");

var ac = new AcConfig();
Check(ac.ResolveMode(true) == "cold" && ac.ResolveMode(false) == "wind", "Resolve default cold and fan modes");
ac.Modes = new() { ["Cool"] = "Refrigerar", ["Fan"] = "Ventilar" };
Check(ac.ResolveMode(true) == "Cool" && ac.ResolveMode(false) == "Fan", "Resolve case-sensitive device values using aliases");
ac.Modes = new() { ["0"] = "Frío", ["1"] = "Ventilador", ["2"] = "Especial" };
Check(ac.ResolveMode(true) == "0" && ac.ResolveMode(false) == "1", "Resolve manufacturer values by labels");
ac.CoolMode = "2";
Check(ac.ResolveMode(true) == "2", "Explicit mode mapping overrides inference");
ac.CoolMode = "missing";
Check(ac.ResolveMode(true) == null, "Never send unknown cold mode");
ac.CoolMode = "";
Check(ac.ResolveMode(true) == null, "Respect unconfigured mode selection");
var config = new Config { Shortcuts = new() { large }, AirConditioners = new() { ac } };
var restored = JsonSerializer.Deserialize<Config>(JsonSerializer.Serialize(config));
Check(restored.Shortcuts[0].Keys.SequenceEqual(large.Keys) && restored.AirConditioners[0].CoolMode == "",
    "Persist large chords and explicit mode choices in JSON");
Console.WriteLine("All shortcut checks passed.");
