#!/usr/bin/env bash
# Counts the rand64() calls made inside each sampler. The input masking is excluded.
#     bash run_count_rng.sh [samples]            (default 1000 samples)
#     ORDERS="1 3" bash run_count_rng.sh 500
set -euo pipefail
export LC_ALL=C

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=${PROJECT_ROOT:-$script_dir}
build="$root/build_count_rng"
cc=${CC:-gcc}
runs=${1:-1000}
order_text=${ORDERS:-1 2 3 4 5}
read -r -a orders <<< "${order_text//$'\n'/ }"

if [[ $# -gt 1 || ! $runs =~ ^[1-9][0-9]*$ ]]; then
    printf 'Usage: bash %s [positive sample count]\n' "$0" >&2
    exit 1
fi
for d in "${orders[@]}"; do
    if [[ ! $d =~ ^[1-9][0-9]*$ ]]; then
        printf 'Invalid masking order: %s\n' "$d" >&2
        exit 1
    fi
done
for src in COMFiZ BPTBC24; do
    if [[ ! -f "$root/$src/utils.c" ]]; then
        printf 'Missing %s/utils.c. Run this script from the project root.\n' "$src" >&2
        exit 1
    fi
done
mkdir -p "$build"
printf 'sampler,order,samples,total_calls,mean_calls,min_calls,max_calls\n' \
    > "$build/rand64_counts.csv"

build_sampler() {
    local src=$1 variant=$2 d=$3 file object
    local dir="$build/${variant}_d$d"
    local -a flags=(-std=c99 -O3 -fno-lto -DCOUNT_RNG -DMASKORDER="$d" -I"$root/$src")
    if [[ $variant == comfiz ]]; then
        flags+=(-DSAMPLER_COMFIZ)
    else
        flags+=(-DSAMPLER_BPTBC24)
    fi
    if [[ $variant == bptbc24_nr ]]; then flags+=(-DBPTBC24_NR); fi
    mkdir -p "$dir" || return
    rm -f "$dir"/*.o "$dir/count_rng" || return
    for file in "$root/$src"/*.c; do
        object="$dir/$(basename "${file%.c}").o"
        "$cc" "${flags[@]}" -c "$file" -o "$object" || return
    done
    "$cc" "${flags[@]}" -c "$root/apps/count_rng.c" -o "$dir/count_rng.o" || return
    "$cc" -fno-lto "$dir"/*.o -lm -o "$dir/count_rng" || return
}

# Builds one sampler, runs it and prints its CSV row.
measure() {
    local src=$1 variant=$2 d=$3 row
    local log="$build/${variant}_d$d.build.log"
    if ! build_sampler "$src" "$variant" "$d" > "$log" 2>&1; then
        cat "$log" >&2
        exit 1
    fi
    row=$("$build/${variant}_d$d/count_rng" "$runs" --csv)
    printf '%s\n' "$row" >> "$build/rand64_counts.csv"
    printf '%s\n' "$row"
}

printf 'rand64() calls inside each sampler (%s completed samples each)\n' "$runs"
printf 'Input masking excluded; ratio = calls / COMFiZ calls.\n\n'
printf '%3s %-12s %14s %8s\n' d Sampler Calls/sample Ratio

for d in "${orders[@]}"; do
    comfiz_total=0
    for variant in comfiz bptbc24 bptbc24_nr; do
        src=BPTBC24
        if [[ $variant == comfiz ]]; then src=COMFiZ; fi
        row=$(measure "$src" "$variant" "$d") || exit 1
        IFS=, read -r name _ _ total mean _ _ <<< "$row"
        if [[ $variant == comfiz ]]; then comfiz_total=$total; fi
        awk -v d="$d" -v n="$name" -v m="$mean" -v t="$total" -v c="$comfiz_total" 'BEGIN {
            printf "%3d %-12s %14.2f %7.2fx\n", d, n, m, t / c
        }'
    done
done

printf '\nExact totals and min/max calls per sample: %s/rand64_counts.csv\n' "$build"
