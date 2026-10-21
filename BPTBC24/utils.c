#include <assert.h> //assert for debug
#include <stdbool.h> //boolean type
#include <stdio.h> //printf
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "utils.h"

//Constants

#define SIZE              63U //64-1
#define HALF_SIZE         32U
#define HALF_BIT          4294967295U //eq. 0xffffffff
#define ONE               1UL
#define THREE             3UL

//----------------------------------------------------------------------------------------

uint64_t double_to_bits(double d) {
    uint64_t b;
    memcpy(&b, &d, sizeof(d));
    return b;
}

int cmp_double(const void *a, const void *b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

double calc_median(double values[ITER]) {
    qsort(values, ITER, sizeof(values[0]), cmp_double);

    double mid1 = values[ITER/2 - 1];
    double mid2 = values[ITER/2];
    return (mid1 + mid2) / 2.0;
}

//----------------------------------------------------------------------------------------
// Random number generation.
//
// rand64() is the ChaCha20-based PRNG of Falcon's reference implementation
// (prng_refill and prng_get_u64), seeded once from /dev/urandom (CryptGenRandom on
// Windows). Every random word used by both samplers comes from it.
//
// COUNT_RNG counts the calls in rand64_calls (used by apps/count_rng.c).
// INSECURE_FAST_RNG replaces ChaCha20 by splitmix64. It is NOT cryptographic and
// only serves to measure the run time spent outside random number generation.
//----------------------------------------------------------------------------------------

#ifdef COUNT_RNG
uint64_t rand64_calls = 0;
#define COUNT_RAND64() (rand64_calls++)
#else
#define COUNT_RAND64() ((void)0)
#endif

#ifdef INSECURE_FAST_RNG

uint64_t rand64(void)
{
    static uint64_t state = 0x9E3779B97F4A7C15ULL;
    uint64_t z;

    COUNT_RAND64();
    z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

#else

#if defined(_WIN32)
#define FALCON_RAND_WIN32 1
#define FALCON_RAND_URANDOM 0
#else
#define FALCON_RAND_WIN32 0
#define FALCON_RAND_URANDOM 1
#endif

// PRNG state with a 512-byte output buffer, so values are generated in advance.
// The unions with dummy_u64 ensure alignment for 64-bit access.
typedef struct {
    union {
        uint8_t d[512]; // MUST be 512, exactly
        uint64_t dummy_u64;
    } buf;
    size_t ptr;
    union {
        uint8_t d[256];
        uint64_t dummy_u64;
    } state;
    int type;
} prng;

#if FALCON_RAND_URANDOM
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif
#if FALCON_RAND_WIN32
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "advapi32")
#endif

// Fills seed with len bytes from the operating system; returns 1 on success.
static int get_seed(void *seed, size_t len)
{
    (void)seed;
    if (len == 0) {
        return 1;
    }
#if FALCON_RAND_URANDOM
    {
        int f;

        f = open("/dev/urandom", O_RDONLY);
        if (f >= 0) {
            while (len > 0) {
                ssize_t rlen;

                rlen = read(f, seed, len);
                if (rlen < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    break;
                }
                seed = (uint8_t *)seed + rlen;
                len -= (size_t)rlen;
            }
            close(f);
            if (len == 0) {
                return 1;
            }
        }
    }
#endif
#if FALCON_RAND_WIN32
    {
        HCRYPTPROV hp;

        if (CryptAcquireContext(&hp, 0, 0, PROV_RSA_FULL,
            CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
        {
            BOOL r;

            r = CryptGenRandom(hp, (DWORD)len, seed);
            CryptReleaseContext(hp, 0);
            if (r) {
                return 1;
            }
        }
    }
#endif
    return 0;
}

// ChaCha20 PRNG. The state is the key (32 bytes), the IV (16 bytes) and the block
// counter (8 bytes), which is XORed into the first 8 bytes of the IV. Output words
// are interleaved as in the AVX2 implementation that runs eight ChaCha20 instances
// in parallel, so the output matches Falcon's reference PRNG.
static void prng_refill(prng *p)
{
    static const uint32_t CW[] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574
    };

    uint64_t cc;
    size_t u;

    // The state uses local endianness; only the output bytes are little endian.
    cc = *(uint64_t *)(p->state.d + 48);
    for (u = 0; u < 8; u ++) {
        uint32_t state[16];
        size_t v;
        int i;

        memcpy(&state[0], CW, sizeof CW);
        memcpy(&state[4], p->state.d, 48);
        state[14] ^= (uint32_t)cc;
        state[15] ^= (uint32_t)(cc >> 32);
        for (i = 0; i < 10; i ++) {

#define QROUND(a, b, c, d)   do { \
        state[a] += state[b]; \
        state[d] ^= state[a]; \
        state[d] = (state[d] << 16) | (state[d] >> 16); \
        state[c] += state[d]; \
        state[b] ^= state[c]; \
        state[b] = (state[b] << 12) | (state[b] >> 20); \
        state[a] += state[b]; \
        state[d] ^= state[a]; \
        state[d] = (state[d] <<  8) | (state[d] >> 24); \
        state[c] += state[d]; \
        state[b] ^= state[c]; \
        state[b] = (state[b] <<  7) | (state[b] >> 25); \
    } while (0)

            QROUND( 0,  4,  8, 12);
            QROUND( 1,  5,  9, 13);
            QROUND( 2,  6, 10, 14);
            QROUND( 3,  7, 11, 15);
            QROUND( 0,  5, 10, 15);
            QROUND( 1,  6, 11, 12);
            QROUND( 2,  7,  8, 13);
            QROUND( 3,  4,  9, 14);

#undef QROUND

        }

        for (v = 0; v < 4; v ++) {
            state[v] += CW[v];
        }
        for (v = 4; v < 14; v ++) {
            state[v] += ((uint32_t *)p->state.d)[v - 4];
        }
        state[14] += ((uint32_t *)p->state.d)[10]
            ^ (uint32_t)cc;
        state[15] += ((uint32_t *)p->state.d)[11]
            ^ (uint32_t)(cc >> 32);
        cc ++;

        // same interleaving as the AVX2 implementation
        for (v = 0; v < 16; v ++) {
            p->buf.d[(u << 2) + (v << 5) + 0] =
                (uint8_t)state[v];
            p->buf.d[(u << 2) + (v << 5) + 1] =
                (uint8_t)(state[v] >> 8);
            p->buf.d[(u << 2) + (v << 5) + 2] =
                (uint8_t)(state[v] >> 16);
            p->buf.d[(u << 2) + (v << 5) + 3] =
                (uint8_t)(state[v] >> 24);
        }
    }
    *(uint64_t *)(p->state.d + 48) = cc;

    p->ptr = 0;
}

// 64-bit value from the PRNG. The buffer is refilled when fewer than 9 bytes
// remain, so the last few bytes may be dropped but the buffer is never empty.
static inline uint64_t prng_get_u64(prng *p)
{
    size_t u;

    u = p->ptr;
    if (u >= (sizeof p->buf.d) - 9) {
        prng_refill(p);
        u = 0;
    }
    p->ptr = u + 8;

    return (uint64_t)p->buf.d[u + 0]
        | ((uint64_t)p->buf.d[u + 1] << 8)
        | ((uint64_t)p->buf.d[u + 2] << 16)
        | ((uint64_t)p->buf.d[u + 3] << 24)
        | ((uint64_t)p->buf.d[u + 4] << 32)
        | ((uint64_t)p->buf.d[u + 5] << 40)
        | ((uint64_t)p->buf.d[u + 6] << 48)
        | ((uint64_t)p->buf.d[u + 7] << 56);
}

uint64_t rand64(void)
{
    static prng p;
    static bool b_oneway = false;

    COUNT_RAND64();
    if (!b_oneway) {
        uint8_t tmp[56];
        uint64_t th, tl;
        int i;

        if (!get_seed(tmp, sizeof tmp)) {
            abort();
        }
        for (i = 0; i < 14; i ++) {
            uint32_t w;

            w = (uint32_t)tmp[(i << 2) + 0]
                | ((uint32_t)tmp[(i << 2) + 1] << 8)
                | ((uint32_t)tmp[(i << 2) + 2] << 16)
                | ((uint32_t)tmp[(i << 2) + 3] << 24);
            *(uint32_t *)(p.state.d + (i << 2)) = w;
        }
        tl = *(uint32_t *)(p.state.d + 48);
        th = *(uint32_t *)(p.state.d + 52);
        *(uint64_t *)(p.state.d + 48) = tl + (th << 32);
        prng_refill(&p);
        b_oneway = true;
    }
    return prng_get_u64(&p);
}

#undef FALCON_RAND_WIN32
#undef FALCON_RAND_URANDOM

#endif // INSECURE_FAST_RNG

// Uniform value in [0, mod) by rejection, drawn through rand64; mod = 0 means 2^64.
uint64_t randmod(uint64_t ui64_mod)
{
    uint64_t ui64_res, ui64_limit;

    if (ui64_mod == 0) {
        return rand64();
    }
    ui64_limit = (0ULL - ui64_mod) % ui64_mod;
    do {
        ui64_res = rand64();
    } while (ui64_res < ui64_limit);
    ui64_res %= ui64_mod;
    assert(ui64_res < ui64_mod);
    return ui64_res;
}

void print_binary_form(uint64_t ui64_in)
{
    int64_t ui64_i;
    printf("0b");
    for(ui64_i = SIZE; (0 <= ui64_i); ui64_i--){
        printf("%d", (int)((ui64_in >> ui64_i) & ONE));
    }
    printf("\n");
}

// Returns outup and outdown such that out = outup * 2^64 + outdown, using only
// 32x32 -> 64 multiplications.
void Mult128(uint64_t *ui64_outup, uint64_t *ui64_outdown,
             uint64_t ui64_in1, uint64_t ui64_in2)
{
    uint64_t ui64_au, ui64_ad, ui64_bu, ui64_bd;
    uint64_t ui64_multuu, ui64_multud, ui64_multdu, ui64_multdd;
    uint64_t ui64_addlow, ui64_addup;

    ui64_au = ui64_in1 >> HALF_SIZE;
    ui64_ad = ui64_in1 & HALF_BIT;
    ui64_bu = ui64_in2 >> HALF_SIZE;
    ui64_bd = ui64_in2 & HALF_BIT;

    ui64_multuu = ui64_au * ui64_bu;
    ui64_multud = ui64_au * ui64_bd;
    ui64_multdu = ui64_ad * ui64_bu;
    ui64_multdd = ui64_ad * ui64_bd;

    *ui64_outup = ui64_multuu;
    *ui64_outdown = ui64_multdd & HALF_BIT;

    ui64_addlow = (ui64_multud & HALF_BIT) + (ui64_multdu & HALF_BIT);
    ui64_addlow = ui64_addlow + (ui64_multdd >> HALF_SIZE);
    ui64_addup = (ui64_multud >> HALF_SIZE) + (ui64_multdu >> HALF_SIZE);
    ui64_addup = ui64_addup + ((ui64_addlow >> HALF_SIZE) & THREE);
    *ui64_outdown += ui64_addlow << HALF_SIZE;
    *ui64_outup += ui64_addup;
}

void Add128(uint64_t *ui64_outup, uint64_t *ui64_outdown, uint64_t ui64_in1up,
            uint64_t ui64_in1down, uint64_t ui64_in2up, uint64_t ui64_in2down)
{
    uint64_t ui64_in12_A, ui64_in12_B, ui64_in22_A, ui64_in22_B;

    ui64_in12_A = ui64_in1down >> HALF_SIZE;
    ui64_in12_B = ui64_in1down & HALF_BIT;
    ui64_in22_A = ui64_in2down >> HALF_SIZE;
    ui64_in22_B = ui64_in2down & HALF_BIT;

    ui64_in12_B += ui64_in22_B;
    ui64_in12_A += ui64_in22_A + ((ui64_in12_B >> HALF_SIZE) & ONE);

    *ui64_outdown = (ui64_in12_B & HALF_BIT)^((ui64_in12_A & HALF_BIT) << HALF_SIZE);

    *ui64_outup = ui64_in1up + ui64_in2up + ((ui64_in12_A >> HALF_SIZE) & THREE);
}

//----------------------------------------------------------------------------------------

// (u1*2^64 + d1)*(u2*2^64 + d2) = d1d2 + (d1u2 + d2u1) << 64 mod 2^128
void Mult128Bi(uint64_t *ui64_outup, uint64_t *ui64_outdown, uint64_t ui64_in1up,
               uint64_t ui64_in1down, uint64_t ui64_in2up, uint64_t ui64_in2down)
{
    uint64_t ui64_d1d2u,ui64_d1d2d,ui64_d1u2u,ui64_d1u2d,ui64_d2u1u,ui64_d2u1d;

    Mult128(&ui64_d1d2u, &ui64_d1d2d, ui64_in1down, ui64_in2down);
    Mult128(&ui64_d1u2u, &ui64_d1u2d, ui64_in1down, ui64_in2up);
    Mult128(&ui64_d2u1u, &ui64_d2u1d, ui64_in1up, ui64_in2down);

    *ui64_outup = ui64_d1d2u;
    *ui64_outdown = ui64_d1d2d;

    Add128(&ui64_d1d2u, &ui64_d1d2d, ui64_d1u2u, ui64_d1u2d, ui64_d2u1u, ui64_d2u1d);
    // A = d1u2 + d2u1
    ui64_d2u1u = *ui64_outup;
    ui64_d2u1d = *ui64_outdown;
    Add128(ui64_outup, ui64_outdown, ui64_d2u1u, ui64_d2u1d, ui64_d1d2d, 0);
    // out = d1d2 + A << 64
}


uint64_t subq(uint64_t ina, uint64_t inb, uint64_t mod)
{
    uint64_t res1, res2;
    res1 = (ina - inb) % mod;
    res2 = (ina - inb + mod) % mod;
    if (ina < inb){
        return(res2);
    }else{
        return(res1);
    }
}
