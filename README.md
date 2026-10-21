# COMFiZ

Artifact for **COMFiZ: CORDIC-based Masked Fixed-point SamplerZ**. It contains the COMFiZ implementation, the BPTBC24 implementation it is compared against, the benchmark drivers, correctness tests, and the scripts that certify the error analysis.

## Changes since the submitted version

- **Masked BaseSampler.** The uniform value and the sign are sampled share by share, the 18 RCDT comparisons run on Boolean shares in bitsliced form, and both `z` and the correction term `C(z0)` leave the sampler masked. The correction is subtracted share by share inside the trial. BerExp's three uniform coins are also sampled share by share. The acceptance bit is the only unmasked value in a trial.
- **Cryptographic RNG.** Every random word in both implementations comes from ChaCha20, the PRNG of Falcon's reference implementation, seeded from `/dev/urandom`. All timings include its cost.
- **Faster CORDIC exponential.** `SecFxpExp` uses 85 `SecAdd` calls instead of 126: from index 21 on, the angles are powers of two, so the rotation directions are read from the bits of the residual angle. Its output is bit-identical to the previous version.
- **Polynomial baselines.** Falcon's fixed-point `ApproxExp`, masked with `SecMult128` and exact A2B/B2A truncations, with the B2A of Coron (CHES 2017) or of Coron, Großschädl and Vadnala (CHES 2014).
- **New scripts.** `compare_exp.sh` compares CORDIC with both baselines, and `run_count_rng.sh` counts the random words each sampler draws.

## Layout

| File | Contents |
| --- | --- |
| `COMFiZ/COMFiZ.c` | Algorithm 7, the masked fixed-point sampler: center decomposition, reciprocal, rejection loop |
| `COMFiZ/COMFiZ.h` | Declaration of `COMFiZ` |
| `COMFiZ/COMFiZ_gadgets.c` | Algorithms 1–6 and 8: CORDIC exponential and reciprocal, BerExp, the Boolean gadgets, the masked base sampler and its public tables; the two polynomial baselines `SecFxpPolyExp` and `SecFxpPolyExpQuad` |
| `COMFiZ/COMFiZ_gadgets.h` | Declarations of the above and the constant `SIGMA_MIN_OVER_K_Q62` |
| `COMFiZ/gadgets.c` | Masking primitives: `SecAnd`, `SecAdd`, `SecOr`, the refreshes, and the mask conversions, including `B2A128` (CHES 2017) and `B2A128Quad` (CHES 2014) |
| `COMFiZ/gadgets.h` | Masked types, `MASKORDER` and `MASKSIZE` |
| `COMFiZ/utils.c` | The ChaCha20 generator `rand64`, the 128-bit helpers built from 32×32→64 products, and the median |
| `COMFiZ/utils.h` | Declarations of the above and the trial count `ITER` |
| `BPTBC24/secfpr.c` | BPTBC24's floating-point `SamplerZ`, `SecApproxExp`, `SecFprBerExp`, and both reciprocals |
| `BPTBC24/secfpr.h` | Declarations of the above |
| `BPTBC24/fpr_gadgets.c` | Masked binary64 arithmetic: multiplication, addition, normalisation, and the shifts they need |
| `BPTBC24/fpr_gadgets.h` | Declarations of the above |
| `BPTBC24/fpr_modify.c` | The variants of those gadgets used by the division path |
| `BPTBC24/fpr_modify.h` | Declarations of the above |
| `BPTBC24/gadgets.c`, `gadgets.h`, `utils.c`, `utils.h` | The same primitives and the same generator as in `COMFiZ/`, kept alongside so each tree builds on its own |
| `apps/main.c` | Whole-sampler driver; a `SAMPLER_` directive selects which sampler the binary times |
| `apps/bench_gadgets_main.c` | Gadget driver; a `BENCH_` directive selects which routine the binary times |
| `apps/test_comfiz.c` | Correctness tests: each masked gadget is unmasked and compared with an unmasked reference |
| `apps/count_rng.c` | Counts the random words drawn inside one sampler call |
| `verify/comfiz_verify.sage` | Certification of the instantiation: constant rounding, the bounds of Section 4, the conditions of Corollary 2, and a simulation checked against them |
| `CMakeLists.txt` | Builds one shared library per implementation per masking order, one executable per routine, and the tests |
| `run_samplers.sh` | Runs each sampler, one process each, at every order built |
| `run_gadgets.sh` | Runs each gadget, one process each, counterparts adjacent, at every order built |
| `compare_exp.sh` | Times CORDIC against both polynomial baselines at every order built |
| `run_count_rng.sh` | Builds each sampler with a counting generator and reports the random words it draws |

## Building and running

Requires CMake 3.13 or later and a C99 compiler.

```
chmod +x run_samplers.sh run_gadgets.sh compare_exp.sh run_count_rng.sh
cmake -B build
cmake --build build -j
(cd build && ctest)
./run_samplers.sh
./run_gadgets.sh
```

`chmod` needs `sudo` only if the checkout is not owned by you.

Orders 1 to 5 are built by default, each as its own library and set of executables, since `MASKSIZE` is fixed at compile time. The scripts detect which orders are present and run them all.

```
cmake -B build -DMASKORDERS="1;3;5"        # build a subset
ORDERS="1 3" ./run_gadgets.sh              # run a subset
./run_samplers.sh comfiz bptbc24_nr        # run named targets only
```

Each line reports the routine, its median, the order, and the trial count:

```
  COMFiZ            114.82us   (order 1, median of 10000 runs)
```

The trial count is `ITER`, set with `cmake -B build -DITER=100000`. At the default of 10000 a full run over five orders takes several minutes, dominated by BPTBC24 at the higher orders. `ITER` = 100000 was used for the timings in the paper.

Three samplers are built. `comfiz` is Algorithm 7; `bptbc24` is BPTBC24's sampler; `bptbc24_nr` is the same sampler with the Newton–Raphson reciprocal of the follow-up work, selected by the `BPTBC24_NR` definition inside `SamplerZ`. Eight gadgets are built, in counterpart groups: `SecFxpInv` against `SecFprInvNR`; `SecFxpExp` against `SecApproxExp` and the two fixed-point polynomials `SecFxpPolyExp` and `SecFxpPolyExpQ`; and `SecFxpBerExp` against `SecFprBerExp`.

Every binary times exactly one routine, so no measurement is affected by another running before it in the same process. Both implementations are compiled the same way, as shared libraries, so neither is favoured by the indirection a shared object adds on every gadget call. For stable numbers, pin the process to one core and fix the CPU frequency, for example with `taskset -c 2` and the `performance` governor with turbo boost disabled.

## Correctness tests

`ctest` runs `test_comfiz_d<d>` at every order built. Each test unmasks the outputs of the masked gadgets and compares them with unmasked references computed in the test:

- the masked base sampler against Falcon's RCDT sampler, for the same uniform input, at every threshold and on random inputs (`z` and `C(z0)`);
- `SecFxpExp` against CORDIC that updates the residual angle at every step, over the whole supported input range;
- `SecFxpPolyExp` and `SecFxpPolyExpQuad` against Falcon's integer `ApproxExp`;
- `B2A128` and `B2A128Quad` on random 128-bit values.

All comparisons are exact.

## CORDIC against the fixed-point polynomial

```
cmake -B build -DMASKORDERS="$(seq -s ';' 1 15)"
cmake --build build -j
./compare_exp.sh
```

For each order, the script times `SecFxpExp` and the polynomial with each B2A in the same session, and prints which is faster and by how much. `ORDERS="8 9 10" ./compare_exp.sh` runs a subset. The CHES 2017 B2A makes two recursive calls on one share fewer, so the polynomial's cost roughly doubles with each order; with the CHES 2014 B2A both methods are quadratic in the order.

## Randomness

`rand64` in both trees is the ChaCha20 generator of Falcon's reference implementation, seeded once from `/dev/urandom` (`CryptGenRandom` on Windows). Every share, refresh and coin is drawn through it, and every timing includes it.

```
bash run_count_rng.sh 1000
ORDERS="1 3" bash run_count_rng.sh 500
```

`run_count_rng.sh` builds each sampler with `-DCOUNT_RNG`, outside CMake, and reports the mean number of 64-bit words drawn per sampler call and the ratio to COMFiZ. The masking of the inputs is excluded. Exact totals and the minimum and maximum per call are written to `build_count_rng/rand64_counts.csv`.

## Verification
The SageMath script checks that every stored constant is correctly rounded, evaluates the bounds of Section 4 and the two conditions of Corollary 2, then simulates the pipeline and compares the observed errors with those bounds. It takes about a minute and every line should read `True` or `yes`. The checks are done with the fixed-point quantities exact over `QQ` and the transcendental ones in `RealField(200)`. Start Sage in the `verify` directory and load it:

```
cd verify
sage
```
```
sage: load("comfiz_verify.sage")
```
