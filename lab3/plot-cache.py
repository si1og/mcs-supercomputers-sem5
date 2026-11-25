import argparse
import csv
import os
import sys
from collections import defaultdict
from datetime import datetime
from fcntl import LOCK_EX, flock
from io import StringIO
from itertools import count
from pathlib import Path
from tempfile import TemporaryDirectory

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.ticker import MultipleLocator

plt.rcParams["pdf.fonttype"] = 42
SCRIPT_MTIME = Path(__file__).stat().st_mtime_ns


def plot(title, x_label, series, *, log_scale=False, ticks=None):
    fig, ax = plt.subplots(figsize=(8, 4.5), layout="constrained")
    for label, points in series:
        x, latency = zip(*sorted(points))
        ax.plot(x, latency, ".-", label=label)

    if log_scale:
        ax.set_xscale("log", base=2)
    if ticks is not None:
        ax.set_xticks(*ticks)
    if len(series) > 1:
        ax.xaxis.set_major_locator(MultipleLocator(1))
        ax.legend()
    ax.set(title=title, xlabel=x_label, ylabel="Медиана задержки чтения, нс")
    ax.grid(alpha=0.3)
    return fig


def plot_page_probe(samples):
    offsets, times = zip(*sorted((x / 1024, latency / 1000) for x, latency in samples))
    # measure_page_bytes сохраняет замеры первых восьми страниц найденного размера.
    points_per_page = len(offsets) // 8
    starts = offsets[::points_per_page]
    start_times = times[::points_per_page]
    page_kib = starts[1] - starts[0]

    fig, ax = plt.subplots(figsize=(8, 4.5), layout="constrained")
    accent = "#bd4b21"
    ax.scatter(offsets, times, s=8, color="#52677b", alpha=0.4, label="Все записи")
    ax.vlines(starts, 0, 1, transform=ax.get_xaxis_transform(), colors=accent,
              linestyles="dashed", linewidth=0.8, alpha=0.38, zorder=1)
    ax.scatter(starts, start_times, s=27, color=accent, label="Начало страницы", zorder=3)

    # Стрелка отмечает интервал между двумя соседними границами.
    left, right = starts[1:3]
    coords = ax.get_xaxis_transform()
    ax.annotate("", xy=(right, 0.84), xytext=(left, 0.84), xycoords=coords,
                arrowprops={"arrowstyle": "<->", "color": accent})
    ax.text((left + right) / 2, 0.87, f"{page_kib:g} КиБ", transform=coords,
            ha="center", va="bottom", color=accent)

    ax.set(title=f"Размер страницы по замеру ({page_kib:g} КиБ)",
           xlabel="Смещение от начала участка, КиБ", ylabel="Медиана времени записи, мкс")
    ax.set_xticks(starts)
    ax.grid(axis="y", alpha=0.3)
    ax.legend(loc="upper right")
    return fig


def save_csv(output, csv_data, measured_at):
    stem = f"results_{measured_at:%Y-%m-%d_%H-%M-%S}"
    with TemporaryDirectory(dir=output) as directory:
        ready = Path(directory) / "results.csv"
        ready.write_text(csv_data, encoding="utf-8")
        for number in count(1):
            suffix = "" if number == 1 else f"_{number}"
            path = output / f"{stem}{suffix}.csv"
            try:
                os.link(ready, path)
            except FileExistsError:
                continue
            return path


def read_csv(csv_data):
    reader = csv.DictReader(StringIO(csv_data))
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
    return data


def figures(data):
    if page_samples := data.get("page_first_write_bytes"):
        yield "page_size.png", plot_page_probe(page_samples)

    strides = sorted(x for x, _ in data["line_stride_bytes"])
    yield "line_stride.png", plot(
        "Задержка и шаг чтения", "Шаг чтения, байт",
        [(None, data["line_stride_bytes"])], log_scale=True,
        ticks=(strides, [str(x) for x in strides]),
    )

    sizes_kib = [(x / 1024, latency) for x, latency in data["working_set_bytes"]]
    yield "working_set.png", plot(
        "Задержка и размер рабочего набора", "Размер рабочего набора, КиБ",
        [(None, sizes_kib)], log_scale=True,
    )

    yield "l1_associativity.png", plot(
        "Ассоциативность L1", "Число кэш-линий", [
            ("Один набор L1", data["l1_conflict_lines"]),
            ("Разные наборы L1", data["l1_control_lines"]),
        ],
    )


def refresh_report(output):
    report = output / "cache.pdf"
    with (output / ".cache-pdf.lock").open("a") as lock:
        flock(lock, LOCK_EX)
        sources = [(path.stat().st_mtime_ns, path) for path in output.glob("results*.csv")]
        if not sources:
            return
        source_mtime, source = max(sources)
        version = max(source_mtime, SCRIPT_MTIME)
        if report.exists() and report.stat().st_mtime_ns >= version:
            return

        data = read_csv(source.read_text(encoding="utf-8"))
        with TemporaryDirectory(dir=output) as directory:
            temp = Path(directory)
            pdf_path = temp / report.name
            filenames = []
            with PdfPages(pdf_path, metadata={"Title": "Замеры кэша", "Subject": source.name}) as pdf:
                for name, fig in figures(data):
                    pdf.savefig(fig)
                    fig.savefig(temp / name, dpi=180)
                    plt.close(fig)
                    filenames.append(name)
            for name in filenames:
                (temp / name).replace(output / name)
                print(output / name)
            os.utime(pdf_path, ns=(version, version))
            pdf_path.replace(report)
        print(f"{report} ← {source.name}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--refresh", action="store_true", help="Перестроить графики из последнего CSV")
    args = parser.parse_args()

    output = Path("plots")
    output.mkdir(exist_ok=True)
    if not args.refresh:
        started_at = os.environ.get("CACHE_STARTED_AT")
        measured_at = datetime.fromtimestamp(int(started_at)) if started_at else datetime.now()
        csv_data = sys.stdin.read()
        read_csv(csv_data)
        print(save_csv(output, csv_data, measured_at))

    refresh_report(output)


if __name__ == "__main__":
    main()
