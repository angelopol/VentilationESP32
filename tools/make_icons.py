"""Genera los iconos PNG de la PWA y los incrusta en ../Vent/icons.h.

Uso:  python tools/make_icons.py   (requiere Pillow)
"""
import io
import math
import os

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SS = 4  # supersampling para bordes suaves

TOP = (14, 165, 233)     # azul cielo
BOTTOM = (13, 80, 160)   # azul profundo


def draw_icon(size):
    s = size * SS
    img = Image.new("RGB", (s, s))
    px = img.load()
    for y in range(s):
        t = y / (s - 1)
        c = tuple(round(TOP[k] + (BOTTOM[k] - TOP[k]) * t) for k in range(3))
        for x in range(s):
            px[x, y] = c

    fan = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(fan)
    cx = cy = s / 2
    blade_len = s * 0.29
    blade_w = s * 0.15
    for n in range(3):
        base = math.radians(n * 120 - 90)
        # Aspa curvada: poligono entre dos arcos desplazados
        pts = []
        steps = 40
        for i in range(steps + 1):
            f = i / steps
            r = s * 0.06 + blade_len * f
            a = base + 0.9 * f
            w = blade_w * math.sin(math.pi * f) ** 0.6
            pts.append((cx + r * math.cos(a) + w * 0.5 * math.cos(a + math.pi / 2),
                        cy + r * math.sin(a) + w * 0.5 * math.sin(a + math.pi / 2)))
        for i in range(steps, -1, -1):
            f = i / steps
            r = s * 0.06 + blade_len * f
            a = base + 0.9 * f
            w = blade_w * math.sin(math.pi * f) ** 0.6
            pts.append((cx + r * math.cos(a) - w * 0.5 * math.cos(a + math.pi / 2),
                        cy + r * math.sin(a) - w * 0.5 * math.sin(a + math.pi / 2)))
        d.polygon(pts, fill=255)
    hub = s * 0.075
    d.ellipse((cx - hub, cy - hub, cx + hub, cy + hub), fill=255)
    # Anillo exterior (rejilla)
    ring_r, ring_w = s * 0.40, s * 0.03
    d.ellipse((cx - ring_r, cy - ring_r, cx + ring_r, cy + ring_r), outline=255, width=round(ring_w))

    shadow = fan.filter(ImageFilter.GaussianBlur(s * 0.015))
    img.paste((8, 40, 90), (0, round(s * 0.01)), shadow.point(lambda v: v * 0.35))
    img.paste((255, 255, 255), (0, 0), fan)
    return img.resize((size, size), Image.LANCZOS)


def png_bytes(img):
    buf = io.BytesIO()
    img.quantize(colors=64, method=Image.MEDIANCUT).save(buf, "PNG", optimize=True)
    return buf.getvalue()


def c_array(name, data):
    lines = [f"const uint8_t {name}[] PROGMEM = {{"]
    for i in range(0, len(data), 20):
        lines.append("  " + ",".join(f"0x{b:02x}" for b in data[i:i + 20]) + ",")
    lines.append("};")
    lines.append(f"const size_t {name}_len = sizeof({name});")
    return "\n".join(lines)


def main():
    out = ["// Generado por tools/make_icons.py - no editar a mano.", "#pragma once", "#include <Arduino.h>", ""]
    for size, name in ((180, "ICON_180"), (192, "ICON_192"), (512, "ICON_512")):
        img = draw_icon(size)
        img.save(os.path.join(HERE, f"icon-{size}.png"))
        data = png_bytes(img)
        print(f"{name}: {len(data)} bytes")
        out.append(c_array(name, data))
        out.append("")
    with open(os.path.join(ROOT, "Vent", "icons.h"), "w", newline="\n") as f:
        f.write("\n".join(out))

    # Icono de la app de Windows (exe y bandeja)
    ico_sizes = [(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (128, 128), (256, 256)]
    draw_icon(256).save(os.path.join(ROOT, "windows", "vento.ico"), sizes=ico_sizes)
    print("windows/vento.ico")


if __name__ == "__main__":
    main()
