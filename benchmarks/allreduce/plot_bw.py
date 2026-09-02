import os
import re
from typing import Optional, Dict, Tuple, List

import numpy as np
import matplotlib

# Use LaTeX for all figure text and embed TrueType fonts in vector outputs.
# Important: set these rcParams before importing matplotlib.pyplot.
matplotlib.rcParams.update({
    "text.usetex": True,
    "font.family": "serif",
    "pdf.fonttype": 42,
    "ps.fonttype": 42,
})

import matplotlib.pyplot as plt
import matplotlib.transforms as mtransforms


# --------- CONFIG ----------
BASE_DIR = os.path.abspath(os.path.dirname(__file__))
INPUT_DIR = os.path.join(BASE_DIR, "output")
OUTPUT_DIR = os.path.join(BASE_DIR, "output/plots/bw")
TOPOLOGY_PREFIX = "torus_"

# Only include torus_32x32_* (e.g., torus_32x32_200Gb)
TARGET_TORUS_SHAPE = "32x32"
TARGET_PREFIX = f"{TOPOLOGY_PREFIX}{TARGET_TORUS_SHAPE}_"

# Variants:
# For algorithms with L and B: take per-message-size min(L,B).
ALGO_GROUPS = {
    "Trivance": {"B": "TrivanceB", "L": "TrivanceL"},
    "RecDoub":  {"B": "RecDoubB",  "L": "RecDoubL"},
    "Swing":    {"B": "SwingB",    "L": "SwingL"},
    "Bucket":   {"B": "Bucket",    "L": None},
    "Bruck":    {"B": "BruckB",    "L": "BruckL"},
}

TIME_RE = re.compile(r"simulated time:\s*([0-9]*\.?[0-9]+)\s*([a-zA-Zµ]+)", re.IGNORECASE)


# Filename is "count"; message size in BYTES = count * 4
def msg_bytes_from_filename(fname: str) -> Optional[int]:
    base = os.path.splitext(fname)[0]
    if not re.fullmatch(r"\d+", base):
        return None
    return int(base) * 4  # bytes


def to_microseconds(value: float, unit: str) -> float:
    u = unit.strip().lower()
    if u in ("us", "µs"):
        return value
    if u == "ns":
        return value / 1000.0
    if u == "ms":
        return value * 1000.0
    if u in ("s", "sec", "secs", "second", "seconds"):
        return value * 1_000_000.0
    raise ValueError("Unsupported time unit '{}'".format(unit))


def read_sim_time_us(filepath: str) -> float:
    with open(filepath, "r", errors="ignore") as f:
        txt = f.read()
    m = TIME_RE.search(txt)
    if not m:
        raise ValueError("No 'simulated time' found in {}".format(filepath))
    return to_microseconds(float(m.group(1)), m.group(2))


def list_target_topologies() -> List[str]:
    if not os.path.isdir(INPUT_DIR):
        raise FileNotFoundError("Missing input folder: {}".format(INPUT_DIR))

    tops = []
    for name in os.listdir(INPUT_DIR):
        p = os.path.join(INPUT_DIR, name)
        if os.path.isdir(p) and name.startswith(TARGET_PREFIX):
            tops.append(name)
    return sorted(tops)


def read_variant_dir(variant_dir: Optional[str], missing_logged: set) -> Dict[int, float]:
    if not variant_dir:
        return {}

    if not os.path.isdir(variant_dir):
        if variant_dir not in missing_logged:
            print("MISSING VARIANT DIR:", variant_dir)
            missing_logged.add(variant_dir)
        return {}

    m: Dict[int, float] = {}
    for fname in os.listdir(variant_dir):
        mbytes = msg_bytes_from_filename(fname)
        if mbytes is None:
            continue
        fpath = os.path.join(variant_dir, fname)
        if not os.path.isfile(fpath):
            continue
        try:
            t_us = read_sim_time_us(fpath)
        except Exception:
            continue
        m[mbytes] = t_us
    return m


def collect_algo_curve_best_of_LB(
    topology_dir: str,
    variant_L: Optional[str],
    variant_B: Optional[str],
    missing_logged: set
) -> Tuple[np.ndarray, np.ndarray, Optional[int]]:
    L_dir = os.path.join(topology_dir, variant_L) if variant_L else None
    B_dir = os.path.join(topology_dir, variant_B) if variant_B else None

    L_map = read_variant_dir(L_dir, missing_logged)
    B_map = read_variant_dir(B_dir, missing_logged)

    if not L_map and not B_map:
        return np.array([], dtype=np.int64), np.array([], dtype=float), None

    xs_all = sorted(set(L_map.keys()) | set(B_map.keys()))
    xs = []
    ys = []
    first_B_x = None

    for x in xs_all:
        l = L_map.get(x, None)
        b = B_map.get(x, None)

        if l is None and b is None:
            continue
        if l is None:
            xs.append(x)
            ys.append(b)
            continue
        if b is None:
            xs.append(x)
            ys.append(l)
            continue

        if b < l and first_B_x is None:
            first_B_x = x
        xs.append(x)
        ys.append(min(l, b))

    return np.array(xs, dtype=np.int64), np.array(ys, dtype=float), first_B_x


def fmt_bytes(b: int) -> str:
    if b < 1024:
        return "{} B".format(b)
    if b < 1024**2:
        return "{} KiB".format(b // 1024)
    if b < 1024**3:
        return "{} MiB".format(b // (1024**2))
    return "{} GiB".format(b // (1024**3))


def curve_to_dict(xs: np.ndarray, ys: np.ndarray) -> Dict[int, float]:
    return {int(x): float(y) for x, y in zip(xs.tolist(), ys.tolist())}


def bandwidth_label_from_topology_name(topology_name: str) -> str:
    # torus_32x32_200Gb -> 200Gb
    if topology_name.startswith(TARGET_PREFIX):
        return topology_name[len(TARGET_PREFIX):]
    return topology_name


def numeric_key_for_bw(label: str) -> Tuple[int, str]:
    # crude sorting: extract leading number if present (e.g., 200Gb -> 200)
    m = re.match(r"^\s*(\d+)", label)
    return (int(m.group(1)) if m else 10**18, label)


def latex_escape_text(text: str) -> str:
    """Escape dynamic labels so they are safe with text.usetex=True."""
    replacements = {
        "\\": r"\textbackslash{}",
        "&": r"\&",
        "%": r"\%",
        "$": r"\$",
        "#": r"\#",
        "_": r"\_",
        "{": r"\{",
        "}": r"\}",
        "~": r"\textasciitilde{}",
        "^": r"\textasciicircum{}",
    }
    return "".join(replacements.get(ch, ch) for ch in text)


def best_nonbaseline_vs_trivance_pct(
    topology_dir: str,
    missing_logged: set,
) -> Tuple[np.ndarray, np.ndarray]:
    # Trivance baseline
    triv = ALGO_GROUPS["Trivance"]
    x_t, y_t, _ = collect_algo_curve_best_of_LB(topology_dir, triv.get("L"), triv.get("B"), missing_logged)
    if x_t.size == 0:
        return np.array([], dtype=np.int64), np.array([], dtype=float)
    base_map = curve_to_dict(x_t, y_t)

    # Collect each non-baseline algo maps
    algo_maps: Dict[str, Dict[int, float]] = {}
    for algo, variants in ALGO_GROUPS.items():
        if algo == "Trivance":
            continue
        x_a, y_a, _ = collect_algo_curve_best_of_LB(topology_dir, variants.get("L"), variants.get("B"), missing_logged)
        if x_a.size == 0:
            continue
        algo_maps[algo] = curve_to_dict(x_a, y_a)

    if not algo_maps:
        return np.array([], dtype=np.int64), np.array([], dtype=float)

    # For each message size, pick the best (minimum time) among algos that exist at that x
    xs = sorted(base_map.keys())
    x_out: List[int] = []
    y_out: List[float] = []

    for x in xs:
        base = base_map.get(x, None)
        if base is None:
            continue

        best_time = None
        for amap in algo_maps.values():
            t = amap.get(x, None)
            if t is None:
                continue
            if best_time is None or t < best_time:
                best_time = t

        if best_time is None:
            continue

        pct = (best_time - base) / base * 100.0
        x_out.append(x)
        y_out.append(pct)

    return np.array(x_out, dtype=np.int64), np.array(y_out, dtype=float)


def plot_all_bandwidths_one_figure() -> None:
    tops = list_target_topologies()
    if not tops:
        raise RuntimeError("No {}* folders found in {}".format(TARGET_PREFIX, INPUT_DIR))

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    plt.figure(figsize=(10,6))

    font_scale = 2.3
    base_fontsize = plt.rcParams.get("font.size", 10)
    fs = base_fontsize * font_scale

    missing_logged = set()

    any_plotted = False
    x_all = set()

    # Sort by bandwidth label
    tops_sorted = sorted(tops, key=lambda name: numeric_key_for_bw(bandwidth_label_from_topology_name(name)))

    for topo in tops_sorted:
        topology_dir = os.path.join(INPUT_DIR, topo)
        bw_label = bandwidth_label_from_topology_name(topo)

        x_bw, y_bw = best_nonbaseline_vs_trivance_pct(topology_dir, missing_logged)
        if x_bw.size == 0:
            print("SKIP (missing baseline or no non-baseline overlap):", topo)
            continue

        any_plotted = True
        x_all |= set(x_bw.tolist())

        plt.plot(
            x_bw,
            y_bw,
            linestyle="-",
            linewidth=2.3,
            marker="o",
            markersize=10,
            label=latex_escape_text(bw_label),
        )

    if not any_plotted:
        raise RuntimeError("No plots generated (no bandwidth topology produced a curve).")

    plt.xscale("log", base=2)
    plt.ylim(-20, 25)

    # X ticks: union across all plotted bandwidths
    x_ref = np.array(sorted(x_all), dtype=np.int64)
    x_labels = [latex_escape_text(fmt_bytes(int(v))) for v in x_ref.tolist()]
    plt.xticks(x_ref, x_labels, rotation=45, fontsize=fs)

    # Y ticks: show every second label
    ax = plt.gca()
    yticks = ax.get_yticks()
    ax.set_yticks(yticks)
    ax.set_yticklabels(
        [("{:.0f}".format(t) if (i % 2 == 0) else "") for i, t in enumerate(yticks)],
        fontsize=fs,
    )

    plt.axhline(0, color="gray", linewidth=1, linestyle="--")

    plt.xlabel(r"AllReduce Size", fontsize=fs*1.2)
    plt.ylabel(
        r"Best Algorithm Relative" + "\n" + r"Completion Time vs.\ \textsc{Trivance} (\%)",
        fontsize=fs*1.2,
    )

    plt.grid(True, which="both", linestyle="--", linewidth=0.5)

    # BETTER/WORSE arrows at right
    trans = mtransforms.blended_transform_factory(ax.transAxes, ax.transData)
    x_out = 1.02
    arrow_len = 25
    arrow_len_z = 20

    ax.annotate(
        "",
        xy=(x_out, arrow_len),
        xytext=(x_out, 0),
        xycoords=trans,
        textcoords=trans,
        arrowprops=dict(arrowstyle="-|>,head_width=0.6,head_length=0.8", color="green", linewidth=5),
        clip_on=False,
    )
    ax.text(
        x_out + 0.015, arrow_len * 0.5,
        r"\textsc{Trivance}" + "\n" + r"\textsc{better}",
        transform=trans,
        va="center",
        ha="left",
        fontsize=fs,
        color="green",
    )

    ax.annotate(
        "",
        xy=(x_out, -arrow_len_z),
        xytext=(x_out, 0),
        xycoords=trans,
        textcoords=trans,
        arrowprops=dict(arrowstyle="-|>,head_width=0.6,head_length=0.8", color="red", linewidth=5),
        clip_on=False,
    )
    ax.text(
        x_out + 0.015, -arrow_len * 0.5 + 4.5,
        r"\textsc{Trivance}" + "\n" + r"\textsc{worse}",
        transform=trans,
        va="center",
        ha="left",
        fontsize=fs,
        color="red",
    )

    plt.legend(
        loc="lower left",
        bbox_to_anchor=(0.02, 0.02),
        fontsize=fs * 0.6,
        frameon=True,
        fancybox=True,
        framealpha=0.9,
        edgecolor="none",
        facecolor="white",
        handlelength=2.5,
        handletextpad=0.6,
        borderpad=0.6,
        labelspacing=0.4,
        title=r"Bandwidth",
        title_fontsize=fs * 0.65,
    )

    plt.tight_layout()

    out_base = f"{TOPOLOGY_PREFIX}{TARGET_TORUS_SHAPE}_ALL_BW_best_vs_trivance"
    pdf_path = os.path.join(OUTPUT_DIR, out_base + ".pdf")

    plt.savefig(pdf_path, bbox_inches="tight")
    plt.close()

    print("SAVED:", pdf_path)


def main() -> None:
    plot_all_bandwidths_one_figure()


if __name__ == "__main__":
    main()
