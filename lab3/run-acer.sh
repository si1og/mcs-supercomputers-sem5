#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
conda=${CONDA_EXE:-conda}
if ! command -v "$conda" >/dev/null && [[ -x "$HOME/miniconda3/bin/conda" ]]; then
    conda="$HOME/miniconda3/bin/conda"
fi

if [[ ${1:-} == --watch ]] && (( $# == 1 )); then
    exec "$conda" run --no-capture-output -n lab3-cache python "$script_dir/watch-cache.py"
fi

if (( $# > 0 )); then
    echo "Использование: $0 [--watch]" >&2
    exit 2
fi

started_at=$(date +%s)
COPYFILE_DISABLE=1 tar -cf - -C "$script_dir" cache.c measurements/benchmark.h \
    measurements/{benchmark,page_size,line_stride,working_set,l1_associativity}.c |
    ssh -T -o ConnectTimeout=120 acer '
    set -eu
    work_dir=$(mktemp -d)
    trap '\''rm -rf -- "$work_dir"'\'' EXIT
    cd "$work_dir"
    tar -xf -
    cc -std=c11 -O2 -o cache cache.c measurements/*.c
    ./cache' |
    CACHE_STARTED_AT="$started_at" \
    "$conda" run --no-capture-output -n lab3-cache python "$script_dir/plot-cache.py"
