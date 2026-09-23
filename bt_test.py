"""Consola para probar el ventilador ESP32 por Bluetooth (SPP).

Conexion (empareja antes "ESP32test" en la configuracion de Bluetooth del PC):
  python bt_test.py                                 # interactivo: elige entre los dispositivos emparejados
  python bt_test.py --address 24:6F:28:AA:BB:CC     # RFCOMM directo, sin dependencias
  python bt_test.py --port COM5                     # puerto COM del emparejamiento (pip install pyserial)
  python bt_test.py --list                          # lista los puertos COM

Prueba automatica de todos los modos:
  python bt_test.py --port COM5 --auto
"""
import argparse
import socket
import sys
import threading
import time

MODES = {
    "0": "Reposo (ventilador apagado, LED rojo)",
    "1": "Velocidad 1 (PWM 51)",
    "2": "Velocidad 2 (PWM 102)",
    "3": "Velocidad 3 (PWM 153)",
    "4": "Velocidad 4 (PWM 204)",
    "5": "Velocidad 5 (PWM 255)",
    "6": "Auto: maximo si la sensacion termica >= objetivo",
    "7": "Progresivo segun la temperatura objetivo",
}
# 'a'..'s' -> 16..70 grados en pasos de 3
SETPOINTS = {chr(ord("a") + k): 16 + 3 * k for k in range(19)}

HELP = """Comandos:
  0-7          cambia de modo ({modes})
  t <grados>   temperatura objetivo 16-70 (se redondea al paso de 3); solo la aplica en modos 6 y 7
  raw <texto>  envia los caracteres tal cual
  auto         recorre todos los modos
  help         muestra esta ayuda
  q            salir
En los modos 6 y 7 el ESP32 envia la sensacion termica cada 100 ms; se muestra como "<< valor".
""".format(modes="0=reposo, 1-5=velocidades, 6=auto, 7=progresivo")


class RfcommLink:
    def __init__(self, address, channel):
        self.sock = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
        self.sock.connect((address, channel))
        self.sock.settimeout(0.5)

    def write(self, data):
        self.sock.sendall(data)

    def read(self):
        try:
            return self.sock.recv(256)
        except socket.timeout:
            return b""

    def close(self):
        self.sock.close()


class SerialLink:
    def __init__(self, port):
        import serial
        self.ser = serial.Serial(port, 115200, timeout=0.5)

    def write(self, data):
        self.ser.write(data)

    def read(self):
        return self.ser.read(256)

    def close(self):
        self.ser.close()


class Reader(threading.Thread):
    """Muestra lo que envia el ESP32 sin inundar la consola (una linea cada `interval` s)."""

    def __init__(self, link, interval):
        super().__init__(daemon=True)
        self.link, self.interval = link, interval
        self.running = True
        self.last_value = None
        self.last_print = 0.0
        self.count = 0

    def run(self):
        buf = b""
        while self.running:
            try:
                chunk = self.link.read()
            except OSError as e:
                if self.running:
                    print(f"\n!! Conexion perdida: {e}")
                    self.running = False
                return
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode(errors="replace").strip()
                if not text:
                    continue
                self.last_value = text
                self.count += 1
                now = time.time()
                if now - self.last_print >= self.interval:
                    self.last_print = now
                    print(f"<< {text}")


def setpoint_letter(degrees):
    degrees = max(16, min(70, degrees))
    letter = min(SETPOINTS, key=lambda k: abs(SETPOINTS[k] - degrees))
    return letter, SETPOINTS[letter]


def send(link, text, note=""):
    link.write(text.encode())
    print(f">> {text!r}" + (f"  ({note})" if note else ""))


def auto_test(link, reader, step):
    print(f"-- Prueba automatica ({step:.0f} s por modo) --")
    for mode in "12345":
        send(link, mode, MODES[mode])
        time.sleep(step)
    for mode in "67":
        send(link, mode, MODES[mode])
        time.sleep(1)
        before = reader.count
        letter, deg = setpoint_letter(22)
        send(link, letter, f"objetivo {deg} C")
        time.sleep(step)
        got = reader.count - before
        status = f"OK, {got} lecturas, ultima {reader.last_value}" if got else "SIN DATOS: revisa el DHT11"
        print(f"   modo {mode}: {status}")
    send(link, "0", MODES["0"])
    print("-- Fin de la prueba --")


def list_ports():
    try:
        from serial.tools import list_ports
    except ImportError:
        sys.exit("Instala pyserial para listar puertos: pip install pyserial")
    ports = list(list_ports.comports())
    if not ports:
        print("No hay puertos COM.")
    for p in ports:
        print(f"{p.device:8} {p.description}  [{p.hwid}]")
    print("\nEl ESP32 emparejado aparece como 'Vinculo serie estandar a traves de Bluetooth'; "
          "suele funcionar el puerto 'saliente', el que tiene la direccion del ESP32 en el hwid.")


def paired_devices():
    """Dispositivos Bluetooth emparejados en Windows: [(nombre, direccion)]."""
    try:
        import winreg
        root = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                              r"SYSTEM\CurrentControlSet\Services\BTHPORT\Parameters\Devices")
    except (ImportError, OSError):
        return []
    devices = []
    i = 0
    while True:
        try:
            key = winreg.EnumKey(root, i)
        except OSError:
            break
        i += 1
        try:
            with winreg.OpenKey(root, key) as sub:
                raw, _ = winreg.QueryValueEx(sub, "Name")
            name = bytes(raw).rstrip(b"\0").decode(errors="replace")
        except OSError:
            name = "(sin nombre)"
        address = ":".join(key[j:j + 2] for j in range(0, 12, 2)).upper()
        devices.append((name, address))
    # El ESP32 primero
    devices.sort(key=lambda d: (not d[0].upper().startswith("ESP32"), d[0].lower()))
    return devices


def choose_target():
    """Menu para elegir a que conectarse. Devuelve ("address", addr) o ("port", com)."""
    options = [("address", addr, f"{name}  [{addr}]") for name, addr in paired_devices()]
    try:
        from serial.tools import list_ports
        options += [("port", p.device, f"{p.device}  {p.description}") for p in list_ports.comports()]
    except ImportError:
        pass

    if options:
        print("Dispositivos disponibles:")
        for n, (_, _, label) in enumerate(options, 1):
            print(f"  {n}) {label}")
    else:
        print("No se encontraron dispositivos emparejados. Empareja 'ESP32test' en la configuracion de Bluetooth.")
    print("  m) escribir una direccion Bluetooth o un puerto COM a mano")
    print("  q) salir")

    default = "1" if options and options[0][0] == "address" and options[0][2].upper().startswith("ESP32") else ""
    while True:
        try:
            ans = input(f"Elige [{default}]: " if default else "Elige: ").strip().lower() or default
        except EOFError:
            sys.exit()
        if ans == "q":
            sys.exit()
        if ans == "m":
            value = input("Direccion (XX:XX:XX:XX:XX:XX) o puerto (COM5): ").strip()
            if value.upper().startswith("COM"):
                return "port", value.upper()
            if value:
                return "address", value
            continue
        if ans.isdigit() and 1 <= int(ans) <= len(options):
            kind, value, _ = options[int(ans) - 1]
            return kind, value
        print("Opcion no valida.")


def main():
    ap = argparse.ArgumentParser(description="Prueba el ventilador ESP32 por Bluetooth")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--address", help="direccion Bluetooth del ESP32 (XX:XX:XX:XX:XX:XX)")
    g.add_argument("--port", help="puerto COM del ESP32 emparejado (requiere pyserial)")
    g.add_argument("--list", action="store_true", help="lista los puertos COM")
    ap.add_argument("--channel", type=int, default=1, help="canal RFCOMM (por defecto 1)")
    ap.add_argument("--auto", action="store_true", help="ejecuta la prueba automatica y sale")
    ap.add_argument("--step", type=float, default=3, help="segundos por modo en la prueba automatica")
    ap.add_argument("--interval", type=float, default=1, help="segundos entre lecturas mostradas")
    args = ap.parse_args()

    if args.list:
        list_ports()
        return

    if args.address:
        kind, target = "address", args.address
    elif args.port:
        kind, target = "port", args.port
    else:
        kind, target = choose_target()

    print(f"Conectando a {target}...")
    try:
        if kind == "address":
            link = RfcommLink(target, args.channel)
        else:
            link = SerialLink(target)
    except ImportError:
        sys.exit("Falta pyserial: pip install pyserial  (o usa --address)")
    except OSError as e:
        sys.exit(f"No se pudo conectar: {e}\n"
                 "Comprueba que el ESP32 esta encendido, emparejado y que no hay otra app conectada.")
    print("Conectado.\n")

    reader = Reader(link, args.interval)
    reader.start()
    try:
        if args.auto:
            auto_test(link, reader, args.step)
            return
        print(HELP)
        while reader.running:
            try:
                cmd = input("> ").strip()
            except EOFError:
                break
            if not cmd:
                continue
            word, _, rest = cmd.partition(" ")
            word = word.lower()
            if word in ("q", "quit", "exit", "salir"):
                break
            elif word in ("h", "help", "?", "ayuda"):
                print(HELP)
            elif cmd in MODES:
                send(link, cmd, MODES[cmd])
            elif word == "t":
                try:
                    letter, deg = setpoint_letter(float(rest))
                except ValueError:
                    print("Uso: t <grados>, por ejemplo t 28")
                    continue
                send(link, letter, f"objetivo {deg} C")
            elif word == "raw":
                if rest:
                    send(link, rest)
            elif word == "auto":
                auto_test(link, reader, args.step)
            else:
                print("Comando no reconocido. Escribe help.")
    except KeyboardInterrupt:
        print()
    finally:
        reader.running = False
        link.close()
        print("Desconectado.")


if __name__ == "__main__":
    main()
