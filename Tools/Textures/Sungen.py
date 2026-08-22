import sys
sys.dont_write_bytecode = True

import os
import math
import argparse

try:
    from PIL import Image
except ImportError:
    print("\033[91m[-] Error: Pillow not found: pip install Pillow\033[0m")
    sys.exit(1)

if os.name == 'nt':
    import ctypes
    kernel32 = ctypes.windll.kernel32
    kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)


def _tag(msg):
    return f"\033[36m[Pip3D]\033[0m {msg}"


DEFAULT_SIZE     = 32
DEFAULT_DISK_R   = 4.5
DEFAULT_HALO_R   = 15.5
CHROMA_KEY       = 0x0000
PREVIEW_NAME     = "_Sun.png"


def _rgb565_gray(v):
    if v <= 0:
        return 0
    if v >= 255:
        return 0xFFFF
    v5 = (max(0, min(255, int(v))) >> 3) & 0x1F
    if v5 == 0:
        return 0
    g6 = (v5 << 1) | 1
    return (v5 << 11) | (g6 << 5) | v5


def _rgb565(r, g, b):
    r = max(0, min(255, int(r)))
    g = max(0, min(255, int(g)))
    b = max(0, min(255, int(b)))
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def _smoothstep(e0, e1, x):
    if e1 == e0:
        return 0.0 if x < e0 else 1.0
    t = (x - e0) / (e1 - e0)
    if t < 0.0:
        t = 0.0
    elif t > 1.0:
        t = 1.0
    return t * t * (3.0 - 2.0 * t)


def gen_sun_pixels(size, disk_r, halo_r):
    cx = cy = (size - 1) / 2.0
    px565 = []
    preview = []
    for y in range(size):
        for x in range(size):
            dx = x - cx
            dy = y - cy
            d = math.sqrt(dx * dx + dy * dy)

            if d > halo_r:
                px565.append(CHROMA_KEY)
                preview.append((0, 0, 0))
                continue

            if d <= disk_r:
                v = 255
            else:
                t = (d - disk_r) / (halo_r - disk_r)
                a = 1.0 - _smoothstep(0.0, 1.0, t)
                v = int(255.0 * a)

            v = max(0, min(255, v))
            px = _rgb565_gray(v)
            px565.append(px)
            v5 = (v >> 3) & 0x1F
            preview.append((v5 * 8 + v5 // 4, v5 * 8 + v5 // 4, v5 * 8 + v5 // 4))

    return px565, preview


def write_header(pixels, size, disk_r, halo_r, out_path):
    shift = int(math.log2(size))
    var = "sun"
    total_bytes = size * size * 2

    lines = []
    lines.append("/*")
    lines.append(" * Pip3D Sun Texture Asset")
    lines.append(" * Generated automatically by Tools/Textures/Sungen.py. Do not edit.")
    lines.append(" *")
    lines.append(f" * Dimensions    : {size}x{size} (Grayscale RGB565, square)")
    lines.append(f" * Parameters    : Disk Radius={disk_r:.1f}px, Halo Radius={halo_r:.1f}px")
    lines.append(f" * Flash Memory  : {total_bytes} bytes ({total_bytes / 1024.0:.2f} KB)")
    lines.append(" */")
    lines.append("")
    lines.append("#pragma once")
    lines.append("")
    lines.append('#include "Rendering/Resources/Texture.hpp"')
    lines.append("")
    lines.append("namespace pip3D")
    lines.append("{")
    lines.append("    namespace detail")
    lines.append("    {")
    lines.append("        alignas(16) static const uint16_t s_%sTextureData[%d] = {" % (var, size * size))

    items_per_line = 12
    for i in range(0, len(pixels), items_per_line):
        chunk = pixels[i:i + items_per_line]
        hex_str = ", ".join(f"0x{px:04X}" for px in chunk)
        lines.append(f"            {hex_str},")

    lines.append("        };")
    lines.append("    }")
    lines.append("")
    lines.append("    inline Texture g_%sTexture = {" % var)
    lines.append("        .data     = detail::s_%sTextureData," % var)
    lines.append("        .mipData  = nullptr,")
    lines.append("        .shiftU   = %d," % shift)
    lines.append("        .mipCount = 0")
    lines.append("    };")
    lines.append("}")
    lines.append("")

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def _to_array(preview, size):
    arr = bytearray()
    for (r, g, b) in preview:
        arr += bytes((r, g, b))
    import numpy as np
    return np.frombuffer(bytes(arr), dtype=np.uint8).reshape((size, size, 3))


def main():
    p = argparse.ArgumentParser(description="pip3D sun texture generator")
    p.add_argument("output", nargs="?")
    p.add_argument("--size", type=int, default=DEFAULT_SIZE)
    args = p.parse_args()

    size = args.size
    scale = size / 32.0
    disk_r = DEFAULT_DISK_R * scale
    halo_r = DEFAULT_HALO_R * scale

    pixels, preview = gen_sun_pixels(size, disk_r, halo_r)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    sources_dir = os.path.join(script_dir, "Sources")
    os.makedirs(sources_dir, exist_ok=True)
    preview_path = os.path.join(sources_dir, PREVIEW_NAME)
    Image.fromarray(_to_array(preview, size), "RGB").save(preview_path)
    print(_tag(f"Sungen: sun texture {size}x{size}  disk={disk_r}  halo={halo_r}"))
    print(_tag(f"Sungen: Preview: {preview_path}"))

    if args.output:
        write_header(pixels, size, disk_r, halo_r, args.output)
        print(_tag(f"Sungen: Flash header: {args.output}  ({size * size * 2} bytes)"))


if __name__ == "__main__":
    main()