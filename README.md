# Vento – Ventilation Control System with ESP32

An ESP32 connects to your WiFi network and serves a local web app (installable as a PWA on iPhone) to control a fan and LEDs based on the temperature and humidity measured by a DHT11 sensor. It can also control **Tuya air conditioners** on the same network (see [Air conditioners](#air-conditioners-tuya)).

![image](https://github.com/user-attachments/assets/a596511f-6e45-47b3-971b-50e81033e8c6)

## Setup

1. Install the **ESP32** board package, the **DHT sensor library** (Adafruit) and **ArduinoJson** (v7, by Benoit Blanchon) in the Arduino IDE.
2. Open `Vent/Vent.ino` in the Arduino IDE. Copy `Vent/secrets.example.h` to `Vent/secrets.h` and fill in your WiFi SSID and password (`secrets.h` is git-ignored). If they are wrong, you can set the network from the phone instead (see below).
3. Check `FAN_PIN` in `Vent/Vent.ino` matches the pin wired to the fan driver.
4. Flash the board and open the serial monitor (115200) to see the assigned IP.

## Using the app

- Open `http://vento.local` (or the IP shown in the serial monitor) from any device on the same network.
- **iPhone:** open it in Safari → Share → *Add to Home Screen*. It launches full-screen with the Vento icon.
- No internet is needed, only a shared local network.

## WiFi setup mode (own network)

If the ESP32 can't join a known network within 15 s at boot, or loses the router for more than 30 s, it creates its own WiFi network **"Vento"** (password `vento1234`, configurable in `secrets.h`):

1. Join "Vento" from the phone. The setup page opens automatically (captive portal); otherwise go to `http://192.168.4.1/wifi`.
2. Pick your network, type the password and tap **Conectar**. Only 2.4 GHz networks are supported.
3. If it connects, the network is saved in flash and the "Vento" network turns off after 30 s. A wrong password is never saved.

The fan can also be controlled at `http://192.168.4.1` while connected to "Vento", with no router at all. The setup page is always available from the app under *Configurar WiFi*.

Known networks are tried in order: the one saved from the portal, then the one in `secrets.h`. While "Vento" is active and nobody is connected to it, the ESP32 retries the router every minute.

## LEDs

| LED   | Meaning |
|-------|---------|
| Red   | **Solid**: connected to WiFi. **Slow blink**: connecting to the router. **Fast blink**: own "Vento" network active (setup mode). |
| Green | Fan running (speeds 1–5 and Progressive mode). |
| Blue  | Temperature-controlled mode (Auto / Progressive). |

The fan keeps working in its current mode while WiFi is down.

## Modes

| Mode | Behavior |
|------|----------|
| 0 – Off | Fan stopped. |
| 1–5 | Fixed speeds from 70 % to 100 % (`FAN_MIN_PWM`), with a 100 % kick when starting from a stop. |
| 6 – Auto | Full speed when the heat index ≥ target temperature, off otherwise. |
| 7 – Progressive | PWM scales with how close the heat index is to the target; full speed above it. |

The target temperature ranges from 16 °C to 70 °C in steps of 1 °C.

## Air conditioners (Tuya)

Vento controls Tuya / Smart Life air conditioners directly over the local network (Tuya protocol 3.3, 3.4 and 3.5, the same one `tinytuya` uses). No cloud, no extra server: the ESP32 talks to them and the web app shows one card per air conditioner with room temperature, power, target temperature (− / +), mode and fan speed. The Windows app controls them too.

1. Get each device's `id` and `local_key` with [tinytuya](https://github.com/jasonacox/tinytuya): `python -m tinytuya wizard` (needs a free iot.tuya.com project linked to your Smart Life account; its Access ID / Secret are only needed for this step). The protocol `version` can be left as `"auto"`: the first time, Vento tries 3.3, 3.4 and 3.5 and keeps the one that answers (`/api/ac/raw` shows which one; writing it in the config skips the detection).
2. Give the air conditioner a **fixed IP** in the router.
3. Add it to `AC_DEVICES_JSON` in `Vent/secrets.h` (see `secrets.example.h`); several devices go in the same list. Only `name`, `id`, `key` and `ip` are required.
4. Flash, open `http://vento.local/api/ac/raw?d=0` (`d` is the device index) and change things from the remote or the Smart Life app to see which **DP** is which. The defaults are the usual ones for Tuya air conditioners (category `kt`): `1` power, `2` target temperature, `3` room temperature, `4` mode, `5` fan speed. Adjust `dps`, `modes`, `fans` and `scale` (`10` if the device sends `240` for 24.0 °C) if yours differ; `0` hides a DP the model doesn't have.

```json
{ "name": "Aire", "id": "bf0123…", "key": "0123456789abcdef", "ip": "192.168.1.50", "version": "auto",
  "dps": { "power": 1, "setpoint": 2, "temp": 3, "mode": 4, "fan": 5 },
  "scale": 1, "min": 16, "max": 31, "step": 1,
  "modes": { "cold": "Frío", "wet": "Seco", "wind": "Ventilador", "hot": "Calor", "auto": "Auto" },
  "fans": { "low": "Baja", "mid": "Media", "high": "Alta", "auto": "Auto" },
  "toggles": { "15": "Oscilación", "101": "Sueño" } }
```

`toggles` adds on/off buttons for any other boolean DP (swing, sleep, child lock…). Some manufacturers use their own values instead of the `kt` defaults (for example `Cool` / `Dyr` / `Fan` and `Low` / `High`) and extra DPs that the standard spec doesn't list; the full model with every DP comes from the Tuya API at `/v2.0/cloud/thing/<id>/model` (e.g. `tinytuya.Cloud(...).cloudrequest(...)`).

Air conditioners take a few seconds to apply a command. Until the device reports the new value (at most 15 s), Vento keeps showing the requested one, so the UI doesn't bounce back and forth; if the device never applies it, the real state comes back after those 15 s.

The ESP32 only polls the air conditioners (every 5 s) while someone has the app open, and each exchange opens and closes its own connection, because many Tuya devices accept only one local connection at a time. Commands are sent from a background task, so a slow or unplugged device never blocks the fan or the web server. The Smart Life app and the remote keep working as usual.

## HTTP API

| Method | Path | Description |
|--------|------|-------------|
| GET  | `/api/state` | `{mode, setpoint, pwm, temp, hum, hic, rssi, up, heap, minHeap, reset}`; `?ac=1` adds `acs` (same as `/api/ac`), which the web app uses so it only makes one request at a time |
| POST | `/api/mode?v=0..7` | Change mode |
| POST | `/api/setpoint?v=16..70` | Change target temperature |
| GET  | `/api/wifi/status` | WiFi / setup-mode status |
| GET  | `/api/wifi/scan` | Nearby networks (asynchronous, poll until `scanning` is false) |
| POST | `/api/wifi` (`ssid`, `pass`) | Try a network; saved only if it connects |
| POST | `/api/wifi/forget` | Forget the saved network |
| GET  | `/api/ac` | Air conditioners: `{devices: [{name, online, error, power, setpoint, temp, mode, fan, min, max, step, modes, fans, toggles: [{dp, name, on}]}]}` |
| POST | `/api/ac/power?d=<i>&v=0\|1` | Turn air conditioner `i` off / on |
| POST | `/api/ac/temp?d=<i>&v=<°C>` | Target temperature (clamped to `min`..`max`) |
| POST | `/api/ac/mode?d=<i>&v=<mode>` | One of the configured `modes` keys |
| POST | `/api/ac/fan?d=<i>&v=<speed>` | One of the configured `fans` keys |
| POST | `/api/ac/toggle?d=<i>&v=<dp>:<0\|1>` | Turn one of the configured `toggles` off / on |
| GET  | `/api/ac/raw?d=<i>` | Raw DPS as reported by the device, to find out what each one is |

## WiFi only

The serial monitor (115200) only shows logs: the assigned IP, the reason for the last restart and errors talking to the air conditioners. Everything is controlled over WiFi (web app, HTTP API and Windows app).

Vento is WiFi only: Bluetooth was removed because it shares the ESP32's radio with WiFi and forces WiFi to sleep between beacons, which made the web app and `vento.local` (mDNS) unreliable. Without it the firmware is also much smaller and fits the default partition scheme.

## Windows app

`windows/` contains a tray app (.NET 9, WPF):

- **Right-click** the tray icon: Off and speeds 1–5 (the current one is checked), web panel, *Start with Windows*.
- **Left-click**: a small window with the heat index, **Auto** / **Progresivo** and the target temperature.
- The icon turns grey when Vento isn't reachable.
- **Air conditioners:** each one has its own submenu (power, target temperature, mode, fan) and its own block in the left-click window. Add them to `AirConditioners` in `%APPDATA%\Vento\config.json` with the same JSON as in `AC_DEVICES_JSON` (the Windows app talks to them directly, so it works even if the ESP32 is off). They're only polled while the window is open or the menu is shown. A `config.json` with a syntax error is never overwritten: the app says so and uses the defaults until it's fixed.

It connects over WiFi: first to Vento's last known IP (reported in `/api/state`) and, if that doesn't answer, to `http://vento.local`, retrying every 10 s while Vento is unreachable. When nothing is clicked it stays silent: if Vento is off when the PC starts, no errors or windows appear.

**Sistema de ventilación** (tray submenu): the fan and the air conditioners follow the PC.

- *Encender al iniciar Windows*: when the app starts (normally with Windows) it sets Vento to **speed 5** and turns **on** every air conditioner, each as soon as it answers, if that happens within 5 minutes; if a device is off or unreachable, nothing happens (`StartupMode` in the config: fan mode `0`–`7`, or `-1` to disable the whole startup).

- *Apagar al apagar el PC*: when Windows shuts down, restarts or logs off, it turns Vento **off** and the air conditioners **off**, all at once and waiting at most 4 s so shutdown isn't held up (`ShutdownMode` in the config: fan mode `0`–`7`, or `-1` to disable).

It starts with Windows automatically after the first run (registry `Run` key, no admin needed). Settings are in `%APPDATA%\Vento\config.json`; `Host` must match `DEVICE_HOSTNAME`.

**Download / release:** pushing a `v*` tag (e.g. `git tag v1.0.0 && git push --tags`) runs `.github/workflows/release-windows.yml`, which publishes a single self-contained `Vento.exe` to GitHub Releases. The workflow can also be run manually from the Actions tab.

**Local build:** `dotnet publish windows/Vento.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -o publish`

## Files

The Arduino sketch lives in `Vent/` (the IDE requires the folder to share the `.ino` name):

- `Vent/Vent.ino` – firmware (WiFi, web server, fan/LED control).
- `Vent/web_ui.h` – the web app HTML, CSS and PWA manifest.
- `Vent/icons.h` – embedded PNG icons, generated by `tools/make_icons.py`.
- `Vent/secrets.example.h` – template for WiFi credentials, the setup network and the air conditioners.
- `Vent/tuya.h` / `Vent/tuya.cpp` – Tuya local protocol client (3.3 / 3.4 / 3.5).
- `Vent/aircon.h` / `Vent/aircon.cpp` – air conditioner list, background polling and `/api/ac` state.
- `windows/` – Windows tray app; `windows/vento.ico` is also generated by `tools/make_icons.py`. `Tuya.cs`, `AirConditioner.cs` and `AcSection.cs` are the air conditioner client, state and panel block.
