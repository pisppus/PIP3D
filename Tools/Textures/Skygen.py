import sys
sys.dont_write_bytecode = True

import os
import argparse
import numpy as np

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


TILE_W            = 256
TILE_H            = 256
DEFAULT_SCREEN_W  = 480
DEFAULT_SCREEN_H  = 320
DEFAULT_COVERAGE  = 0.42
DEFAULT_SEED      = 0xC10D
DEFAULT_CLOUD     = "250,250,252"

PREVIEW_FILENAME  = "_CloudsData.png"


def _hash_grid(ix, iy, seed):
    h = (seed & 0xFFFF) ^ ((ix * 374761393) ^ (iy * 668265263))
    h ^= h >> 16
    h = (h * np.int64(0x7FEB352D)) & 0xFFFFFFFF
    h ^= h >> 15
    h = (h * np.int64(0x846CA68B)) & 0xFFFFFFFF
    h ^= h >> 16
    return h.astype(np.float32) * (1.0 / 4294967296.0)


def value_noise_2d(xs, ys, wrap_x, wrap_y, seed):
    x0  = np.floor(xs).astype(np.int32)
    y0  = np.floor(ys).astype(np.int32)
    tx  = (xs - x0).astype(np.float32)
    ty  = (ys - y0).astype(np.float32)
    u   = tx * tx * tx * (tx * (tx * 6 - 15) + 10)
    v   = ty * ty * ty * (ty * (ty * 6 - 15) + 10)
    x0w = x0 % wrap_x
    x1w = (x0 + 1) % wrap_x
    y0w = y0 % wrap_y
    y1w = (y0 + 1) % wrap_y
    c00 = _hash_grid(x0w, y0w, seed)
    c10 = _hash_grid(x1w, y0w, seed)
    c01 = _hash_grid(x0w, y1w, seed)
    c11 = _hash_grid(x1w, y1w, seed)
    a   = c00 + (c10 - c00) * u
    b   = c01 + (c11 - c01) * u
    return a + (b - a) * v


def fbm2d(xs, ys, wrap_x, wrap_y, octaves, seed):
    d    = np.zeros_like(xs, dtype=np.float32)
    amp  = 0.5
    norm = 0.0
    for o in range(octaves):
        d   += value_noise_2d(xs, ys, wrap_x, wrap_y, seed ^ (o * 7919)) * amp
        norm += amp
        xs   = xs * 2.0
        ys   = ys * 2.0
        amp  *= 0.5
    return d / norm


def _worley_hash2(ix, iy, seed):
    ix  = ix.astype(np.int64)
    iy  = iy.astype(np.int64)
    s1  = np.int64(seed & 0xFFFF)
    h1  = s1 ^ (ix * np.int64(374761393)) ^ (iy * np.int64(668265263))
    h1  = h1 ^ ((h1 >> 16) & np.int64(0xFFFFFFFF))
    h1  = (h1 * np.int64(0x7FEB352D)) & np.int64(0xFFFFFFFF)
    h1  = h1 ^ ((h1 >> 15) & np.int64(0xFFFFFFFF))
    h1  = (h1 * np.int64(0x846CA68B)) & np.int64(0xFFFFFFFF)
    h1  = h1 ^ ((h1 >> 16) & np.int64(0xFFFFFFFF))
    ux  = (h1.astype(np.float64) * (1.0 / 4294967296.0)).astype(np.float32)
    s2  = np.int64((seed ^ 0xA5A5A5) & 0xFFFF)
    h2  = s2 ^ (ix * np.int64(60498839)) ^ (iy * np.int64(19583743))
    h2  = h2 ^ ((h2 >> 16) & np.int64(0xFFFFFFFF))
    h2  = (h2 * np.int64(0x5BD1E995)) & np.int64(0xFFFFFFFF)
    h2  = h2 ^ ((h2 >> 13) & np.int64(0xFFFFFFFF))
    uy  = (h2.astype(np.float64) * (1.0 / 4294967296.0)).astype(np.float32)
    return ux, uy


def worley_f1(xs, ys, nx_cells, ny_cells, seed):
    ix  = np.floor(xs).astype(np.int32)
    iy  = np.floor(ys).astype(np.int32)
    fx  = (xs - ix).astype(np.float32)
    fy  = (ys - iy).astype(np.float32)
    f1  = np.full(xs.shape, 1e9, dtype=np.float32)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            cxw      = (ix + dx) % nx_cells
            cyw      = (iy + dy) % ny_cells
            ux, uy   = _worley_hash2(cxw, cyw, seed)
            d2       = (fx - dx - ux) ** 2 + (fy - dy - uy) ** 2
            f1       = np.where(d2 < f1, d2, f1)
    return np.sqrt(f1)


def cloud_density(xs, ys, nx, ny, seed):
    wx      = fbm2d(xs + 1.7, ys + 9.2, nx, ny, 3, seed ^ 0xAABB) * 2.0 - 1.0
    wy      = fbm2d(xs + 8.3, ys + 2.8, nx, ny, 3, seed ^ 0xCCDD) * 2.0 - 1.0
    xs2     = xs + wx * 0.6
    ys2     = ys + wy * 0.4
    cell_size = 0.50
    f1      = worley_f1(xs2, ys2, nx, ny, seed)
    density = np.clip(1.0 - f1 / cell_size, 0.0, 1.0)
    detail  = fbm2d(xs2 * 3.0, ys2 * 3.0, nx, ny, 4, seed ^ 0xF00D) * 0.35
    return np.clip(density + detail * density, 0.0, 1.0)


SHADE_MIN = 0.72
SHADE_MAX = 1.07


def gen_cloud_mask(coverage, seed):
    nx     = 8
    ny     = 8
    xs_lin = np.linspace(0, nx, TILE_W, endpoint=False, dtype=np.float32)
    ys_lin = np.linspace(0, ny, TILE_H, endpoint=False, dtype=np.float32)
    xs, ys = np.meshgrid(xs_lin, ys_lin)

    density = cloud_density(xs, ys, nx, ny, seed)

    cov_w   = coverage * 0.36
    d       = np.clip((density - cov_w) / (1.0 - cov_w), 0.0, 1.0)
    d_sharp = np.power(d, 0.6)
    alpha   = d_sharp * d_sharp * (3.0 - 2.0 * d_sharp)

    inner = np.clip(alpha - 0.55, 0.0, 1.0) * 0.20
    shade = np.clip(0.85 - inner, SHADE_MIN, SHADE_MAX)
    rim   = np.clip(1.0 - np.abs(alpha - 0.22) * 6.0, 0.0, 1.0) * 0.07
    shade = np.clip(shade + rim, SHADE_MIN, SHADE_MAX)

    alpha_plane = np.clip(np.rint(alpha * 255.0), 0, 255).astype(np.uint8)
    shade_plane = np.clip(np.rint((shade - SHADE_MIN) / (SHADE_MAX - SHADE_MIN) * 255.0), 0, 255).astype(np.uint8)
    return alpha_plane, shade_plane


def mask_to_preview(alpha_plane, shade_plane, cloud_rgb):
    a_norm = alpha_plane.astype(np.float32) / 255.0
    s_norm = SHADE_MIN + shade_plane.astype(np.float32) / 255.0 * (SHADE_MAX - SHADE_MIN)
    cr, cg, cb = cloud_rgb
    r = np.clip(cr * s_norm * a_norm, 0, 255)
    g = np.clip(cg * s_norm * a_norm, 0, 255)
    b = np.clip(cb * s_norm * a_norm, 0, 255)
    tiled = np.tile(np.stack([r, g, b], axis=-1).astype(np.uint8), (4, 4, 1))
    return tiled


def fmt_array_chunked(plane, items_per_line=16):
    lines = []
    for y in range(TILE_H):
        row = plane[y]
        for i in range(0, TILE_W, items_per_line):
            chunk = row[i:i + items_per_line]
            hex_str = ", ".join(f"0x{v:02X}" for v in chunk)
            lines.append(f"            {hex_str},")
    return "\n".join(lines)


def write_mask_header(alpha_plane, shade_plane, cloud_rgb, coverage, seed, out_path):
    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    cr, cg, cb = cloud_rgb
    default565 = to565(int(cr), int(cg), int(cb))
    total = alpha_plane.nbytes + shade_plane.nbytes

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("/*\n")
        f.write(" * Pip3D Cloud Mask Asset — Square Tileable\n")
        f.write(" * Generated automatically by Tools/Textures/Skygen.py. Do not edit.\n")
        f.write(" *\n")
        f.write(f" * Tile Size      : {TILE_W}x{TILE_H} (2 Planes: Alpha + Shade)\n")
        f.write(f" * Wrap           : Seamless on both axes (periodic noise)\n")
        f.write(f" * Parameters     : Coverage={coverage:.2f}, Seed={seed:#x}\n")
        f.write(f" * Default Color  : RGB({cr}, {cg}, {cb}) -> 0x{default565:04X}\n")
        f.write(f" * Flash Memory   : {total} bytes ({total / 1024.0:.2f} KB)\n")
        f.write(" */\n\n")
        f.write("#pragma once\n\n")
        f.write("#include <cstdint>\n\n")
        f.write("namespace pip3D\n{\n")
        f.write("    namespace detail\n    {\n")
        f.write(f"        // Alpha Channel Map ({TILE_W}x{TILE_H}, tileable)\n")
        f.write(f"        alignas(16) static const uint8_t s_cloudsAlphaData[{TILE_H}][{TILE_W}] =\n        {{\n")
        f.write(fmt_array_chunked(alpha_plane) + "\n")
        f.write("        };\n\n")
        f.write(f"        // Shade Factor Map ({TILE_W}x{TILE_H}, tileable)\n")
        f.write(f"        alignas(16) static const uint8_t s_cloudsShadeData[{TILE_H}][{TILE_W}] =\n        {{\n")
        f.write(fmt_array_chunked(shade_plane) + "\n")
        f.write("        };\n    }\n\n")
        f.write(f"    inline constexpr uint16_t        CLOUDS_PANO_W        = {TILE_W};\n")
        f.write(f"    inline constexpr uint16_t        CLOUDS_PANO_H        = {TILE_H};\n")
        f.write(f"    inline constexpr uint16_t        CLOUDS_PANO_REPEATS  = 1;\n")
        f.write(f"    inline constexpr uint16_t        CLOUDS_DEFAULT_COLOR = 0x{default565:04X};\n")
        f.write("}\n")
    return out_path, total


def to565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def main():
    p = argparse.ArgumentParser(description="pip3D cloud mask generator (square tileable)")
    p.add_argument("output",      nargs="?")
    p.add_argument("--coverage",  type=float, default=DEFAULT_COVERAGE)
    p.add_argument("--seed",      type=lambda x: int(x, 0), default=DEFAULT_SEED)
    p.add_argument("--cloud",     default=DEFAULT_CLOUD)
    p.add_argument("--screen-w",  type=int, default=DEFAULT_SCREEN_W)
    p.add_argument("--screen-h",  type=int, default=DEFAULT_SCREEN_H)
    args = p.parse_args()

    cloud_rgb = tuple(int(v) for v in args.cloud.split(","))

    print(_tag(f"Skygen: square tileable cloud mask coverage={args.coverage}  seed={args.seed:#x}  cloud={cloud_rgb}"))
    print(_tag(f"Skygen: texture={TILE_W}x{TILE_H}  (screen {args.screen_w}x{args.screen_h})"))

    alpha_plane, shade_plane = gen_cloud_mask(args.coverage, args.seed)

    script_dir   = os.path.dirname(os.path.abspath(__file__))
    sources_dir  = os.path.join(script_dir, "Asset")
    os.makedirs(sources_dir, exist_ok=True)
    preview_path = os.path.join(sources_dir, PREVIEW_FILENAME)
    Image.fromarray(mask_to_preview(alpha_plane, shade_plane, cloud_rgb), "RGB").save(preview_path)
    print(_tag(f"Skygen: Preview (4x4 tiled): {preview_path}"))

    if args.output:
        hpp, total = write_mask_header(alpha_plane, shade_plane, cloud_rgb, args.coverage, args.seed, args.output)
        print(_tag(f"Skygen: Flash header: {hpp}  ({total} bytes total, 0 RAM)"))


if __name__ == "__main__":
    main()