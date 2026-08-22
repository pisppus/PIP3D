import sys
sys.dont_write_bytecode = True

import os
import argparse
import math

try:
    from PIL import Image, ImageFilter
except ImportError:
    print("[-] Error: Pillow library not found. It will be installed by the master script.")
    sys.exit(1)

if os.name == 'nt':
    import ctypes
    kernel32 = ctypes.windll.kernel32
    kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)


def _pixel_to_rgb565(r, g, b):
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    return (r5 << 11) | (g6 << 5) | b5


def _image_to_rgb565_array(img):
    pixels = img.load()
    w, h = img.size
    arr = []
    for y in range(h):
        for x in range(w):
            r, g, b = pixels[x, y]
            arr.append(_pixel_to_rgb565(r, g, b))
    return arr


def _write_hex_array_chunked(out, data, items_per_line=12, indent="            "):
    for i in range(0, len(data), items_per_line):
        chunk = data[i:i + items_per_line]
        hex_str = ", ".join(f"0x{v:04X}" for v in chunk)
        out.write(f"{indent}{hex_str},\n")


def prev_power_of_two(n):
    if n <= 1:
        return 1
    p = 1
    while (p << 1) <= n:
        p <<= 1
    return p


def _tag(msg):
    return f"\033[36m[Pip3D]\033[0m {msg}"


def _warn(msg):
    return f"\033[33m[Pip3D] WARNING: {msg}\033[0m"


def _err(msg):
    return f"\033[91m[-] Error: {msg}\033[0m"


def convert_png2tex(img_path, force_output_path=None, target_size=None, no_mip=False):
    if not os.path.exists(img_path):
        print(_err(f"Source image {img_path} not found!"))
        sys.exit(1)

    try:
        img = Image.open(img_path).convert('RGB')
    except Exception as e:
        print(_err(f"Opening image: {str(e)}"))
        sys.exit(1)

    orig_width, orig_height = img.size

    try:
        resample_lanczos = Image.Resampling.LANCZOS
    except AttributeError:
        resample_lanczos = Image.LANCZOS if hasattr(Image, "LANCZOS") else Image.ANTIALIAS

    orig_raw_name = os.path.splitext(os.path.basename(img_path))[0]
    sanitized = ''.join(c if (c.isascii() and c.isalnum() or c == '_') else '_' for c in orig_raw_name)
    if sanitized and sanitized[0].isdigit():
        sanitized = 'tex_' + sanitized
    if sanitized != orig_raw_name:
        print(_warn(f"texture name '{orig_raw_name}' contains invalid C++ chars; sanitized to '{sanitized}'."))
        print(_warn(f"  Tip: rename source file to '{sanitized}.png' for clarity."))
    raw_name = sanitized

    disable_mips = no_mip
    if raw_name.lower().endswith('_nomip'):
        disable_mips = True
        raw_name = raw_name[:-len('_nomip')]

    soft_downscale = False
    if raw_name.lower().endswith('_soft'):
        soft_downscale = True
        raw_name = raw_name[:-len('_soft')]

    name_parts = raw_name.split('_')
    clean_name = raw_name
    parsed_w = None
    parsed_h = None

    if len(name_parts) > 1:
        last = name_parts[-1]
        if 'x' in last:
            try:
                wp, hp = last.split('x')
                wv = int(wp)
                hv = int(hp)
                if wv >= 16 and (wv & (wv - 1)) == 0 and hv >= 16 and (hv & (hv - 1)) == 0:
                    parsed_w = wv
                    parsed_h = hv
                    clean_name = "_".join(name_parts[:-1])
            except ValueError:
                pass
        else:
            try:
                val = int(last)
                if val >= 16 and (val & (val - 1)) == 0:
                    parsed_w = val
                    parsed_h = val
                    clean_name = "_".join(name_parts[:-1])
            except ValueError:
                pass

    if target_size:
        target_w = min(target_size, min(orig_width, orig_height))
        target_h = target_w
    elif parsed_w and parsed_h:
        target_w = min(parsed_w, orig_width)
        target_h = min(parsed_h, orig_height)
    else:
        target_w = orig_width
        target_h = orig_height

    target_w = prev_power_of_two(target_w)
    target_h = prev_power_of_two(target_h)
    target_w = max(16, target_w)
    target_h = max(16, target_h)

    is_square = (target_w == target_h)

    if img.size != (target_w, target_h):
        if not is_square:
            src_aspect = orig_width / float(orig_height)
            dst_aspect = target_w / float(target_h)
            print(_tag(f"non-square texture: {orig_width}x{orig_height} → {target_w}x{target_h} "
                       f"(POT-quantized per axis, aspect {src_aspect:.2f}→{dst_aspect:.2f})"))
        img = img.resize((target_w, target_h), resample_lanczos)

        downscale = min(orig_width, orig_height) / float(min(target_w, target_h))
        if downscale >= 2.0 and not soft_downscale:
            img = img.filter(ImageFilter.UnsharpMask(radius=1.5, percent=70, threshold=2))
            print(_tag(f"unsharp mask after {downscale:.1f}x downscale (suffix _soft disables)"))

    base_array = _image_to_rgb565_array(img)
    width = target_w
    height = target_h
    shiftU = int(math.log2(width))
    shiftV = int(math.log2(height))

    mip_levels = []
    if disable_mips:
        print(_tag(f"mipmaps disabled (suffix _nomip) — saves ~33% flash but no minification filter."))
    else:
        cur_w, cur_h = width, height
        mip_img = img
        while cur_w > 1 or cur_h > 1:
            next_w = cur_w >> 1 if cur_w > 1 else 1
            next_h = cur_h >> 1 if cur_h > 1 else 1
            mip_img = mip_img.resize((next_w, next_h), resample_lanczos)
            if cur_w > 2 or cur_h > 2:
                mip_img = mip_img.filter(ImageFilter.GaussianBlur(radius=0.5))
            mip_levels.append((next_w, next_h, _image_to_rgb565_array(mip_img)))
            cur_w, cur_h = next_w, next_h

    mip_count = len(mip_levels)

    var_name = clean_name.lower()

    if force_output_path:
        header_path = force_output_path
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        target_dir = None
        curr = script_dir
        for _ in range(4):
            test_path = os.path.join(curr, "lib", "Pip3D", "Pip3D", "Rendering", "Resources")
            if os.path.exists(test_path):
                target_dir = os.path.join(test_path, "Textures")
                break
            curr = os.path.dirname(curr)

        if target_dir:
            header_path = os.path.join(target_dir, clean_name + ".hpp")
        else:
            header_path = os.path.splitext(img_path)[0] + ".hpp"

    base_bytes = width * height * 2
    mip_bytes = sum(len(arr) * 2 for _, _, arr in mip_levels)
    total_bytes = base_bytes + mip_bytes

    try:
        with open(header_path, 'w', encoding='utf-8') as out:
            out.write("/*\n")
            out.write(f" * Pip3D Texture Asset — {clean_name}\n")
            out.write(" * Generated automatically by Tools/Textures/Convert.py. Do not edit.\n")
            out.write(" *\n")
            out.write(f" * Source File   : {os.path.basename(img_path)} ({orig_width}x{orig_height})\n")
            out.write(f" * Format        : {'non-square (POT-quantized per axis)' if not is_square else 'square'}\n")
            out.write(f" * Texture Size  : {width}x{height} (RGB565, shiftU={shiftU}, shiftV={shiftV})\n")
            out.write(f" * Mipmaps       : {mip_count} level(s)\n")
            out.write(f" * Flash Memory  : {base_bytes} bytes base + {mip_bytes} bytes mips = {total_bytes} bytes ({total_bytes / 1024.0:.2f} KB)\n")
            out.write(" */\n\n")
            out.write("#pragma once\n\n")
            out.write("#include \"Rendering/Resources/Texture.hpp\"\n\n")
            out.write("namespace pip3D\n{\n")
            out.write("    namespace detail\n    {\n")
            out.write(f"        // Base Level (LOD 0): {width}x{height}\n")
            out.write(f"        alignas(16) static const uint16_t s_{var_name}TextureData[{width * height}] = {{\n")
            _write_hex_array_chunked(out, base_array, items_per_line=12)
            out.write("        };\n\n")

            if mip_count > 0:
                total_mip_pixels = sum(len(arr) for _, _, arr in mip_levels)
                out.write(f"        // Mipmap Levels (LOD 1 .. LOD {mip_count}): {total_mip_pixels} total pixels (each axis halved per LOD)\n")
                out.write(f"        alignas(16) static const uint16_t s_{var_name}MipData[{total_mip_pixels}] = {{\n")
                for level_idx, (mw, mh, arr) in enumerate(mip_levels):
                    out.write(f"            // LOD {level_idx + 1}: {mw}x{mh}\n")
                    _write_hex_array_chunked(out, arr, items_per_line=12)
                out.write("        };\n\n")

            out.write("    }\n\n")

            shiftV_value = shiftV if not is_square else 0
            out.write(f"    inline Texture g_{var_name}Texture = {{\n")
            out.write(f"        .data     = detail::s_{var_name}TextureData,\n")
            if mip_count > 0:
                out.write(f"        .mipData  = detail::s_{var_name}MipData,\n")
            else:
                out.write(f"        .mipData  = nullptr,\n")
            out.write(f"        .shiftU   = {shiftU},\n")
            out.write(f"        .shiftV   = {shiftV_value},\n")
            out.write(f"        .mipCount = {mip_count}\n")
            out.write("    };\n}\n")

        rel_img = os.path.join("Textures", "Sources", os.path.basename(img_path)).replace("\\", "/")
        rel_hpp = os.path.join("Rendering", "Resources", "Textures", os.path.basename(header_path)).replace("\\", "/")
        print(_tag(f"Converting: {rel_img} -> {rel_hpp} ({width}x{height} {'square' if is_square else 'non-square'} POT, {base_bytes / 1024.0:.2f} KB + {mip_count} mips = {mip_bytes / 1024.0:.2f} KB, {total_bytes / 1024.0:.2f} KB total)"))
        if max(width, height) >= 256:
            print(_warn(f"texture max dim is {max(width, height)}px = {base_bytes // 1024} KB base RGB565 — significant flash usage on ESP32-S3."))

    except Exception as e:
        print(_err(f"Exporting texture: {str(e)}"))
        sys.exit(1)

    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Pip3D Image -> C++ RGB565 Texture Converter (non-square + LANCZOS mips)",
        epilog="Filename conventions:\n"
               "  Name_64.png       → 64x64 square\n"
               "  Name_512x128.png  → 512x128 non-square\n"
               "  Name_nomip.png    → no mipmaps (saves ~33% flash)\n"
               "  Name_64_nomip.png → 64x64 square, no mips\n"
               "  Name_soft.png     → no unsharp on heavy downscale (default: mild unsharp at ≥2x)\n"
               "Without suffix: source size is used (POT-rounded, no upper limit).")
    parser.prog = "Convert"
    parser.add_argument("pairs", nargs="+",
                        help="input [output], or in1 out1 in2 out2 ... for batch")
    parser.add_argument("--size", type=int, help="Force specific (square) texture size")
    parser.add_argument("--nomip", action="store_true",
                        help="Disable mipmaps (same as _nomip filename suffix)")
    args = parser.parse_args()

    files = args.pairs
    if len(files) % 2 != 0:
        if len(files) == 1:
            files = [files[0], None]
        else:
            parser.error("expected input/output pairs: in1 out1 [in2 out2 ...]")

    for i in range(0, len(files), 2):
        convert_png2tex(files[i], files[i + 1],
                        (args.size if args.size else None),
                        no_mip=args.nomip)