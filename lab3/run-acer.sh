#!/usr/bin/env bash
set -euo pipefail

if (( $# > 1 )); then
    echo "Использование: $0 [16|32|64|128]" >&2
    exit 2
fi

line_bytes=${1:-64}
case "$line_bytes" in
    16|32|64|128) ;;
    *) echo "Размер кэш-линии должен быть 16, 32, 64 или 128 байт." >&2; exit 2 ;;
esac

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
conda=${CONDA_EXE:-conda}
if ! command -v "$conda" >/dev/null && [[ -x "$HOME/miniconda3/bin/conda" ]]; then
    conda="$HOME/miniconda3/bin/conda"
fi

ssh -T acer '
    set -eu
    work_dir=$(mktemp -d)
    trap '\''rm -rf -- "$work_dir"'\'' EXIT
    cc -std=c11 -O2 -x c -o "$work_dir/cache" -
    "$work_dir/cache"' "$line_bytes" < "$script_dir/cache.c" |
    "$conda" run --no-capture-output -n lab3-cache python "$script_dir/plot-cache.py"
