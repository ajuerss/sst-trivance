import os
import re
from typing import Optional, Dict, Tuple

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
# plot.py sits next to:
#   ./output/torus_<size>/...   (input)
# Plots are written to:
#   ./output/plots/
BASE_DIR = os.path.abspath(os.path.dirname(__file__))
INPUT_DIR = os.path.join(BASE_DIR, "output")
OUTPUT_DIR = os.path.join(BASE_DIR, "output/plots")
TOPOLOGY_PREFIX = "torus_"
PDF_DIR = os.path.join(OUTPUT_DIR)

# Variants:
# For algorithms with L and B: take per-message-size min(L,B).
ALGO_GROUPS = {
    "Trivance": {"B": "TrivanceB", "L": "TrivanceL"},
    "RecDoub":  {"B": "RecDoubB",  "L": "RecDoubL"},
    "Swing":    {"B": "SwingB",    "L": "SwingL"},
    "Bucket":   {"B": "Bucket",    "L": None},
    "Bruck":    {"B": "BruckB",    "L": "BruckL"},
}

# Colors/markers for papers
ALGO_STYLE = {
    "Trivance": {"color": "#1f77b4", "marker": "o"},  # baseline (not plotted as a line)
    "RecDoub":  {"color": "#2ca02c", "marker": "s"},
    "Swing":    {"color": "#d62728", "marker": "^"},
    "Bucket":   {"color": "#ff7f0e", "marker": "*"},
    "Bruck":    {"color": "#9467bd", "marker": "v"},
}

# Filename is "count"; message size in BYTES = count * 4
def msg_bytes_from_filename(fname: str) -> Optional[int]:
    base = os.path.splitext(fname)[0]
    if not re.fullmatch(r"\d+", base):
        return None
    return int(base) * 4  # bytes

TIME_RE = re.compile(r"simulated time:\s*([0-9]*\.?[0-9]+)\s*([a-zA-Zµ]+)", re.IGNORECASE)

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

def list_torus_topologies() -> list:
    if not os.path.isdir(INPUT_DIR):
        raise FileNotFoundError("Missing output folder: {}".format(OUTPUT_DIR))

    tops = []
    for name in os.listdir(INPUT_DIR):
        p = os.path.join(INPUT_DIR, name)
        if os.path.isdir(p) and name.startswith(TOPOLOGY_PREFIX):
            tops.append(name)
    return sorted(tops)

def read_variant_dir(variant_dir: Optional[str], missing_logged: set) -> Dict[int, float]:
    """
    Read a single variant directory: returns {msg_bytes: time_us}.
    Logs once if the directory is missing.
    """
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
    """
    Returns (xs, ys, first_B_x)
      - xs/ys: per message size take min(L,B) when both exist; otherwise take the available one
      - first_B_x: first x where B is strictly better than L (for hollow marker); None if never
    """
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

        # both exist
        if b < l and first_B_x is None:
            first_B_x = x
        xs.append(x)
        ys.append(min(l, b))

    return np.array(xs, dtype=np.int64), np.array(ys, dtype=float), first_B_x

def parse_nodes_from_torus_name(topology_name: str) -> Optional[int]:
    topo_size = topology_name[len(TOPOLOGY_PREFIX):]
    parts = topo_size.split("x")
    if not parts or any(not p.isdigit() for p in parts):
        return None
    n = 1
    for p in parts:
        n *= int(p)
    return n

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

def plot_topology(topology_name: str) -> None:
    topology_dir = os.path.join(INPUT_DIR, topology_name)
    os.makedirs(PDF_DIR, exist_ok=True)

    plt.figure(figsize=(10, 6))

    font_scale = 2.3
    base_fontsize = plt.rcParams.get("font.size", 10)
    fs = base_fontsize * font_scale

    missing_logged = set()

    # --- Baseline (Trivance) as best-of(L,B) ---
    triv = ALGO_GROUPS.get("Trivance")
    if triv is None:
        print("MISSING BASELINE CONFIG: Trivance")
        plt.close()
        return

    x_t, y_t, triv_switch_x = collect_algo_curve_best_of_LB(
        topology_dir, triv.get("L"), triv.get("B"), missing_logged
    )
    if x_t.size == 0:
        print("MISSING BASELINE DATA:", topology_name, "-> Trivance")
        plt.close()
        return

    base_map = curve_to_dict(x_t, y_t)

    any_plotted = False
    x_all = set(base_map.keys())

    # --- Plot other algos as percent diff to Trivance ---
    for algo, variants in ALGO_GROUPS.items():
        if algo == "Trivance":
            continue

        x_a, y_a, first_B_x = collect_algo_curve_best_of_LB(
            topology_dir, variants.get("L"), variants.get("B"), missing_logged
        )

        if x_a.size == 0:
            print("MISSING ALGORITHM DATA:", topology_name, "->", algo)
            continue

        algo_map = curve_to_dict(x_a, y_a)

        # Only compute where both algo and Trivance exist
        common_x = sorted(set(algo_map.keys()) & set(base_map.keys()))
        if not common_x:
            print("NO OVERLAP WITH BASELINE:", topology_name, "->", algo)
            continue

        x_plot = np.array(common_x, dtype=np.int64)
        base_vals = np.array([base_map[int(x)] for x in common_x], dtype=float)
        algo_vals = np.array([algo_map[int(x)] for x in common_x], dtype=float)

        # Percent difference relative to Trivance:
        # (algo - base) / base * 100
        y_pct = (algo_vals - base_vals) / base_vals * 100.0

        any_plotted = True
        x_all |= set(common_x)

        style = ALGO_STYLE.get(algo, {"color": "black", "marker": "o"})
        plt.plot(
            x_plot,
            y_pct,
            linestyle="-",
            linewidth=2.5,
            color=style["color"],
            marker=style["marker"],
            markersize=11 if algo != "Bucket" else 13,
            label=algo,
        )

        # Hollow marker: first x where B beats L (only if that x is in overlap range)
        if first_B_x is not None:
            fb = int(first_B_x)
            if fb in base_map and fb in algo_map:
                idx = np.where(x_plot == fb)[0]
                if idx.size:
                    i = int(idx[0])
                    plt.plot(
                        [x_plot[i]],
                        [y_pct[i]],
                        linestyle="None",
                        marker=style["marker"],
                        markersize=11 if algo != "Bucket" else 13,
                        markerfacecolor="white",
                        markeredgecolor=style["color"],
                        markeredgewidth=2,
                        zorder=10,
                    )

    if not any_plotted:
        print("NO PLOT GENERATED:", topology_name, "(no non-baseline algorithm had data)")
        plt.close()
        return

    # Axes formatting
    plt.xscale("log", base=2)
    plt.ylim(-40, 45)
    if triv_switch_x is not None:
        plt.axvline(triv_switch_x, color="gray", linewidth=2, linestyle=":")


    # X axis: every message size in the union (baseline + plotted algos)
    x_ref = np.array(sorted(x_all), dtype=np.int64)
    x_labels = [fmt_bytes(int(v)) for v in x_ref.tolist()]
    plt.xticks(x_ref, x_labels, rotation=45, fontsize=fs)

    # Y axis: show every second tick label
    ax = plt.gca()
    
    trans = mtransforms.blended_transform_factory(ax.transAxes, ax.transData)
    
    x_out = 1.02        # just outside right spine
    arrow_len = 50      # data units (percent)
    arrow_len_x = 40      # data units (percent)

    # GOOD arrow (pure vertical up)
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

    # WORSE arrow (pure vertical down)
    ax.annotate(
        "",
        xy=(x_out, -arrow_len_x),
        xytext=(x_out, 0),
        xycoords=trans,
        textcoords=trans,
        arrowprops=dict(arrowstyle="-|>,head_width=0.6,head_length=0.8", color="red", linewidth=5),
        clip_on=False,
    )

    ax.text(
        x_out + 0.015, -arrow_len_x * 0.5,
        r"\textsc{Trivance}" + "\n" + r"\textsc{worse}",
        transform=trans,
        va="center",
        ha="left",
        fontsize=fs,
        color="red",
    )

    yticks = ax.get_yticks()
    ax.set_yticks(yticks)
    ax.set_yticklabels(
        [("{:.0f}".format(t) if (i % 2 == 0) else "") for i, t in enumerate(yticks)],
        fontsize=fs,
    )

    plt.axhline(0, color="gray", linewidth=1, linestyle="--")

    plt.xlabel(r"AllReduce Size", fontsize=fs*1.2)
    plt.ylabel(
        r"Relative Completion" + "\n" + r"Time vs.\ \textsc{Trivance} (\%)",
        fontsize=fs*1.2,
    )

    topo_size = topology_name[len(TOPOLOGY_PREFIX):]
    nodes = parse_nodes_from_torus_name(topology_name)

    plt.grid(True, which="both", linestyle="--", linewidth=0.5)
    plt.legend(
        loc="lower left",
        bbox_to_anchor=(0.02, 0.02),
        fontsize=fs * 0.9,
        frameon=True,
        fancybox=True,
        framealpha=0.9,
        edgecolor="none",
        facecolor="white",
        handlelength=2.5,
        handletextpad=0.6,
        borderpad=0.6,
        labelspacing=0.4,
    )

    plt.tight_layout()

    pdf_path = os.path.join(PDF_DIR, "{}_rel_vs_trivance.pdf".format(topology_name))

    plt.savefig(pdf_path, bbox_inches="tight")
    plt.close()

    print("SAVED:", pdf_path)

def main() -> None:
    tops = list_torus_topologies()
    if not tops:
        raise RuntimeError("No torus_* folders found in {}".format(OUTPUT_DIR))

    for topo in tops:
        plot_topology(topo)

if __name__ == "__main__":
    main()
