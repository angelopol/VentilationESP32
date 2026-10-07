using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace Vento
{
    public sealed record ShortcutAction(string Id, string Label, Func<Task<bool>> Run);

    public static class ShortcutActions
    {
        public static List<ShortcutAction> Create(VentoClient client, IEnumerable<AirConditioner> acs)
        {
            var actions = new List<ShortcutAction>();
            var fanLock = new SemaphoreSlim(1, 1);
            void Fan(string id, string label, Func<Task<bool>> run) => actions.Add(new(id, "Ventilador · " + label, async () =>
            {
                await fanLock.WaitAsync();
                try { return await run(); }
                finally { fanLock.Release(); }
            }));
            Fan("fan.on", "Encender (nivel 1)", () => client.SetModeAsync(1));
            Fan("fan.off", "Apagar", () => client.SetModeAsync(0));
            Fan("fan.toggle", "Alternar encendido (nivel 5) / apagado", async () =>
            {
                if (!await client.RefreshStateAsync() || client.State == null) return false;
                return await client.SetModeAsync(client.State.Mode == 0 ? 5 : 0);
            });
            async Task<bool> Step(int direction)
            {
                if (!await client.RefreshStateAsync() || client.State == null) return false;
                int mode = client.State.Mode;
                // Auto/progresivo -> velocidad manual más cercana al PWM actual.
                if (mode > 5) mode = client.State.Pwm == 0 ? 0 : Math.Clamp(1 + (int)Math.Round((client.State.Pwm / 2.55 - 70) / 7.5), 1, 5);
                return await client.SetModeAsync(Math.Clamp(mode + direction, 0, 5));
            }
            Fan("fan.up", "Subir velocidad", () => Step(1));
            Fan("fan.down", "Bajar velocidad (nivel 0 apaga)", () => Step(-1));
            for (int i = 1; i <= 7; i++)
            {
                int mode = i;
                Fan("fan.mode." + mode, VentoState.ModeName(mode), () => client.SetModeAsync(mode));
            }
            foreach (var ac in acs)
            {
                var gate = new SemaphoreSlim(1, 1);
                void Add(string id, string label, Func<Task<bool>> run) => actions.Add(new("ac." + ac.Config.Id + "." + id,
                    ac.Name + " · " + label, async () =>
                    {
                        await gate.WaitAsync();
                        try { return await run(); }
                        finally { gate.Release(); }
                    }));
                if (ac.Config.Dps.Power > 0)
                {
                    Add("on", "Encender", () => ac.SetPowerAsync(true));
                    Add("off", "Apagar", () => ac.SetPowerAsync(false));
                    Add("toggle", "Alternar encendido / apagado", async () =>
                    {
                        await ac.RefreshAsync(wait: true);
                        return ac.Online && await ac.SetPowerAsync(!ac.Power);
                    });
                }
                if (ac.Config.Dps.Power > 0 && ac.Config.Dps.Mode > 0 && ac.Config.Dps.Fan > 0)
                    Add("fanCycle", "Encender ventilador en baja / alternar alta y baja", async () =>
                    {
                        await ac.RefreshAsync(wait: true);
                        return ac.Online && await ac.CycleFanAsync();
                    });
                async Task<bool> Temperature(int direction)
                {
                    await ac.RefreshAsync(wait: true);
                    if (!ac.Online || !ac.Setpoint.HasValue) return false;
                    return await ac.SetSetpointAsync(ac.Setpoint.Value + direction * ac.Config.Step);
                }
                if (ac.Config.Dps.Setpoint > 0 && ac.Config.Dps.Mode > 0)
                {
                    Add("temp.up", "Subir temperatura y activar frío", () => Temperature(1));
                    Add("temp.down", "Bajar temperatura y activar frío", () => Temperature(-1));
                }
                if (ac.Config.Dps.Mode > 0)
                {
                    Add("fanMode", "Modo ventilador", () => ac.Config.ResolveMode(false) is string mode
                        ? ac.SetModeAsync(mode) : Task.FromResult(false));
                    foreach (var kv in ac.Config.Modes)
                        Add("mode." + kv.Key, "Modo " + kv.Value, () => ac.SetModeAsync(kv.Key));
                }
                if (ac.Config.Dps.Fan > 0)
                    foreach (var kv in ac.Config.Fans)
                        Add("speed." + kv.Key, "Velocidad " + kv.Value, () => ac.SetFanAsync(kv.Key));
                foreach (var toggle in ac.Toggles())
                    Add("toggle." + toggle.Dp, "Alternar " + toggle.Name, async () =>
                    {
                        await ac.RefreshAsync(wait: true);
                        return ac.Online && await ac.SetToggleAsync(toggle.Dp, !ac.IsToggleOn(toggle.Dp));
                    });
            }
            return actions;
        }
    }
}
