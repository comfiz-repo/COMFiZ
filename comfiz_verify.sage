# Verification of the Falcon-512 instantiation of Section 5. We check that every
# stored constant is correctly rounded, evaluate the bounds of Section 4 and the
# two conditions of Corollary 2, then reproduce the unshared integer behaviour of
# the COMFiZ pipeline and compare the observed errors with those bounds. Every
# fixed-point quantity is kept exact over QQ; only the transcendental bounds are
# evaluated in RealField(200).
import random, math

R = RealField(200)

def Dyad(y):                              # exact rational of a binary64
    num, den = float(y).as_integer_ratio()
    return QQ(num)/QQ(den)

# ---------------- parameters ----------------
q, n, qx, qinv, w, Bz = 62, 63, 54, 56, 64, 18
sig_min = Dyad('1.2778336969128337')        # Falcon's stored binary64
sig_max = QQ('18205/10000')
LAM = 128
a = 2*LAM+1                # Renyi order 2*lambda+1
QZ = R(2)^74               # COMFiZ calls
Qbs = R(2)^75              # BaseSampler calls
trials = 100000

def lg(x):
    return log(R(x), 2)

def nearest(x):                           # round to the nearest integer
    return floor(x + R(1)/2)

def fmt(x):
    return "2^%.4f" % float(lg(x))

# ---------------- code constants ----------------
ALPHA_STAR_Q62     = 0x62C0310A4F066350
CSTAR_Q62     = 0x09A7C5E0C1DDE105
LSTAR_Q62     = 0x2C5C85FDF473DE6B
LSTAR_INV_Q56 = 0x0171547652B82FE1
A_TABLE_Q62 = [
 2533227465661617455, 1177883693488034215,
 579491617566063541,  288606558191708983,
 144162128078953545,  72063458959086026,
 36029530053560535,   18014490136289835,
 9007210708013329,    4503601059027081,
 2251799992642244,    1125899929212246,
 562949956217515,     281474977060181,
 140737488399019,     70368744183125,
 35184372089515,      17592186044501,
 8796093022219,       4398046511105]
BASE_RCDT = [(0xA3F<<60)|0x7F42ED3AC391802, (0x54D<<60)|0x32B181F3F7DDB82,
 (0x227<<60)|0xDCDD0934829C1FF, (0x0AD<<60)|0x1754377C7994AE4,
 (0x029<<60)|0x5846CAEF33F1F6F, (0x007<<60)|0x74AC754ED74BD5F,
 (0x001<<60)|0x024DD542B776AE4, 117656387352093658, 8867391802663976,
 496969357462633, 20680885154299, 638331848991, 14602316184, 247426747,
 3104126, 28824, 198, 1]

# every stored constant must be the correctly rounded one
print("== constant rounding checks ==")
print(" LSTAR_Q62     ok:", LSTAR_Q62     == nearest(R(2)^62*log(R(2))))
print(" LSTAR_INV_Q56 ok:", LSTAR_INV_Q56 == nearest(R(2)^56/log(R(2))))
print(" CSTAR_Q62     ok:", CSTAR_Q62     == nearest(R(2)^62/R(2*sig_max^2)))
bad = [j for j in range(1,21) if A_TABLE_Q62[j-1] != nearest(R(2)^62*arctanh(R(2)^(-j)))]
print(" A_TABLE_Q62[1..20] ok:", bad == [])
tail = [j for j in range(21,61) if nearest(R(2)^62*arctanh(R(2)^(-j))) != 2^(62-j)]
print(" A_j = 2^(62-j) for 21<=j<=60 ok:", tail == [])

def A(j):
    return A_TABLE_Q62[j-1] if j <= 20 else 1 << (62-j)

# ---------------- schedule ----------------
sched = []
i = 1
rep = False
while i <= 60:
    sched.append(i)
    if i in (4,13,40) and not rep:
        rep = True
    else:
        i += 1
        rep = False
assert len(sched) == 63, len(sched)

K = R(1)
Gam = R(1)
for Ii in sched:
    K *= sqrt(1 - R(2)^(-2*Ii))
    Gam *= (1 + R(2)^(-Ii))
print("\n== schedule ==")
print(" n            =", len(sched))
print(" K            = %.8f" % float(K))
print(" Gamma        = %.8f" % float(Gam))
print(" Gamma/K      = %.6f" % float(Gam/K))
Asum = sum(A(Ii) for Ii in sched)
print(" sum A/2^62   = %.6f" % float(R(Asum)/R(2)^62))
# convergence condition of Lemma 4
ok_tail = True
for k in range(len(sched)):
    if A(sched[k]) > sum(A(sched[j]) for j in range(k+1, len(sched))) + A(sched[-1]):
        ok_tail = False
print(" tail cond    :", ok_tail, " (equality at 2nd index 40:",
      A(40) == sum(A(sched[j]) for j in range(sched.index(40)+2, len(sched))) + A(sched[-1]), ")")

eps_T = sum(abs(R(A(Ii))/R(2)^q - arctanh(R(2)^(-Ii))) for Ii in sched)
rho = R(A(sched[-1]))/R(2)^q
print(" eps_T        =", fmt(eps_T))
print(" rho          =", fmt(rho))

bnd = {}

# ---------------- inversion / scale (Lemma 9) ----------------
bnd['eps_inv'] = (1 + R(2*q)/R(sig_min)) * R(2)^(-q)
alpha   = R(sig_min)/K
eps_G   = alpha - R(ALPHA_STAR_Q62)/R(2)^q
bnd['eps_ccs'] = R(sig_min)*bnd['eps_inv'] + K*(eps_G + R(2)^(-q))
off = floor(R(2)^62*alpha) - ALPHA_STAR_Q62
off_min = ceil(R(sig_min^2)*bnd['eps_inv']*R(2)^62/K - (R(2)^62*alpha - floor(R(2)^62*alpha)))
print("\n== inversion / scale ==")
print(" ALPHA_STAR_Q62    = floor(2^62 smin/K) -", off, " ok:", off >= off_min, " (minimum offset", str(off_min) + ")")
print(" eps_G        = %.3f * 2^-62" % float(eps_G*R(2)^62))
print(" K*eps_G      = %.3f * 2^-62" % float(K*eps_G*R(2)^62))
print(" smin^2*e_inv = %.3f * 2^-62" % float(R(sig_min^2)*bnd['eps_inv']*R(2)^62))
print(" K*eps_G >= smin^2 eps_inv :", K*eps_G >= R(sig_min^2)*bnd['eps_inv'])   # gives ccs <= 1
ccs_min = R(sig_min/sig_max) - bnd['eps_ccs']
print(" ccs_min      = %.8f" % float(ccs_min))

# ---------------- exponent construction ----------------
bnd['eps_g'] = R(2)^(-qx) + (Bz+1)*bnd['eps_inv'] + R(2)^(-qx)/R(sig_min)
bnd['eps_x'] = R(2)^(-qx) + Bz^2*R(2)^(-q-1) + bnd['eps_g']*((Bz+1)/R(sig_min) + bnd['eps_g']/2)
xtrue = R(19^2/(2*sig_min^2) - 18^2/(2*sig_max^2))    # worst case z0 = 18, beta = 1
x_max = QQ('61663/1000')
print("\n== exponent ==")
print(" max exact x  = %.6f" % float(xtrue))
print(" x_max        =", float(x_max), " absorbs eps_x:", R(x_max) >= xtrue + bnd['eps_x'])

# decomposition modulo ln 2 (Lemma 7)
eps_L = log(R(2))*R(2)^(-qinv-1) + R(2)^(-q-1)/log(R(2)) + R(2)^(-q-qinv-2)
b = R(x_max)*eps_L + R(2)^(-q-1)
s_max = floor(x_max*LSTAR_INV_Q56/2^qinv)
bnd['eps_red'] = s_max*R(2)^(-q-1)
print(" eps_L        =", fmt(eps_L))
print(" b            =", fmt(b))
print(" s_max        =", s_max)
print(" b+eps_red<=1/4:", b+bnd['eps_red'] <= R(1)/4)
print(" sum A >= 2^62(ln2+b):", R(Asum) >= R(2)^62*(log(R(2))+b))

# ---------------- CORDIC (Theorem 2) ----------------
eps_R    = exp(b)*(exp(rho+eps_T)-1)
eps_eval = Gam*n*R(2)^(-q)
bnd['eps_cordic'] = eps_R + eps_eval
print("\n== CORDIC ==")
print(" eps_R        =", fmt(eps_R))
print(" eps_eval     =", fmt(eps_eval))
print(" exp(b)+eps_cordic < 2 :", exp(b)+bnd['eps_cordic'] < 2)
print(" Gamma*2^q/K < 2^w     :", Gam*R(2)^q/K < R(2)^w)

# ---------------- Bernoulli and output (Theorem 3, Lemma 10, Theorem 4) ----------------
bnd['eps_Ber'] = bnd['eps_cordic'] + exp(b) - 1 + R(2)^(-w) + 2*bnd['eps_red']
bnd['D_Ber']   = 4*bnd['eps_Ber']/ccs_min
bnd['D_x']     = exp(bnd['eps_x']) - 1
bnd['D_acc']   = (1+bnd['D_x'])*(1+bnd['D_Ber']) - 1
bnd['D_out']   = 2*bnd['D_acc']/(1-bnd['D_acc'])

# ---------------- Renyi, first condition of Corollary 2 ----------------
inner = a*(a-1)*bnd['D_out']^2/(2*(1-bnd['D_out'])^(a+1))
Ba_m1 = exp(log(1+inner)/(a-1)) - 1
thr1  = R(a-1)/(4*a*QZ)
print("\n== Renyi (finite precision) ==")
print(" B_a - 1      = 2^%.5f" % float(lg(Ba_m1)))
print(" (a-1)/(4aQZ) = 2^%.5f" % float(lg(thr1)))
print(" condition holds:", Ba_m1 <= thr1, " margin %.3f bits" % float(lg(thr1)-lg(Ba_m1)))

# ---------------- base sampler, second condition of Corollary 2 ----------------
chi = []
prev = QQ(2)^72
for k in range(19):
    cur = QQ(BASE_RCDT[k]) if k < 18 else QQ(0)
    chi.append((prev-cur)/QQ(2)^72)
    prev = cur
print("\n== base sampler ==")
print(" sum chi      =", sum(chi))
rho_tot = R(0)
kk = 0
while True:
    t = exp(-R(kk)^2/R(2*sig_max^2))
    rho_tot += t
    if t < R(10)^(-60) and kk > 10:
        break
    kk += 1
ideal = [exp(-R(k)^2/R(2*sig_max^2))/rho_tot for k in range(19)]
print(" ideal tail>18=", fmt(1 - sum(ideal)))
S = sum(R(chi[k])^a/ideal[k]^(a-1) for k in range(19))
Rbs_m1 = exp(log(S)/(a-1)) - 1
thr2 = 1/(4*Qbs)
print(" R_a(chi||D)-1= 2^%.5f" % float(lg(Rbs_m1)))
print(" 1/(4Qbs)     = 2^%.5f" % float(lg(thr2)))
print(" condition holds:", Rbs_m1 <= thr2, " margin %.3f bits" % float(lg(thr2)-lg(Rbs_m1)))

# =============================================================================
# Simulation of the deployed computation against the bounds above
# =============================================================================

M64 = (1 << 64) - 1
def s64(x):
    x &= M64
    return x - (1 << 64) if x >> 63 else x

def asr(x, k):                            # arithmetic right shift, floor division
    return x // (1 << k)

# Algorithm 8, unshared
def SecFprToInt(mu):                      # mu binary64
    Q = floor(Dyad(mu) * (1 << 54))
    return asr(Q, 54), Q & ((1 << 54) - 1)

# Algorithm 6, unshared
def SecFxpInv(Xsig):                      # Xsig = 2^62 * sigma'
    Y = (1 << 62) - Xsig
    T = 0
    for i in range(1, 63):
        M = 1 if Y >= 0 else 0
        T = (T << 1) | M
        Y = Y - (Xsig >> i) if M else Y + (Xsig >> i)
    return (T << 1) | 1

# Algorithm 2, unshared
def SecFxpExp(xq, W0):                    # xq signed Q62, W0 unsigned Q62
    W, Z = W0, xq
    for Ii in sched:
        M = 1 if Z >= 0 else 0
        Ws = W >> Ii
        Z = Z - A(Ii) if M else Z + A(Ii)
        W = W - Ws if M else W + Ws
    return W

# half-Gaussian draw z0 and uniform sign bit beta
def BaseSampler():
    u = random.getrandbits(int(72))
    return sum(1 for c in BASE_RCDT if u < c), random.getrandbits(1)

# lines 9-20 of Algorithm 7: scaled displacement G and exponent X, both Q54
def COMFiZExponent(z, Rmu, Usig, Cz0):
    T = (z << 54) - Rmu
    G = asr(T * Usig, 62)
    return G, asr(G*G - Cz0, 55)

# lines 1-12 of Algorithm 5: shift s and reduced exponent R
def BerExpReduce(X):                      # X signed Q54
    Xe = (max(X, 0) << 2) & M64           # Q56
    P = (Xe * LSTAR_INV_Q56)
    s = (P >> 112) & M64
    S = 0
    for k in range(7):
        if (s >> k) & 1: S = (S + ((LSTAR_Q62 << k) & M64)) & M64
    return s, s64(((Xe << 6) - S) & M64)

obs = dict((k, R(0)) for k in bnd)
stat = {'s': 0, 'W': 0}

# one full trial: proposal z built from (z0, beta), center mu, deviation sigma
def Trial(sig, mu, z0, beta):
    Xsig = floor(sig * 2^62)              # exact, sigma' has 52 fraction bits
    assert Xsig == sig * 2^62
    mu_f, Rmu = SecFprToInt(mu)
    mu_r = Dyad(mu) - mu_f

    Usig = SecFxpInv(Xsig)
    obs['eps_inv'] = max(obs['eps_inv'], R(abs(QQ(Usig)/QQ(2)^62 - 1/sig)))

    W0 = asr(Usig * ALPHA_STAR_Q62, 62)
    ccs = K * R(W0) / R(2)^62
    assert 0 < ccs <= 1, ccs
    obs['eps_ccs'] = max(obs['eps_ccs'], abs(ccs - R(sig_min/sig)))

    z = beta + (2*beta - 1)*z0
    Cz0 = (z0*z0*CSTAR_Q62) << 47
    G, X = COMFiZExponent(z, Rmu, Usig, Cz0)
    obs['eps_g'] = max(obs['eps_g'], R(abs(QQ(G)/QQ(2)^54 - (z - mu_r)/sig)))

    x = (z - mu_r)^2/(2*sig^2) - QQ(z0^2)/(2*sig_max^2)
    assert 0 <= x <= x_max, x
    x_hat = QQ(X)/QQ(2)^54
    xe = max(x_hat, QQ(0))
    obs['eps_x'] = max(obs['eps_x'], R(abs(x_hat - x)))

    s, Rr = BerExpReduce(X)
    assert 0 <= s <= s_max, s
    stat['s'] = max(stat['s'], s)
    r = QQ(Rr)/QQ(2)^62
    obs['eps_red'] = max(obs['eps_red'], abs(R(r) - (R(xe) - s*log(R(2)))))

    Wn = SecFxpExp(Rr, W0)
    stat['W'] = max(stat['W'], Wn)
    obs['eps_cordic'] = max(obs['eps_cordic'], abs(R(Wn)/R(2)^62 - ccs*exp(-R(r))))

    T = (Wn << 2) & M64 if Wn < (1 << 62) else M64
    p_star = R(T) / R(2)^64 / R(2)^s
    p_hat = ccs * exp(-R(xe))
    p = ccs * exp(-R(x))
    obs['eps_Ber'] = max(obs['eps_Ber'], abs(p_star - p_hat) * R(2)^s)
    obs['D_x']   = max(obs['D_x'],   abs(p_hat/p - 1))
    obs['D_Ber'] = max(obs['D_Ber'], abs(p_star/p_hat - 1))
    obs['D_acc'] = max(obs['D_acc'], abs(p_star/p - 1))

def Dbl(x):                               # largest binary64 not exceeding x
    y = float(x)
    while Dyad(y) > x: y = math.nextafter(y, -math.inf)
    return Dyad(y)

# random trials
random.seed(int(1))
for trial in range(trials):
    sig = Dbl(sig_min + (sig_max - sig_min) * Dyad(random.random()))
    z0, beta = BaseSampler()
    Trial(sig, random.uniform(float(-2^20), float(2^20)), z0, beta)

# sweep of the whole support: every proposal, extremal sigma, grid of mu_r
for z0 in range(19):
    for beta in (0, 1):
        for sig in (Dbl(sig_min), Dbl(sig_max)):
            for k in range(64):
                Trial(sig, k/64.0, z0, beta)

obs['D_out'] = 2*obs['D_acc']/(1 - obs['D_acc'])   # Theorem 4, from the observed D_acc

print("\n== observed vs. proven ==")
print(" trials:", trials, "+ sweep   max s:", stat['s'],
      "  2^62 - max W_n:", (1 << 62) - stat['W'])
print(" %-11s %12s %12s %5s" % ("error", "observed", "bound", "ok"))
for k in ['eps_inv','eps_ccs','eps_g','eps_x','eps_red','eps_cordic','eps_Ber',
          'D_x','D_Ber','D_acc','D_out']:
    print(" %-11s %12s %12s %5s" % (k, fmt(obs[k]), fmt(bnd[k]),
                                    "yes" if obs[k] <= bnd[k] else "NO"))

# the sweep never reaches the saturating branch, so it is exercised directly:
# ccs = 1 together with a negative reduced exponent drives W_n past 2^62
W0_one = nearest(R(2)^62/K)
Wsat = SecFxpExp(-(1 << 40), W0_one)
Tsat = (Wsat << 2) & M64 if Wsat < (1 << 62) else M64
ccs_one = K * R(W0_one) / R(2)^62
err_sat = abs(R(Tsat)/R(2)^64 - ccs_one)
print("\n== saturating branch ==")
print(" W_n/2^62      = %.8f" % float(R(Wsat)/R(2)^62))
print(" 2^62 <= W_n < 2^63 :", (1 << 62) <= Wsat < (1 << 63))
print(" bit 62 set         :", (Wsat >> 62) & 1 == 1)
print(" W_n << 2 wraps to  =", fmt((Wsat << 2) & M64), "of 2^64")
print(" threshold saturated:", Tsat == M64)
print(" |p* - p_hat|  =", fmt(err_sat), " <= eps_Ber:", err_sat <= bnd['eps_Ber'])

# worst-case coordinate growth over the supported input range
worst = 0
for xq in [-floor(QQ(2)^62/10^15), 0, 1, floor(R(2)^62*log(R(2)))-1]:
    W = floor(R(2)^62 / K)
    Z = xq
    for Ii in sched:
        M = 1 if Z >= 0 else 0
        Ws = W >> Ii
        Z = Z - A(Ii) if M else Z + A(Ii)
        W = W - Ws if M else W + Ws
        worst = max(worst, W)
print(" max intermediate W/2^62 = %.6f" % float(R(worst)/R(2)^62),
      "(Lemma 5 allows Gamma/K = %.4f)" % float(Gam/K))
