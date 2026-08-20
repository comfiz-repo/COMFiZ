#!/bin/sh
# Runs each whole sampler in turn, one process each, for every order that was
# built. Restrict either dimension:
#     ORDERS="1 3" ./run_samplers.sh
#     ./run_samplers.sh comfiz bptbc24_nr
cd "$(dirname "$0")/build" || exit 1
LD_LIBRARY_PATH=$PWD:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH

orders=${ORDERS:-$(ls comfiz_d* 2>/dev/null | sed 's/.*_d//' | sort -n)}

echo "Masked Falcon SamplerZ"
echo
for d in $orders; do
    for t in ${*:-comfiz bptbc24 bptbc24_nr}; do
        [ -x "./${t}_d${d}" ] && ./"${t}_d${d}"
    done
    echo
done
