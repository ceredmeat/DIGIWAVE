# Builds digiwave's tables.h: PPG-style wavetables made fresh, plus four tables from the
# Jomox Alphabase wave file (ABWV0300.wav, 64 shapes of 256 points).
# Each table: 16 frames, each frame 128 signed 8-bit samples (the PPG's own size and depth),
# band-limited to 48 harmonics so a frame itself adds no aliasing.
import sys, wave, struct
import numpy as np

N = 128          # samples a frame
F = 16           # frames a table
H = 48           # highest harmonic kept
src_wav, out = sys.argv[1:3]
ph = np.arange(N) / N

def additive(amps, phases=None):
    x = np.zeros(N)
    for k, a in enumerate(amps, 1):
        if k > H or a == 0: continue
        p = 0 if phases is None else phases[k - 1]
        x += a * np.sin(2 * np.pi * k * ph + p)
    return x

def bandlimit(sig):
    """sig sampled finely over one cycle -> N samples, harmonics 1..H only, no DC."""
    S = np.fft.rfft(sig)
    S[0] = 0
    S[H + 1:] = 0
    T = np.zeros(N // 2 + 1, complex)
    T[:H + 1] = S[:H + 1] * (N / len(sig))
    return np.fft.irfft(T, N)

FINE = 4096
pf = np.arange(FINE) / FINE

def saw_amps(n=H, tilt=1.0):
    return [(1.0 / k) ** tilt for k in range(1, n + 1)]

def frames(fn):
    return [fn(i / (F - 1)) for i in range(F)]

tables = []   # (name, [F arrays])

# 1 PWM: saw into narrowing pulse
def pwm(t):
    w = 0.5 - 0.47 * t
    return bandlimit(np.where(pf < w, 1.0, -1.0) - (2 * w - 1))
tables.append(('PWM', frames(pwm)))
# 2 harmonic build: sine, then one more saw harmonic each frame (up to all 48)
tables.append(('BILD', frames(lambda t: additive([1 / k if k <= 1 + int(round(t * 47)) else 0 for k in range(1, H + 1)]))))
# 3 odd build: square built up
tables.append(('ODD', frames(lambda t: additive([1 / k if k % 2 and k <= 1 + int(round(t * 47)) else 0 for k in range(1, H + 1)]))))
# 4 resonant saw: a resonance peak sweeping up a saw (the PPG's classic sweep)
def res(t, base):
    c = 1.5 + t * 30
    return additive([base(k) * (0.25 + 3.0 * np.exp(-((k - c) / 1.6) ** 2)) for k in range(1, H + 1)])
tables.append(('RSAW', frames(lambda t: res(t, lambda k: 1 / k))))
tables.append(('RSQR', frames(lambda t: res(t, lambda k: (1 / k) if k % 2 else 0.04 / k))))
# 6 vowels: formant pairs A E I O U and back
VOW = [(800, 1150), (400, 2100), (300, 2300), (450, 800), (330, 700)]
def vowel(t, f0=110.0):
    x = t * (len(VOW) - 1)
    i = min(int(x), len(VOW) - 2); u = x - i
    f1 = VOW[i][0] * (1 - u) + VOW[i + 1][0] * u
    f2 = VOW[i][1] * (1 - u) + VOW[i + 1][1] * u
    a = []
    for k in range(1, H + 1):
        f = k * f0
        a.append((1 / k ** 0.5) * (np.exp(-((f - f1) / 90) ** 2) + 0.6 * np.exp(-((f - f2) / 130) ** 2) + 0.02))
    return additive(a)
tables.append(('VOX', frames(vowel)))
# 7 sync sweep: saw hard-synced, slave ratio 1..8
tables.append(('SYNC', frames(lambda t: bandlimit(2 * ((pf * (1 + 7 * t)) % 1.0) - 1))))
# 8 single partial walking up: harmonic 1..16, with a soft fundamental
tables.append(('PART', frames(lambda t: additive([0.35 if k == 1 else (1.0 if k == 1 + int(round(t * 15)) else 0) for k in range(1, H + 1)]))))
# 9 organ drawbars
BARS = [1, 2, 3, 4, 6, 8, 10, 12, 16]
def organ(t):
    a = np.zeros(H)
    for j, h in enumerate(BARS):
        lvl = np.clip(t * len(BARS) - j + 1, 0, 1) if j else 1.0
        a[h - 1] += lvl * (0.8 ** j)
    return additive(list(a))
tables.append(('ORGN', frames(organ)))
# 10-12 FM sweeps: index 0..8, ratios 1, 2, 3
def fm(r):
    return lambda t: bandlimit(np.sin(2 * np.pi * pf + (t * 8) * np.sin(2 * np.pi * r * pf)))
tables.append(('FM1', frames(fm(1))))
tables.append(('FM2', frames(fm(2))))
tables.append(('FM3', frames(fm(3))))
# 13 wavefolder: sine folded harder and harder
def fold(t):
    x = np.sin(2 * np.pi * pf) * (1 + 6 * t)
    return bandlimit(np.sin(np.pi / 2 * x))
tables.append(('FOLD', frames(fold)))
# 14 phase distortion (CZ-style resonance): a cosine window sweeping
def pd(t):
    r = 1 + 15 * t
    win = 1 - pf
    return bandlimit(win * np.cos(2 * np.pi * r * pf) - 0)
tables.append(('PD', frames(pd)))
# 15 metallic comb: saw with every n-th harmonic, n sweeping
tables.append(('COMB', frames(lambda t: additive([1 / k ** 0.7 * (1.0 if (k % (1 + int(round(t * 7)))) == 1 or t == 0 else 0.08) for k in range(1, H + 1)]))))
# 16 digital grit: fixed random spectra crossfaded (seeded, reproducible)
rng = np.random.default_rng(3)
G = [rng.random(H) ** 3 / np.arange(1, H + 1) ** 0.5 for _ in range(4)]
GP = [rng.random(H) * 2 * np.pi for _ in range(4)]
def grit(t):
    x = t * 3; i = min(int(x), 2); u = x - i
    return additive(list(G[i] * (1 - u) + G[i + 1] * u), list(GP[i] * (1 - u) + GP[i + 1] * u))
tables.append(('GRIT', frames(grit)))
# 17 staircase: sine with fewer and fewer levels (PPG-era grit)
tables.append(('STAI', frames(lambda t: bandlimit(np.round(np.sin(2 * np.pi * pf) * (24 - 22 * t)) / (24 - 22 * t)))))
# 18 triangle to saw to square
def tri_saw_sq(t):
    tri = 1 - 4 * np.abs(((pf + 0.25) % 1) - 0.5)
    saw = 2 * pf - 1
    sq = np.where(pf < 0.5, 1.0, -1.0)
    if t < 0.5:
        u = t * 2; return bandlimit(tri * (1 - u) + saw * u)
    u = (t - 0.5) * 2; return bandlimit(saw * (1 - u) + sq * u)
tables.append(('TSS', frames(tri_saw_sq)))
# 19 hollow: saw losing its even harmonics
tables.append(('HOLW', frames(lambda t: additive([(1 / k) * (1 - t * 0.97 if k % 2 == 0 else 1) for k in range(1, H + 1)]))))
# 20 dark to bright
tables.append(('BRIT', frames(lambda t: additive(saw_amps(tilt=3.0 - 2.4 * t)))))
# 21 rectified: sine to half-wave to full-wave
def rect(t):
    s = np.sin(2 * np.pi * pf)
    return bandlimit(np.maximum(s, -1 + t) if t < 1 else np.abs(s))
tables.append(('RECT', frames(rect)))
# 22 bell-ish: odd partial clusters growing
tables.append(('BELL', frames(lambda t: additive([(1.0 if k == 1 else 0) + t * (0.9 if k in (3, 7, 11, 17, 23, 31) else 0) + t * t * (0.5 if k in (5, 13, 29, 41) else 0) for k in range(1, H + 1)]))))

# Jomox Alphabase shapes: all 64, as 4 tables of 16 frames
w = wave.open(src_wav); n = w.getnframes()
A = np.array(struct.unpack('<%dh' % n, w.readframes(n)), float).reshape(64, 256)
for j in range(4):
    tables.append(('JX%d' % (j + 1), [bandlimit(np.interp(pf, np.arange(256) / 256, A[j * 16 + i])) for i in range(F)]))

def to8(x):
    m = np.max(np.abs(x))
    if m < 1e-9: return np.zeros(N, int)
    return np.clip(np.round(x / m * 127), -127, 127).astype(int)

with open(out, 'w') as f:
    f.write('/* Made by gen_digiwave.py. Do not edit by hand. %d tables of %d frames of %d 8-bit samples:\n'
            ' * PPG-style tables made fresh, and JX1-JX4 from the Jomox Alphabase wave file (its 64 shapes). */\n'
            % (len(tables), F, N))
    f.write('#define DW_TABLES %d\n#define DW_FRAMES %d\n#define DW_LEN %d\n' % (len(tables), F, N))
    f.write('static const int8_t dw_tab[DW_TABLES][DW_FRAMES][DW_LEN] = {\n')
    for name, fr in tables:
        f.write('  { /* %s */\n' % name)
        for x in fr:
            f.write('    {' + ','.join(str(v) for v in to8(x)) + '},\n')
        f.write('  },\n')
    f.write('};\n')
    f.write('static const char *const dw_names[DW_TABLES] = {' + ','.join('"%s"' % nm for nm, _ in tables) + '};\n')
    # the wave envelope's decay: DEC 0..127 -> 5 ms .. 10 s; per 32-sample block (1500 a second), Q16
    ms = [5.0 * 2000.0 ** (v / 127.0) for v in range(128)]
    mult = [int(round(65536 * np.exp(-1.0 / (m / 1000.0 * 1500.0)))) for m in ms]
    f.write('static const uint16_t dw_dec_mult[128] = {' + ','.join(str(min(65535, x)) for x in mult) + '};\n')
    f.write('static const uint16_t dw_dec_ms[128] = {' + ','.join(str(int(round(m))) for m in ms) + '};\n')
print(len(tables), 'tables')
