# COMFiZ

Artifact for *COMFiZ: a masked fixed-point Gaussian sampler for Falcon*. It contains the COMFiZ implementation, the BPTBC24 implementation it is compared against, the benchmark drivers, and the scripts that certify the error analysis.

## Layout

| File | Contents |
| --- | --- |
| `COMFiZ/COMFiZ.c` | Algorithm 7, the masked fixed-point sampler: center decomposition, reciprocal, rejection loop |
| `COMFiZ/COMFiZ.h` | Declaration of `COMFiZ` |
| `COMFiZ/COMFiZ_gadgets.c` | Algorithms 1–6 and 8: CORDIC exponential and reciprocal, BerExp, the Boolean gadgets, the base sampler, and the public tables |
| `COMFiZ/COMFiZ_gadgets.h` | Declarations of the above and the constant `SIGMA_MIN_OVER_K_Q62` |
| `COMFiZ/gadgets.c` | Masking primitives: `SecAnd`, `SecAdd`, `SecOr`, the refreshes, and the mask conversions |
| `COMFiZ/gadgets.h` | Masked types, `MASKORDER` and `MASKSIZE` |
| `COMFiZ/utils.c` | The pseudorandom generator `rand64`, the 128-bit helpers, and the median |
| `COMFiZ/utils.h` | Declarations of the above and the trial count `ITER` |
| `BPTBC24/secfpr.c` | BPTBC24's floating-point `SamplerZ`, `SecApproxExp`, `SecFprBerExp`, and both reciprocals |
| `BPTBC24/secfpr.h` | Declarations of the above |
| `BPTBC24/fpr_gadgets.c` | Masked binary64 arithmetic: multiplication, addition, normalisation, and the shifts they need |
| `BPTBC24/fpr_gadgets.h` | Declarations of the above |
| `BPTBC24/fpr_modify.c` | The variants of those gadgets used by the division path |
| `BPTBC24/fpr_modify.h` | Declarations of the above |
| `BPTBC24/gadgets.c`, `gadgets.h`, `utils.c`, `utils.h` | The same primitives as in `COMFiZ/`, kept alongside so each tree builds on its own |
| `apps/main.c` | Whole-sampler driver; a `SAMPLER_` directive selects which sampler the binary times |
| `apps/bench_gadgets_main.c` | Gadget driver; a `BENCH_` directive selects which routine the binary times |
| `verify/comfiz_verify.sage` | Certification of the instantiation: constant rounding, the bounds of Section 4, the conditions of Corollary 2, and a simulation checked against them |
| `CMakeLists.txt` | Builds one shared library per implementation per masking order, and one executable per routine |
| `run_samplers.sh` | Runs each sampler, one process each, at every order built |
| `run_gadgets.sh` | Runs each gadget, one process each, counterparts adjacent, at every order built |

## Building and running

Requires CMake 3.13 or later and a C99 compiler.

```
chmod +x run_samplers.sh run_gadgets.sh
cmake -B build
cmake --build build -j
./run_samplers.sh
./run_gadgets.sh
```

`chmod` needs `sudo` only if the checkout is not owned by you.

Orders 1 to 5 are built by default, each as its own library and set of executables, since `MASKSIZE` is fixed at compile time. Both scripts detect which orders are present and run them all.

```
cmake -B build -DMASKORDERS="1;3;5"        # build a subset
ORDERS="1 3" ./run_gadgets.sh              # run a subset
./run_samplers.sh comfiz bptbc24_nr        # run named targets only
```

Each line reports the routine, its median, the order, and the trial count:

```
  COMFiZ             33.46us   (order 1, median of 10000 runs)
```

The trial count is `ITER` in `utils.h`. At the default of 10000 a full run over five orders takes several minutes, dominated by BPTBC24 at the higher orders. `ITER` = 100000 was used to get the timings given in the paper.

Three samplers are built. `comfiz` is Algorithm 7; `bptbc24` is BPTBC24's sampler; `bptbc24_nr` is the same sampler with the Newton–Raphson reciprocal of the follow-up work, selected by the `BPTBC24_NR` definition inside `SamplerZ`. Six gadgets are built, in counterpart pairs: `SecFxpInv` against `SecFprInvNR`, `SecFxpExp` against `SecApproxExp`, and `SecFxpBerExp` against `SecFprBerExp`.

Every binary times exactly one routine, so no measurement is affected by another running before it in the same process. Both implementations are compiled the same way, as shared libraries, so neither is favoured by the indirection a shared object adds on every gadget call.

## Verification
The SageMath script checks that every stored constant is correctly rounded, evaluates the bounds of Section 4 and the two conditions of Corollary 2, then simulates the pipeline and compares the observed errors with those bounds. It takes about a minute and every line should read `True` or `yes`. The checks are done with the fixed-point quantities exact over `QQ` and the transcendental ones in `RealField(200)`. Start Sage in the `verify` directory and load it:

```
cd verify
sage
```
```
sage: load("comfiz_verify.sage")
```
