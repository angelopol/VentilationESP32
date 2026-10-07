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
var fanConfig = new AcConfig();
Check(fanConfig.ResolveFan(false) == "low" && fanConfig.ResolveFan(true) == "high", "Resolve default low and high speeds");
var startFan = AirConditioner.FanCycleCommand(fanConfig, false, "cold", "high");
Check(startFan.Count == 3 && (bool)startFan[1] && (string)startFan[4] == "wind" && (string)startFan[5] == "low",
    "Off air conditioner targets power on, fan mode and low speed");
Check((string)AirConditioner.FanCycleCommand(fanConfig, true, "cold", "high")[5] == "low",
    "Cooling switches to fan mode at low speed");
Check((string)AirConditioner.FanCycleCommand(fanConfig, true, "wind", "low")[5] == "high",
    "Active low fan switches to high");
Check((string)AirConditioner.FanCycleCommand(fanConfig, true, "wind", "high")[5] == "low",
    "Active high fan switches to low");
Check((string)AirConditioner.FanCycleCommand(fanConfig, true, "wind", "auto")[5] == "high",
    "Other active fan speeds switch to high");
fanConfig.Fans = new() { ["1"] = "Baja", ["3"] = "Alta" };
Check(fanConfig.ResolveFan(false) == "1" && fanConfig.ResolveFan(true) == "3", "Resolve manufacturer speeds by label");
fanConfig.Fans = new() { ["L"] = "Especial uno", ["H"] = "Especial dos" };
Check(AirConditioner.FanCycleCommand(fanConfig, false, null, null) == null, "Reject unresolved speeds without sending partial commands");
fanConfig.LowFan = "L"; fanConfig.HighFan = "H";
Check((string)AirConditioner.FanCycleCommand(fanConfig, true, "wind", "L")[5] == "H", "Honor explicit speed mappings");
var savedFan = JsonSerializer.Deserialize<AcConfig>(JsonSerializer.Serialize(fanConfig));
Check(savedFan.LowFan == "L" && savedFan.HighFan == "H", "Persist speed mappings");
fanConfig.HighFan = "L";
Check(AirConditioner.FanCycleCommand(fanConfig, true, "wind", "L") == null, "Reject identical low and high mappings");
fanConfig.HighFan = "missing";
Check(fanConfig.ResolveFan(true) == null, "Reject unknown configured speed");
fanConfig.HighFan = "";
Check(fanConfig.ResolveFan(true) == null, "Respect unconfigured speed selection");
fanConfig.HighFan = "H";
fanConfig.Dps.Power = 0;
Check(AirConditioner.FanCycleCommand(fanConfig, false, null, null) == null, "Require power DP for fan cycle");

// Simulate a device that resets its speed on every mode write and ignores fan
// speed when it is included in a command with other DPs.
var deviceConfig = new AcConfig
{
    Id = "test", Key = "0123456789abcdef", Ip = "127.0.0.1",
    Modes = new() { ["Cool"] = "Frío", ["Fan"] = "Ventilador" },
    Fans = new() { ["Low"] = "Baja", ["High"] = "Alta" },
};
bool devicePower = false;
string deviceMode = "Cool", deviceFan = "High";
var writes = new List<Dictionary<string, object>>();
Task<JsonElement> Query(IEnumerable<int> requested) => Task.FromResult(JsonSerializer.SerializeToElement(
    new Dictionary<string, object> { ["1"] = devicePower, ["4"] = deviceMode, ["5"] = deviceFan }));
Task Send(IDictionary<string, object> values)
{
    writes.Add(new(values));
    if (values.TryGetValue("1", out var power)) devicePower = (bool)power;
    if (values.TryGetValue("4", out var mode)) { deviceMode = (string)mode; deviceFan = "Low"; }
    if (values.Count == 1 && values.TryGetValue("5", out var fan)) deviceFan = (string)fan;
    return Task.CompletedTask;
}
var device = new AirConditioner(deviceConfig, Query, Send);
using var vento = new VentoClient(new Config());
var cycle = ShortcutActions.Create(vento, new[] { device }).Single(a => a.Id == "ac.test.fanCycle");
Check(await cycle.Run() && devicePower && deviceMode == "Fan" && deviceFan == "Low",
    "Shortcut starts real command sequence in fan mode at low speed");
Check(writes.Count == 3 && writes[0].ContainsKey("1") && writes[1].ContainsKey("4") && writes[2].ContainsKey("5") &&
    writes.All(w => w.Count == 1), "Send power, mode and speed separately in order");
writes.Clear();
Check(await cycle.Run() && deviceFan == "High", "Second shortcut press changes device speed to high");
Check(writes.Count == 1 && writes[0].ContainsKey("5"), "Active fan changes only speed without resetting mode or power");
writes.Clear();
Check(await cycle.Run() && deviceFan == "Low", "Third shortcut press changes device speed back to low");
Check(writes.Count == 1 && writes[0].ContainsKey("5"), "High to low also writes only speed");
// A change made from the remote must override the previously requested cycle state.
deviceMode = "Cool"; deviceFan = "High";
writes.Clear();
Check(await cycle.Run() && deviceMode == "Fan" && deviceFan == "Low", "Read external mode change before cycling");
Check(writes.Count == 2 && writes[0].ContainsKey("4") && writes[1].ContainsKey("5"),
    "Already powered device switches mode before setting speed");

Console.WriteLine("All shortcut checks passed.");
