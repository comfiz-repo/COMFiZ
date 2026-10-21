#!/bin/sh
# Times SecFxpExp (CORDIC) against Falcon's fixed-point polynomial ApproxExp,
# masked with the 2^d B2A of Coron (CHES 2017) and with the d^2 B2A of Coron,
# Grosschadl and Vadnala (CHES 2014), for every order that was built. All three
# run in the same session, so their ratios are directly comparable.
# Restrict the orders with: ORDERS="1 2 3" ./compare_exp.sh
cd "$(dirname "$0")/build" || exit 1
LD_LIBRARY_PATH=$PWD:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH

orders=${ORDERS:-$(ls bench_secfxpexp_d* 2>/dev/null | sed 's/.*_d//' | sort -n)}

time_of() {
    ./"$1" | awk '{ sub("us", "", $2); print $2 }'
}

# "CORDIC 2.10x slower" or "poly 1.17x slower"
verdict() {
    awk -v c="$1" -v p="$2" 'BEGIN {
        if (c >= p) printf "CORDIC %.2fx slower", c / p
        else        printf "poly %.2fx slower", p / c
    }'
}

printf "%5s %12s %16s %16s   %-22s %s\n" order "CORDIC (us)" "poly 2^d (us)" "poly d^2 (us)" \
       "vs poly, 2^d B2A" "vs poly, d^2 B2A"
for d in $orders; do
    c=$(time_of bench_secfxpexp_d"$d")
    p=$(time_of bench_secfxppolyexp_d"$d")
    q=$(time_of bench_secfxppolyexpq_d"$d")
    printf "%5d %12.2f %16.2f %16.2f   %-22s %s\n" "$d" "$c" "$p" "$q" \
           "$(verdict "$c" "$p")" "$(verdict "$c" "$q")"
done
