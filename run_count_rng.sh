#!/usr/bin/env bash
# Counts the rand64() calls made inside each sampler and measures the share of
# its run time spent generating randomness. Every sampler is built twice: with
# ChaCha20, and with an insecure near-free generator (splitmix64) that only
# serves to time the rest of the computation. The input masking is excluded.
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
printf 'sampler,order,rng,samples,total_calls,mean_calls,min_calls,max_calls,mean_us\n' \
    > "$build/rand64_counts.csv"

build_sampler() {
    local src=$1 variant=$2 rng=$3 d=$4 file object
    local dir="$build/${variant}_${rng}_d$d"
    local -a flags=(-std=c99 -O3 -fno-lto -DCOUNT_RNG -DMASKORDER="$d" -I"$root/$src")
    if [[ $variant == comfiz ]]; then
        flags+=(-DSAMPLER_COMFIZ)
    else
        flags+=(-DSAMPLER_BPTBC24)
    fi
    if [[ $variant == bptbc24_nr ]]; then flags+=(-DBPTBC24_NR); fi
    if [[ $rng == fast ]]; then flags+=(-DINSECURE_FAST_RNG); fi
    mkdir -p "$dir" || return
    rm -f "$dir"/*.o "$dir/count_rng" || return
    for file in "$root/$src"/*.c; do
        object="$dir/$(basename "${file%.c}").o"
        "$cc" "${flags[@]}" -c "$file" -o "$object" || return
    done
    "$cc" "${flags[@]}" -c "$root/apps/count_rng.c" -o "$dir/count_rng.o" || return
    "$cc" -fno-lto "$dir"/*.o -lm -o "$dir/count_rng" || return
}

# Builds one sampler with one generator, runs it and prints its CSV row.
measure() {
    local src=$1 variant=$2 rng=$3 d=$4 row
    local log="$build/${variant}_${rng}_d$d.build.log"
    if ! build_sampler "$src" "$variant" "$rng" "$d" > "$log" 2>&1; then
        cat "$log" >&2
        exit 1
    fi
    row=$("$build/${variant}_${rng}_d$d/count_rng" "$runs" --csv)
    IFS=, read -r name order samples total mean minimum maximum mean_us <<< "$row"
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$name" "$order" "$rng" "$samples" "$total" \
        "$mean" "$minimum" "$maximum" "$mean_us" >> "$build/rand64_counts.csv"
    printf '%s\n' "$row"
}

printf 'rand64() calls and run time inside each sampler (%s completed samples each)\n' "$runs"
printf 'Input masking excluded; ratio = calls / COMFiZ calls.\n'
printf 'RNG share = 1 - (time with the insecure fast generator) / (time with ChaCha20).\n\n'
printf '%3s %-12s %14s %8s %16s %14s %10s\n' d Sampler Calls/sample Ratio \
    'ChaCha20 (us)' 'fast RNG (us)' 'RNG share'

for d in "${orders[@]}"; do
    comfiz_total=0
    for variant in comfiz bptbc24 bptbc24_nr; do
        src=BPTBC24
        if [[ $variant == comfiz ]]; then src=COMFiZ; fi
        row=$(measure "$src" "$variant" chacha "$d") || exit 1
        IFS=, read -r name _ _ total mean _ _ t_chacha <<< "$row"
        row=$(measure "$src" "$variant" fast "$d") || exit 1
        IFS=, read -r _ _ _ _ _ _ _ t_fast <<< "$row"
        if [[ $variant == comfiz ]]; then comfiz_total=$total; fi
        awk -v d="$d" -v n="$name" -v m="$mean" -v t="$total" -v c="$comfiz_total" \
            -v tc="$t_chacha" -v tf="$t_fast" 'BEGIN {
            printf "%3d %-12s %14.2f %7.2fx %16.2f %14.2f %9.1f%%\n",
                   d, n, m, t / c, tc, tf, 100 * (1 - tf / tc)
        }'
    done
done

printf '\nExact totals, min/max calls per sample and mean times: %s/rand64_counts.csv\n' "$build"
