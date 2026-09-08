import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SPV = os.path.join(HERE, "TraceVulkan.spv")
HPP = os.path.join(HERE, "TraceSpv.hpp")


def main():
    with open(SPV, "rb") as f:
        words = f.read()
    if len(words) % 4 != 0:
        sys.exit("SPIR-V size is not a multiple of 4")
    n = len(words) // 4

    lines = []
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <cstddef>")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append("namespace pip3D")
    lines.append("{")
    lines.append("    namespace Bake")
    lines.append("    {")
    lines.append("        namespace Gpu")
    lines.append("        {")
    lines.append(f"            inline constexpr size_t kVulkanSpvSize = {len(words)};")
    lines.append("            inline constexpr uint32_t kVulkanSpv[] = {")
    for i in range(0, n, 6):
        chunk = ["0x%08xu" % int.from_bytes(words[(i + k) * 4:(i + k + 1) * 4], "little")
                 for k in range(min(6, n - i))]
        lines.append("                " + ", ".join(chunk) + ",")
    lines.append("            };")
    lines.append("        }")
    lines.append("    }")
    lines.append("}")
    with open(HPP, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"OK: {HPP} ({n} SPIR-V words, {len(words)} bytes)")


if __name__ == "__main__":
    main()
