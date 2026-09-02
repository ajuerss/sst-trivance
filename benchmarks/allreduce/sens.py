import os
import re
import argparse
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
OUTPUT_DIR = os.path.join(BASE_DIR, "output/plots/sens")

ALGO_GROUPS = {
    "Trivance": {"B": "TrivanceB", "L": "TrivanceL"},
    "RecDoub":  {"B": "RecDoubB",  "L": "RecDoubL"},
    "Swing":    {"B": "SwingB",    "L": "SwingL"},
    "Bucket":   {"B": "Bucket",    "L": None},
    "Bruck":    {"B": "BruckB",    "L": "BruckL"},
}

MODE_CONFIG = {
    "bw": {
        "legend_title": "Bandwidth",
        "unit": "Gb/s",
        "out_suffix": "ALL_BW",
    },
    "packet": {
        "legend_title": "Packet size",
        "unit": "B",
        "out_suffix": "ALL_PACKET",
    },
    "flit": {
        "legend_title": "Flit size",
        "unit": "B",
        "out_suffix": "ALL_FLIT",
    },
    "nic": {
        "legend_title": "Node injection bandwidth",
        "unit": "Gb/s",
        "out_suffix": "ALL_NIC",
    },
    "linklat": {
        "legend_title": "Link latency",
        "unit": "ns",
        "out_suffix": "ALL_LINKLAT",
    },
    "step": {
        "legend_title": "Per-step latency",
        "unit": "ns",
        "out_suffix": "ALL_STEP",
    },
}

TIME_RE = re.compile(r"simulated time:\s*([0-9]*\.?[0-9]+)\s*([a-zA-Zµ]+)", re.IGNORECASE)


def msg_bytes_from_filename(fname: str) -> Optional[int]:
    base = os.path.splitext(fname)[0]
    if not re.fullmatch(r"\d+", base):
        return None
    return int(base) * 4


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
    raise ValueError(f"Unsupported time unit '{unit}'")


def read_sim_time_us(filepath: str) -> float:
    with open(filepath, "r", errors="ignore") as f:
        txt = f.read()
    m = TIME_RE.search(txt)
    if not m:
        raise ValueError(f"No 'simulated time' found in {filepath}")
    return to_microseconds(float(m.group(1)), m.group(2))


def fmt_bytes(b: int) -> str:
    if b < 1024:
        return f"{b} B"
    if b < 1024**2:
        return f"{b // 1024} KiB"
    if b < 1024**3:
        return f"{b // (1024**2)} MiB"
    return f"{b // (1024**3)} GiB"


def numeric_key(label: str) -> Tuple[int, str]:
    m = re.match(r"^\s*(\d+)", label)
    return (int(m.group(1)) if m else 10**18, label)


def latex_escape_text(text: object) -> str:
    """Escape dynamic text labels so they are safe with text.usetex=True."""
    replacements = {
        "&": r"\&",
        "%": r"\%",
        "$": r"\$",
        "#": r"\#",
        "_": r"\_",
        "{": r"\{",
        "}": r"\}",
    }
    return "".join(replacements.get(ch, ch) for ch in str(text))


def parameter_label_from_folder(folder_name: str, target_prefix: str, mode: str) -> str:
    raw = folder_name[len(target_prefix):] if folder_name.startswith(target_prefix) else folder_name
    raw = raw.strip("_")

    unit = MODE_CONFIG[mode]["unit"]

    if mode == "bw":
        raw = raw.replace("Gb", "").replace("Gbps", "").replace("Gb/s", "")
    elif mode == "nic":
        raw = raw.replace("Gb", "").replace("Gbps", "").replace("Gb/s", "")
    elif mode in ("packet", "flit"):
        raw = raw.replace("B", "")
    elif mode in ("linklat", "step"):
        raw = raw.replace("ns", "")

    return f"{raw}{unit}"


def list_target_topologies(input_dir: str, target_prefix: str) -> List[str]:
    if not os.path.isdir(input_dir):
        raise FileNotFoundError(f"Missing input folder: {input_dir}")

    tops = []
    for name in os.listdir(input_dir):
        p = os.path.join(input_dir, name)
        if os.path.isdir(p) and name.startswith(target_prefix):
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

    result: Dict[int, float] = {}

    for fname in os.listdir(variant_dir):
        mbytes = msg_bytes_from_filename(fname)
        if mbytes is None:
            continue

        fpath = os.path.join(variant_dir, fname)
        if not os.path.isfile(fpath):
            continue

        try:
            result[mbytes] = read_sim_time_us(fpath)
        except Exception:
            continue

    return result


def collect_algo_curve_best_of_LB(
    topology_dir: str,
    variant_L: Optional[str],
    variant_B: Optional[str],
    missing_logged: set,
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
        l = L_map.get(x)
        b = B_map.get(x)

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


def curve_to_dict(xs: np.ndarray, ys: np.ndarray) -> Dict[int, float]:
    return {int(x): float(y) for x, y in zip(xs.tolist(), ys.tolist())}


def best_nonbaseline_vs_trivance_pct(
    topology_dir: str,
    missing_logged: set,
) -> Tuple[np.ndarray, np.ndarray]:

    triv = ALGO_GROUPS["Trivance"]
    x_t, y_t, _ = collect_algo_curve_best_of_LB(
        topology_dir,
        triv.get("L"),
        triv.get("B"),
        missing_logged,
    )

    if x_t.size == 0:
        return np.array([], dtype=np.int64), np.array([], dtype=float)

    triv_map = curve_to_dict(x_t, y_t)

    algo_maps: Dict[str, Dict[int, float]] = {}

    for algo, variants in ALGO_GROUPS.items():
        if algo == "Trivance":
            continue

        x_a, y_a, _ = collect_algo_curve_best_of_LB(
            topology_dir,
            variants.get("L"),
            variants.get("B"),
            missing_logged,
        )

        if x_a.size == 0:
            continue

        algo_maps[algo] = curve_to_dict(x_a, y_a)

    if not algo_maps:
        return np.array([], dtype=np.int64), np.array([], dtype=float)

    x_out: List[int] = []
    y_out: List[float] = []

    for x in sorted(triv_map.keys()):
        triv_time = triv_map.get(x)
        if triv_time is None:
            continue

        best_other = None
        for amap in algo_maps.values():
            t = amap.get(x)
            if t is None:
                continue
            if best_other is None or t < best_other:
                best_other = t

        if best_other is None:
            continue

        pct = (best_other - triv_time) / triv_time * 100.0
        x_out.append(x)
        y_out.append(pct)

    return np.array(x_out, dtype=np.int64), np.array(y_out, dtype=float)


def plot_all_parameters_one_figure(
    input_dir: str,
    output_dir: str,
    topology_shape: str,
    mode: str,
    topology_prefix: str = "torus_",
) -> None:

    if mode not in MODE_CONFIG:
        raise ValueError(f"Unknown mode '{mode}'. Valid modes: {sorted(MODE_CONFIG.keys())}")

    target_prefix = f"{topology_prefix}{topology_shape}_"

    tops = list_target_topologies(input_dir, target_prefix)

    if not tops:
        raise RuntimeError(f"No {target_prefix}* folders found in {input_dir}")

    os.makedirs(output_dir, exist_ok=True)

    plt.figure(figsize=(10, 6))

    font_scale = 2.3
    base_fontsize = plt.rcParams.get("font.size", 10)
    fs = base_fontsize * font_scale

    missing_logged = set()
    any_plotted = False
    x_all = set()

    tops_sorted = sorted(
        tops,
        key=lambda name: numeric_key(parameter_label_from_folder(name, target_prefix, mode)),
    )

    for topo in tops_sorted:
        topology_dir = os.path.join(input_dir, topo)
        param_label = parameter_label_from_folder(topo, target_prefix, mode)

        x_vals, y_vals = best_nonbaseline_vs_trivance_pct(topology_dir, missing_logged)

        if x_vals.size == 0:
            print("SKIP (missing baseline or no non-baseline overlap):", topo)
            continue

        any_plotted = True
        x_all |= set(x_vals.tolist())

        plt.plot(
            x_vals,
            y_vals,
            linestyle="-",
            linewidth=2.3,
            marker="o",
            markersize=10,
            label=latex_escape_text(param_label),
        )

    if not any_plotted:
        raise RuntimeError("No plots generated.")

    plt.xscale("log", base=2)
    plt.ylim(-20, 25)

    x_ref = np.array(sorted(x_all), dtype=np.int64)
    x_labels = [latex_escape_text(fmt_bytes(int(v))) for v in x_ref.tolist()]
    plt.xticks(x_ref, x_labels, rotation=45, fontsize=fs)

    ax = plt.gca()
    yticks = ax.get_yticks()
    ax.set_yticks(yticks)
    ax.set_yticklabels(
        [f"{t:.0f}" if i % 2 == 0 else "" for i, t in enumerate(yticks)],
        fontsize=fs,
    )

    plt.axhline(0, color="gray", linewidth=1, linestyle="--")

    plt.xlabel(r"AllReduce Size", fontsize=fs*1.2)
    plt.ylabel(
        r"Relative Completion" + "\n" + r"Time vs.\ \textsc{Trivance} (\%)",
        fontsize=fs*1.2,
    )

    plt.grid(True, which="both", linestyle="--", linewidth=0.5)

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
        arrowprops=dict(
            arrowstyle="-|>,head_width=0.6,head_length=0.8",
            color="green",
            linewidth=5,
        ),
        clip_on=False,
    )

    ax.text(
        x_out + 0.015,
        arrow_len * 0.5,
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
        arrowprops=dict(
            arrowstyle="-|>,head_width=0.6,head_length=0.8",
            color="red",
            linewidth=5,
        ),
        clip_on=False,
    )

    ax.text(
        x_out + 0.015,
        -arrow_len * 0.5 + 4.5,
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
        title=latex_escape_text(MODE_CONFIG[mode]["legend_title"]),
        title_fontsize=fs * 0.65,
    )

    plt.tight_layout()

    out_base = f"{topology_prefix}{topology_shape}_{MODE_CONFIG[mode]['out_suffix']}_best_vs_trivance"
    pdf_path = os.path.join(output_dir, out_base + ".pdf")

    plt.savefig(pdf_path, bbox_inches="tight")
    plt.close()

    print("SAVED:", pdf_path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--shape",
        type=str,
        default="32x32",
        help='Torus shape, e.g., "8x8", "32x32", or "16x16x16".',
    )
    parser.add_argument(
        "--output_dir",
        type=str,
        default=os.path.join(BASE_DIR, "output/pktsize"),
        help="Large folder containing torus_* result subfolders.",
    )
    parser.add_argument(
        "--topology_prefix",
        type=str,
        default="torus_",
        help='Topology prefix, default "torus_".',
    )

    args = parser.parse_args()

    plot_all_parameters_one_figure(
        input_dir=INPUT_DIR,
        output_dir=OUTPUT_DIR,
        topology_shape=args.shape,
        mode=args.mode,
        topology_prefix=args.topology_prefix,
    )


if __name__ == "__main__":
    main()
