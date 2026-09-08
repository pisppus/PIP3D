import sys

import numpy as np
from PIL import Image
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

ANSI_RESET = "\033[0m"
ANSI_GREEN = "\033[32m"
ANSI_YELLOW = "\033[33m"

CLASS_COLORS = {
    "uniform": "#FFDC3C",
    "ramp": "#5AE65A",
    "pal8": "#46C8FF",
    "pal16": "#FF783C",
    "pal32": "#C86EFF",
    "drift": "#FF0000",
}
CLASS_ORDER = ("uniform", "ramp", "pal8", "pal16", "pal32")


def _tag(color, msg):
    return f"{color}[Pip3D]{ANSI_RESET} {msg}"


def parse_meta(img):
    meta = {}
    for field in img.info.get("Pip3D", "").split(";"):
        key, _, value = field.partition("=")
        if key:
            meta[key.strip()] = value.strip()
    return meta


def build_title(meta):
    scene = meta.get("scene", "Scene")
    mode = meta.get("mode", "")
    title = f"Pip3D baked sparse map: {scene}" + (f" ({mode})" if mode else "")
    stats = []
    if meta.get("w"):
        size = meta["w"] + "\u00d7" + meta.get("h", "?")
        if meta.get("fill"):
            size += f", {float(meta['fill']):.1f}% fill"
        stats.append(size)
    if meta.get("avg"):
        stats.append(f"codec error avg {float(meta['avg']):.2f} / max {float(meta['max']):.0f} LSB")
    if meta.get("drift"):
        stats.append(f"drift texels {meta['drift']}")
    return title + ("\n" + "   \u2014   ".join(stats) if stats else "")


def build_handles(meta):
    handles = []
    for k in CLASS_ORDER:
        count = meta.get(k)
        if count and count != "0":
            handles.append(Patch(facecolor=CLASS_COLORS[k], edgecolor="none",
                                 label=f"{k.upper()} \u00d7{count}"))
    if meta.get("drift") and meta["drift"] != "0":
        handles.append(Patch(facecolor=CLASS_COLORS["drift"], edgecolor="none",
                             label=f"DRIFT 2+ LSB \u00d7{meta['drift']}"))
    return handles


def render(sparse_png, out_png):
    img = Image.open(sparse_png)
    meta = parse_meta(img)
    handles = build_handles(meta)

    fig, ax = plt.subplots(
        figsize=(14, 14 * img.size[1] / img.size[0]),
        constrained_layout=True,
    )
    ax.imshow(np.asarray(img.convert("RGB")), interpolation="nearest")
    ax.set_axis_off()
    ax.set_title(build_title(meta), fontsize=11, pad=12)

    if handles:
        try:
            fig.legend(
                handles=handles,
                loc="outside lower center",
                ncol=3,
                fontsize=9,
                framealpha=0.8,
                title="8\u00d78 block classes",
                title_fontproperties={"weight": "bold", "size": 9},
            )
        except ValueError:
            ax.legend(
                handles=handles,
                loc="upper center",
                bbox_to_anchor=(0.5, -0.01),
                ncol=3,
                fontsize=9,
                framealpha=0.8,
                title="8\u00d78 block classes",
                title_fontproperties={"weight": "bold", "size": 9},
            )

    fig.savefig(out_png, dpi=150)
    plt.close(fig)


def main():
    if len(sys.argv) < 2:
        print(_tag(ANSI_YELLOW, "usage: Sparsemap.py <Sparse.png> [out.png]"))
        return 1
    sparse_png = sys.argv[1]
    out_png = sys.argv[2] if len(sys.argv) > 2 else sparse_png
    render(sparse_png, out_png)
    print(_tag(ANSI_GREEN, f"Sparse map report: {out_png}"))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ImportError as e:
        print(_tag(ANSI_YELLOW, f"Sparse map report skipped ({e})"))
        sys.exit(0)
    except Exception as e:
        print(_tag(ANSI_YELLOW, f"Sparse map report failed: {e}"))
        sys.exit(0)
