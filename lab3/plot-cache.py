import csv
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator


def plot(output, x_label, series, *, log_scale=False, ticks=None):
    fig, ax = plt.subplots(figsize=(8, 4.5), layout="constrained")
    for label, points in series:
        x, latency = zip(*sorted(points))
        ax.plot(x, latency, ".-", label=label)

    if log_scale:
        ax.set_xscale("log", base=2)
    if ticks is not None:
        ax.set_xticks(*ticks)
    if len(series) > 1:
        ax.xaxis.set_major_locator(MultipleLocator(2))
        ax.legend()
    ax.set(xlabel=x_label, ylabel="Медианная задержка чтения, нс")
    ax.grid(alpha=0.3)
    fig.savefig(output, dpi=180)
    plt.close(fig)
    print(output)


def main():
    reader = csv.DictReader(sys.stdin)
    if not {"test", "x", "median_ns"}.issubset(reader.fieldnames or ()):
        sys.exit("В stdin ожидается CSV из cache.c с полями test, x и median_ns.")

    data = defaultdict(list)
    try:
        for row in reader:
            data[row["test"]].append((int(row["x"]), float(row["median_ns"])))
    except (TypeError, ValueError):
        sys.exit(f"Некорректное измерение в строке {reader.line_num} CSV.")

    required = ("line_stride_bytes", "working_set_bytes", "l1_conflict_lines", "l1_control_lines")
    missing = [test for test in required if not data[test]]
    if missing:
        sys.exit(f"В CSV нет результатов для {', '.join(missing)}.")

    output = Path("plots")
    output.mkdir(exist_ok=True)
    strides = sorted(x for x, _ in data["line_stride_bytes"])
    plot(output / "line_stride.png", "Шаг чтения, байт",
         [(None, data["line_stride_bytes"])], log_scale=True,
         ticks=(strides, [str(x) for x in strides]))

    sizes_kib = [(x / 1024, latency) for x, latency in data["working_set_bytes"]]
    plot(output / "working_set.png", "Размер рабочего набора, КиБ",
         [(None, sizes_kib)], log_scale=True)

    plot(output / "l1_associativity.png", "Число кэш-линий", [
        ("Один набор L1", data["l1_conflict_lines"]),
        ("Разные наборы L1", data["l1_control_lines"]),
    ])


if __name__ == "__main__":
    main()
