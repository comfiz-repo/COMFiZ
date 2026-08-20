#!/bin/sh
# Runs each gadget in turn, one process each, counterparts adjacent, for every
# order that was built. Restrict either dimension:
#     ORDERS="1 3" ./run_gadgets.sh
#     ./run_gadgets.sh bench_secfxpexp bench_secapproxexp
cd "$(dirname "$0")/build" || exit 1
LD_LIBRARY_PATH=$PWD:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH

orders=${ORDERS:-$(ls bench_secfxpinv_d* 2>/dev/null | sed 's/.*_d//' | sort -n)}

echo "Masked Gadgets"
echo
for d in $orders; do
    for t in ${*:-bench_secfxpexp bench_secapproxexp \
                  bench_secfxpinv bench_secfprinvnr \
                  bench_secfxpberexp bench_secfprberexp}; do
        [ -x "./${t}_d${d}" ] && ./"${t}_d${d}"
    done
    echo
done
