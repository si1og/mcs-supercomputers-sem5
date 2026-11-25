import subprocess
import sys
from pathlib import Path
from time import sleep


def watch():
    plot_script = Path(__file__).with_name("plot-cache.py")
    previous = None
    print("Слежу за CSV в plots/ и за plot-cache.py. Ctrl+C — остановить.", flush=True)
    while True:
        paths = [plot_script, *Path("plots").glob("results*.csv")]
        try:
            current = {path: path.stat().st_mtime_ns for path in paths}
        except FileNotFoundError:
            sleep(1)
            continue
        if len(current) > 1 and current != previous:
            subprocess.run([sys.executable, str(plot_script), "--refresh"], check=False)
        previous = current
        sleep(1)


if __name__ == "__main__":
    try:
        watch()
    except KeyboardInterrupt:
        pass
