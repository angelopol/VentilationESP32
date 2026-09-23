# Vento – Ventilation Control System with ESP32

An ESP32 connects to your WiFi network and serves a local web app (installable as a PWA on iPhone) to control a fan and LEDs based on the temperature and humidity measured by a DHT11 sensor.

![image](https://github.com/user-attachments/assets/a596511f-6e45-47b3-971b-50e81033e8c6)

## Setup

1. Install the **ESP32** board package and the **DHT sensor library** (Adafruit) in the Arduino IDE.
2. Open `Vent/Vent.ino` in the Arduino IDE. Copy `Vent/secrets.example.h` to `Vent/secrets.h` and fill in your WiFi SSID and password (`secrets.h` is git-ignored). If they are wrong, you can set the network from the phone instead (see below).
3. Check `FAN_PIN` in `Vent/Vent.ino` matches the pin wired to the fan driver.
4. In **Tools → Partition Scheme** select **"Huge APP (3MB No OTA/1MB SPIFFS)"**. WiFi and Bluetooth together don't fit in the default 1.2 MB app partition.
5. Flash the board and open the serial monitor (115200) to see the assigned IP.

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

The target temperature ranges from 16 °C to 70 °C in steps of 3.

## HTTP API

| Method | Path | Description |
|--------|------|-------------|
| GET  | `/api/state` | `{mode, setpoint, pwm, temp, hum, hic, rssi}` |
| POST | `/api/mode?v=0..7` | Change mode |
| POST | `/api/setpoint?v=16..70` | Change target temperature |
| GET  | `/api/wifi/status` | WiFi / setup-mode status |
| GET  | `/api/wifi/scan` | Nearby networks (asynchronous, poll until `scanning` is false) |
| POST | `/api/wifi` (`ssid`, `pass`) | Try a network; saved only if it connects |
| POST | `/api/wifi/forget` | Forget the saved network |

## Bluetooth

Vento is also a Bluetooth Classic (SPP) device named after `DEVICE_HOSTNAME` (`vento` by default). Pair it and send the original single-character commands:

- `0`–`7`: change mode.
- `a`–`s`: target temperature 16–70 °C, in steps of 3 (`a` = 16, `b` = 19, … `s` = 70).
- `?`: replies with the same JSON as `/api/state`, on one line.

In modes 6 and 7 it sends back the heat index as a text line every 500 ms, like the original Bluetooth version. The same commands also work through the serial monitor.

## Windows app

`windows/` contains a tray app (.NET 9, WPF):

- **Right-click** the tray icon: Off and speeds 1–5 (the current one is checked), web panel, Bluetooth pairing, *Start with Windows*.
- **Left-click**: a small window with the heat index, **Auto** / **Progresivo** and the target temperature.
- The icon turns grey when Vento isn't reachable.

It connects to `http://vento.local` and falls back to Bluetooth when WiFi doesn't answer. Bluetooth is used on demand: each action connects, sends the command, reads the new state and disconnects, so Bluetooth stays free for the phone. It keeps checking WiFi and switches back as soon as it answers. If Vento isn't reachable over WiFi and isn't paired, it opens Windows' Bluetooth settings so you can pair `vento`.

It starts with Windows automatically after the first run (registry `Run` key, no admin needed). Settings are in `%APPDATA%\Vento\config.json`; `Host` must match `DEVICE_HOSTNAME`.

**Download / release:** pushing a `v*` tag (e.g. `git tag v1.0.0 && git push --tags`) runs `.github/workflows/release-windows.yml`, which publishes a single self-contained `Vento.exe` to GitHub Releases. The workflow can also be run manually from the Actions tab.

**Local build:** `dotnet publish windows/Vento.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -o publish`

## Files

The Arduino sketch lives in `Vent/` (the IDE requires the folder to share the `.ino` name):

- `Vent/Vent.ino` – firmware (WiFi, web server, fan/LED control).
- `Vent/web_ui.h` – the web app HTML, CSS and PWA manifest.
- `Vent/icons.h` – embedded PNG icons, generated by `tools/make_icons.py`.
- `Vent/secrets.example.h` – template for WiFi credentials and the setup network.
- `windows/` – Windows tray app; `windows/vento.ico` is also generated by `tools/make_icons.py`.
