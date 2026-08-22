import sys
sys.dont_write_bytecode = True

import os
import argparse

if os.name == 'nt':
    import ctypes
    kernel32 = ctypes.windll.kernel32
    kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)


def _tag(msg):
    return f"\033[36m[Pip3D]\033[0m {msg}"


def _err(msg):
    return f"\033[91m[-] Error: {msg}\033[0m"


def write_missing_texture(out_path, size=32, min_mip=8):
    if size < 2 or (size & (size - 1)) != 0:
        print(_err(f"size must be POT ≥ 2, got {size}"))
        sys.exit(1)
    if min_mip < 1 or (min_mip & (min_mip - 1)) != 0:
        print(_err(f"min_mip must be POT ≥ 1, got {min_mip}"))
        sys.exit(1)
    if min_mip > size:
        print(_err(f"min_mip ({min_mip}) cannot be larger than size ({size})"))
        sys.exit(1)

    PINK = 0xF81F
    BLACK = 0x0000

    cell_size = max(2, size // 8)

    base_array = []
    for y in range(size):
        for x in range(size):
            cell_x = x // cell_size
            cell_y = y // cell_size
            base_array.append(PINK if (cell_x + cell_y) % 2 == 0 else BLACK)

    mips = []
    cur = size
    while cur > 1:
        cur = cur >> 1
        if cur < min_mip:
            break
        arr = []
        for y in range(cur):
            for x in range(cur):
                src_x = min((x * size) // cur, size - 1)
                src_y = min((y * size) // cur, size - 1)
                cell_x = src_x // cell_size
                cell_y = src_y // cell_size
                arr.append(PINK if (cell_x + cell_y) % 2 == 0 else BLACK)
        mips.append((cur, cur, arr))

    shift = 0
    n = size
    while (1 << shift) < n:
        shift += 1
    assert (1 << shift) == n, "size not POT"

    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    with open(out_path, 'w', encoding='utf-8') as out:
        out.write("/*\n")
        out.write(f" * Pip3D Texture Asset — Missing (fallback)\n")
        out.write(" * Generated automatically by Tools/Textures/Missinggen.py. Do not edit.\n")
        out.write(" *\n")
        out.write(f" * Texture Size  : {size}x{size} (RGB565, shiftU={shift}, shiftV=0)\n")
        out.write(f" * Mipmaps       : {len(mips)} level(s), min LOD {min_mip}x{min_mip}\n")
        out.write(f" * Pattern       : Pink (0xF81F) / Black (0x0000) checkerboard, cell = {cell_size}x{cell_size} px\n")
        out.write(f" * Purpose       : Fallback when a mesh references a texture that failed to load\n")
        out.write(f" * Flash Memory : {size*size*2} bytes base + {sum(len(m)*2 for _,_,m in mips)} bytes mips = {size*size*2 + sum(len(m)*2 for _,_,m in mips)} bytes ({(size*size*2 + sum(len(m)*2 for _,_,m in mips))/1024:.2f} KB)\n")
        out.write(" */\n\n")
        out.write("#pragma once\n\n")
        out.write("#include \"Rendering/Resources/Texture.hpp\"\n\n")
        out.write("namespace pip3D\n{\n")
        out.write("    namespace detail\n    {\n")
        out.write(f"        // Base Level (LOD 0): {size}x{size}\n")
        out.write(f"        alignas(16) static const uint16_t s_missingTextureData[{size*size}] = {{\n")
        for i in range(0, len(base_array), 12):
            chunk = base_array[i:i+12]
            out.write("            " + ", ".join(f"0x{v:04X}" for v in chunk) + ",\n")
        out.write("        };\n\n")

        if mips:
            total_mip_pixels = sum(len(arr) for _, _, arr in mips)
            out.write(f"        // Mipmap Levels (LOD 1 .. LOD {len(mips)}): {total_mip_pixels} total pixels (each axis halved per LOD; min LOD = {min_mip}x{min_mip})\n")
            out.write(f"        alignas(16) static const uint16_t s_missingMipData[{total_mip_pixels}] = {{\n")
            for level_idx, (mw, mh, arr) in enumerate(mips):
                out.write(f"            // LOD {level_idx + 1}: {mw}x{mh}\n")
                for i in range(0, len(arr), 12):
                    chunk = arr[i:i+12]
                    out.write("            " + ", ".join(f"0x{v:04X}" for v in chunk) + ",\n")
            out.write("        };\n\n")

        out.write("    }\n\n")
        out.write(f"    inline Texture g_missingTexture = {{\n")
        out.write(f"        .data     = detail::s_missingTextureData,\n")
        out.write(f"        .mipData  = detail::s_missingMipData,\n")
        out.write(f"        .shiftU   = {shift},\n")
        out.write(f"        .shiftV   = 0,\n")
        out.write(f"        .mipCount = {len(mips)}\n")
        out.write("    };\n}\n")

    total = size*size*2 + sum(len(arr)*2 for _, _, arr in mips)
    print(_tag(f"Missinggen: fallback texture {size}x{size} (cell {cell_size}px), "
               f"{len(mips)} mips (min {min_mip}x{min_mip}), "
               f"{total} bytes ({total/1024:.2f} KB)"))
    print(_tag(f"Missinggen: Flash header: {out_path}"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate Pip3D fallback 'Missing' texture (pink/black checkerboard)")
    parser.prog = "Missinggen"
    parser.add_argument("output", nargs="?", help="Output .hpp file path")
    parser.add_argument("--size", type=int, default=32, help="Texture size (POT, default 32)")
    parser.add_argument("--min-mip", type=int, default=8, help="Minimum mip size (POT, default 8 — stop mips at 8x8)")
    args = parser.parse_args()

    if args.output:
        write_missing_texture(args.output, args.size, args.min_mip)
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        target_dir = None
        curr = script_dir
        for _ in range(4):
            test_path = os.path.join(curr, "lib", "Pip3D", "Pip3D", "Rendering", "Resources", "Textures")
            if os.path.exists(test_path):
                target_dir = test_path
                break
            curr = os.path.dirname(curr)

        if target_dir:
            out_path = os.path.join(target_dir, "Missing.hpp")
            write_missing_texture(out_path, args.size, args.min_mip)
        else:
            print(_err("Could not locate lib/Pip3D/.../Textures/. Pass explicit output path."))
            sys.exit(1)