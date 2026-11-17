/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DIGIWAVE: a PPG-style wavetable machine for the Digitakt MKI (core 3.0 + machine-pages, OS 1.54).
 *
 * Two wavetable oscillators and a sub, per track. Each table is 16 waves ("frames") of 128 8-bit
 * samples, the PPG's own size and depth: 22 tables made fresh in the PPG manner and 4 from the Jomox
 * Alphabase's wave file (tables.h, made by tools/gen_digiwave.py). WAVE picks the place in the table,
 * a decay envelope sweeps it (ENV, DEC), STEP goes from smooth sweeps to the PPG's hard steps, and BITS
 * crushes. The Digitakt's own filter, amp envelope, LFO and FX follow, as on a sample.
 *
 * Page 1 (A-H): TUNE  TABL  WAVE  ENV  DEC  STEP  BITS  LEV
 * Page 2:       TBL2  OFS   TUN2  DET  MIX  SUB   WAVE  VIEW  (VIEW's panel fills G and H)
 * TABL draws the table in 3D, WAVE the wave it plays, VIEW the table with both oscillators' places.
 *
 * Machine 33 (claimed below). */
#include "digitakt-mk1/core3.h"
#include "tables.h"

#define DW_ID   33
enum { K_TUNE, K_TABL, K_WAVE, K_ENV, K_DEC, K_STEP, K_BITS, K_LEV,      /* A-H: SLICE's slots */
       K_TBL2, K_OFS, K_TUN2, K_DET, K_MIX, K_SUB, K_VIEW };              /* extras 8-14 */
#define LAST    (DW_FRAMES - 1)

/* ---- shared maths -------------------------------------------------------------------------------- */

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static int32_t table_of(int32_t v) { return clampi(v * DW_TABLES / 128, 0, DW_TABLES - 1); }   /* 0..127 */
static int32_t table2_of(int32_t v, int32_t t1)                       /* 0..127: 0-4 SAME, then a table */
{
    return v < 5 ? t1 : clampi((v - 5) * DW_TABLES / 123, 0, DW_TABLES - 1);
}
static int32_t semis2_of(int32_t v) { return clampi((v - 64) * 24 / 63, -24, 24); }

/* STEP: how hard the move between two waves is. 0 a straight crossfade, 127 a hard step. */
static int32_t step_frac(int32_t f, int32_t step)
{
    int32_t k = 256 + step * step * 4;                                /* the slope at the middle, 8.8 */
    return clampi((((f - 128) * k) >> 8) + 128, 0, 255);
}

/* The place in the table, frames in 8.8 (0 .. LAST * 256), from WAVE 0..127 and a bipolar offset. */
static int32_t place(int32_t wave, int32_t offset)
{
    return clampi(wave * (LAST * 256) / 127 + offset, 0, LAST * 256);
}

/* ---- the sound (ev_render_voices: interrupt level, 1500 blocks a second) ------------------------- */

static uint32_t ph1[8], ph2[8], phs[8];
static uint32_t env[8];                                               /* the wave envelope, Q16 */
volatile int32_t dw_live_place[8];                                    /* the last block's place, for VIEW */

static uint32_t inc_for(int32_t pitch)                                /* 16.16 semitones, note 60 = C4 */
{
    pitch = clampi(pitch, 0, 87 << 16);
    return (23410327u >> 13) * (fw_pitch_tab[pitch / 384] >> 16);
}

static int32_t knob(int t, int k) { return clampi(cm_src_value(t, k) >> 8, 0, 127); }

void dw_render(int32_t *blocks)
{
    for (int t = 0; t < 8; t++) {
        if (fw_render_machine[t] != DW_ID)
            continue;
        const volatile int16_t *p = &fw_voice_params[53 * t];
        int32_t amp = fw_amp_level[2 * t + 1];
        if (!(fw_voice_start & (1u << t)) && fw_amp_level[2 * t] == FW_AMP_DECAY &&
            amp > -(1 << 15) && amp < (1 << 15)) {                     /* silent: the AMP has ended */
            for (int i = 0; i < 32; i++)
                blocks[32 * t + i] = 0;
            continue;
        }
        if (fw_voice_start & (1u << t)) {
            ph1[t] = ph2[t] = phs[t] = 0;
            env[t] = 65536;
        }
        int32_t tab1 = table_of(knob(t, K_TABL)), tab2 = table2_of(knob(t, K_TBL2), tab1);
        int32_t envamt = knob(t, K_ENV) - 64, step = knob(t, K_STEP);
        int32_t ofs = (knob(t, K_OFS) - 64) * (LAST * 256) / 64;
        int32_t sweep = ((envamt * (int32_t)env[t]) >> 16) * (LAST * 256) / 64;
        int32_t pos1 = place(knob(t, K_WAVE), sweep), pos2 = clampi(pos1 + ofs, 0, LAST * 256);
        env[t] = (env[t] * dw_dec_mult[knob(t, K_DEC)]) >> 16;
        dw_live_place[t] = pos1;

        int32_t f1 = pos1 >> 8, g1 = f1 < LAST ? f1 + 1 : f1, r1 = step_frac(pos1 & 255, step);
        int32_t f2 = pos2 >> 8, g2 = f2 < LAST ? f2 + 1 : f2, r2 = step_frac(pos2 & 255, step);

        /* the note and TUNE as a sample's (machine-pages' sine example), then OSC2's tune and detune */
        int32_t pitch = ((int32_t)p[K_TUNE] - 0x4000) * 256 + fw_voice_note[t] + (3 << 16);
        int32_t mix = knob(t, K_MIX), sub = knob(t, K_SUB) * 96, bits = knob(t, K_BITS);
        int32_t lev = (p[K_LEV] >> 8) & 0x7f, gain = lev * lev;         /* LEV's law, as a sample's: Q14 */
        int32_t mask = bits ? ~((1 << (4 + bits * 9 / 127)) - 1) : ~0;  /* 12 bits down to 3 */
        int32_t *out = &blocks[32 * t];
        uint32_t i1 = inc_for(pitch), q1 = ph1[t];

        /* Each oscillator reads its wave as the PPG did, a sample at a time with no smoothing in between,
         * and only crossfades two neighbouring waves (STEP's frac). An oscillator MIX leaves out, and the
         * sub at 0, cost nothing. */
        if (mix < 127) {
            const int8_t *a = dw_tab[tab1][f1], *b = dw_tab[tab1][g1];
            int32_t w = 127 - mix;
            if (r1 == 0 || f1 == g1) {
                for (int i = 0; i < 32; i++, q1 += i1)
                    out[i] = a[q1 >> 25] * w;
            } else {
                for (int i = 0; i < 32; i++, q1 += i1) {
                    int32_t x = q1 >> 25, u = a[x];
                    out[i] = (u + (((b[x] - u) * r1) >> 8)) * w;
                }
            }
        } else {
            for (int i = 0; i < 32; i++)
                out[i] = 0;
            q1 += i1 * 32;
        }
        uint32_t i2 = inc_for(pitch + (semis2_of(knob(t, K_TUN2)) << 16) + knob(t, K_DET) * 50 * 65536 / 12700);
        uint32_t q2 = ph2[t];
        if (mix > 0) {
            const int8_t *a = dw_tab[tab2][f2], *b = dw_tab[tab2][g2];
            if (r2 == 0 || f2 == g2) {
                for (int i = 0; i < 32; i++, q2 += i2)
                    out[i] += a[q2 >> 25] * mix;
            } else {
                for (int i = 0; i < 32; i++, q2 += i2) {
                    int32_t x = q2 >> 25, u = a[x];
                    out[i] += (u + (((b[x] - u) * r2) >> 8)) * mix;
                }
            }
        } else
            q2 += i2 * 32;
        uint32_t is = i1 >> 1, qs = phs[t];
        for (int i = 0; i < 32; i++, qs += is) {
            int32_t v = (out[i] * 3) >> 1;                                /* 8 bits x 7 -> Q15, at 3/4 */
            if (sub)
                v += (int32_t)qs < 0 ? -sub : sub;
            v = clampi(v, -32767, 32767) & mask;
            out[i] = (v * gain) << 2;                                     /* Q15 x Q14 -> Q31 */
        }
        ph1[t] = q1; ph2[t] = q2; phs[t] = qs;
    }
}

/* ---- the page: value texts -------------------------------------------------------------------------- */

static int32_t put(char *buf, const char *s)
{
    int i = 0;
    while ((buf[i] = s[i]))
        i++;
    return 1;
}

static int32_t put_num(char *buf, const char *prefix, int32_t v, const char *suffix)
{
    char d[8];
    int n = 0, i = 0;
    while (*prefix)
        buf[i++] = *prefix++;
    if (v < 0) {
        buf[i++] = '-';
        v = -v;
    }
    do {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        buf[i++] = d[--n];
    while (*suffix)
        buf[i++] = *suffix++;
    buf[i] = 0;
    return 1;
}

static int32_t signed_num(char *buf, int32_t v) { return put_num(buf, v > 0 ? "+" : "", v, ""); }
static int32_t v127(int32_t value) { return clampi(value >> 8, 0, 127); }

static int32_t tabl_text(char *buf, int32_t value, int32_t ctx, int32_t m)
{
    (void)ctx; (void)m;
    return put(buf, dw_names[table_of(v127(value))]);
}
static int32_t tbl2_text(char *buf, int32_t value, int32_t ctx, int32_t m)
{
    int32_t v = v127(value);
    (void)ctx; (void)m;
    return v < 5 ? put(buf, "SAME") : put(buf, dw_names[table2_of(v, 0)]);
}
static int32_t bip_text(char *buf, int32_t value, int32_t ctx, int32_t m)       /* -64..+63 */
{
    (void)ctx; (void)m;
    return signed_num(buf, v127(value) - 64);
}
static int32_t dec_text(char *buf, int32_t value, int32_t ctx, int32_t m)
{
    int32_t ms = dw_dec_ms[v127(value)];
    (void)ctx; (void)m;
    if (ms < 1000)
        return put_num(buf, "", ms, ctx ? " ms" : "");
    put_num(buf, "", ms / 1000, ".");
    char *e = buf;
    while (*e) e++;
    return put_num(e, "", (ms % 1000) / 100, ctx ? " s" : "S");
}
static int32_t bits_text(char *buf, int32_t value, int32_t ctx, int32_t m)
{
    int32_t v = v127(value);
    (void)m;
    return v ? put_num(buf, "", 12 - v * 9 / 127, ctx ? " bits" : "B") : put(buf, "OFF");
}
static int32_t tun2_text(char *buf, int32_t value, int32_t ctx, int32_t m)
{
    (void)ctx; (void)m;
    return signed_num(buf, semis2_of(v127(value)));
}
static int32_t det_text(char *buf, int32_t value, int32_t ctx, int32_t m)
{
    (void)m;
    return put_num(buf, "", v127(value) * 50 / 127, ctx ? " cents" : "C");
}
static int32_t view_text(char *buf, int32_t value, int32_t ctx, int32_t m)
{
    (void)ctx; (void)m;
    return put(buf, v127(value) < 64 ? "OSC1" : "OSC2");
}

/* ---- the page: graphics (the UI task; y = 0 is the bottom row) -------------------------------------- */

static int32_t shown(int k, int32_t fallback)
{
    int32_t v = cm_src_shown(k);
    return v == CM_NO_VALUE ? fallback : v127(v);
}

/* One wave of a table at a place (8.8 frames), sample i of DW_LEN, as -128..127. */
static int32_t wave_at(int32_t tab, int32_t pos, int32_t step, int32_t i)
{
    int32_t f = pos >> 8, g = f < LAST ? f + 1 : f, r = step_frac(pos & 255, step);
    int32_t a = dw_tab[tab][f][i], b = dw_tab[tab][g][i];
    return a + (((b - a) * r) >> 8);
}

static void dot(void *bmp, int32_t x, int32_t y) { fw_fillrect(bmp, x, y, x, y, 1); }

/* A wave drawn as a line over w columns from (x, y), h rows high: each column joined to the last. */
static void wave_line(void *bmp, int32_t x, int32_t y, int32_t w, int32_t h, int32_t tab, int32_t pos,
                      int32_t step)
{
    int32_t prev = -1;
    for (int32_t c = 0; c < w; c++) {
        int32_t v = wave_at(tab, pos, step, c * DW_LEN / w);
        int32_t r = (v + 128) * (h - 1) / 255;
        int32_t lo = prev < 0 ? r : prev < r ? prev : r, hi = prev < 0 ? r : prev > r ? prev : r;
        fw_fillrect(bmp, x + c, y + lo, x + c, y + hi, 1);
        prev = r;
    }
}

/* A wave as dots only (the frames behind, in the 3D views). */
static void wave_dots(void *bmp, int32_t x, int32_t y, int32_t w, int32_t h, int32_t tab, int32_t frame)
{
    for (int32_t c = 0; c < w; c += 1)
        dot(bmp, x + c, y + (dw_tab[tab][frame][c * DW_LEN / w] + 128) * (h - 1) / 255);
}

/* TABL: the table in 3D, five of its waves stacked back to front. */
static int32_t draw_tabl(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    int32_t tab = table_of(v127(value));
    (void)flag;
    for (int32_t k = 4; k >= 0; k--)
        wave_dots(bmp, x + 1 + k, y + k * 2 + 1, 12, 7, tab, k * LAST / 4);
    return 1;
}

/* VIEW's panel fills G and H on page 2: WAVE sits on G there too, so both spots are real knobs and no
 * empty box is drawn. Whichever of the two draws last draws the whole panel, from where the other was
 * last drawn. The table of the oscillator VIEW names in 3D, eight waves stacked behind, the one it plays
 * drawn whole in front, and each oscillator's place along the bottom (osc 1 the tall mark). */
static int32_t g_x = -1, h_x = -1;                             /* where WAVE (page 2) and VIEW were drawn */

static void draw_panel(void *bmp, int32_t y, int32_t view)
{
    int32_t t1 = table_of(shown(K_TABL, 0)), t2 = table2_of(shown(K_TBL2, 0), t1);
    int32_t step = shown(K_STEP, 0), wave = shown(K_WAVE, 0);
    int32_t ofs = (shown(K_OFS, 64) - 64) * (LAST * 256) / 64;
    int32_t p1 = place(wave, 0), p2 = clampi(p1 + ofs, 0, LAST * 256);
    int32_t two = view >= 64, tab = two ? t2 : t1, pos = two ? p2 : p1;
    int32_t x0 = (g_x >= 0 ? g_x : h_x - 16) + 1, x1 = (h_x >= 0 ? h_x : g_x + 16) + 16;
    int32_t w = x1 - x0 + 1, front = w * 3 / 4;
    fw_fillrect(bmp, x0, y, x1, y + 16, 0);
    for (int32_t k = 7; k >= 0; k--)                           /* the table, back to front */
        wave_dots(bmp, x0 + 2 + k * (w - front - 2) / 7, y + 5 + k, front, 5, tab, k * LAST / 7);
    fw_fillrect(bmp, x0, y + 3, x0 + front, y + 10, 0);        /* the front wave hides what is behind */
    wave_line(bmp, x0, y + 3, front + 1, 8, tab, pos, step);
    fw_fillrect(bmp, x0, y, x1, y, 1);                         /* the bottom line, and the places on it */
    int32_t m1 = x0 + p1 * (w - 1) / (LAST * 256), m2 = x0 + p2 * (w - 1) / (LAST * 256);
    fw_fillrect(bmp, m1, y + 1, m1, y + 2, 1);
    dot(bmp, m2, y + 1);
}

/* WAVE: on page 1 (knob C) the wave the knobs set, with its place marked along the bottom; on page 2
 * (knob G, the right half of the screen) its half of VIEW's panel. */
static int32_t draw_wave(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    int32_t tab = table_of(shown(K_TABL, 0)), step = shown(K_STEP, 0), wave = v127(value);
    (void)flag;
    if (x >= 64) {
        g_x = x;
        draw_panel(bmp, y, shown(K_VIEW, 0));
        return 1;
    }
    wave_line(bmp, x + 1, y + 3, 16, 14, tab, place(wave, 0), step);
    fw_fillrect(bmp, x + 1, y, x + 16, y, 1);
    fw_fillrect(bmp, x + 1 + wave * 15 / 127, y + 1, x + 1 + wave * 15 / 127, y + 1, 1);
    return 1;
}

static int32_t draw_view(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    (void)flag;
    h_x = x;
    draw_panel(bmp, y, v127(value));
    return 1;
}

/* ---- the page ------------------------------------------------------------------------------------- */

#define KNOB(nm, ln, d, f) { .name = nm, .lname = ln, .look = 0x86, .flags = CM_RANGE | CM_DEFAULT, \
                             .max = 0x7f00, .def = (d) << 8, .fmt = f }
#define DRAWN(nm, ln, d, f, dr) { .name = nm, .lname = ln, .look = 0x86, \
                                  .flags = CM_RANGE | CM_DEFAULT | CM_DRAW, .max = 0x7f00, .def = (d) << 8, \
                                  .fmt = f, .draw = dr }

static const struct cm_src_page dw_pages[2] = {
    { "WAVE", { 0, 1, 2, 3, 4, 5, 6, 7 }, 0, { 0, 0, 0 } },
    { "OSC2", { 8, 9, 10, 11, 12, 13, 2, 14 }, 0, { 0, 0, 0 } },
};

static const struct cm_ui32 dw_page = {
    .base = {
        .abi = cm_ui_v32,
        .page_from = 3,
        .group = "DIGIWAVE",
        .knob = {
            [K_TABL] = DRAWN("TABL", "Wavetable", 0, tabl_text, draw_tabl),
            [K_WAVE] = DRAWN("WAVE", "Wave position", 0, 0, draw_wave),
            [K_ENV] = { .name = "ENV", .lname = "Wave envelope", .look = 0x86,
                        .flags = CM_RANGE | CM_DEFAULT | CM_NOT_SAMPLE, .max = 0x7f00, .def = 64 << 8,
                        .fmt = bip_text },
            [K_DEC] = KNOB("DEC", "Wave env decay", 64, dec_text),
            [K_STEP] = KNOB("STEP", "Smooth to stepped", 0, 0),
            [K_BITS] = KNOB("BITS", "Bit crush", 0, bits_text),
        },
    },
    .extra = {
        KNOB("TBL2", "Osc 2 wavetable", 0, tbl2_text),
        KNOB("OFS", "Osc 2 wave offset", 64, bip_text),
        KNOB("TUN2", "Osc 2 tune", 64, tun2_text),
        KNOB("DET", "Osc 2 detune", 0, det_text),
        KNOB("MIX", "Osc 1 / 2 mix", 0, 0),
        KNOB("SUB", "Sub oscillator", 0, 0),
        DRAWN("VIEW", "Wavetable view", 0, view_text, draw_view),
    },
    .page_count = 2,
    .pages = dw_pages,
};

/* The menu's 11 x 7 icon, as the stock ones: a word a column, the top row in bit 25, pixels then mask.
 * Two waves, one behind the other. */
static const uint32_t dw_px[11] = {
    0x20000000, 0x30000000, 0x14000000, 0x16000000, 0x32000000, 0xe2000000,
    0x86000000, 0x8c000000, 0xe8000000, 0x08000000, 0x0c000000,
};
static const uint32_t dw_mask[11] = {
    0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000,
    0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000,
};
static const struct {
    const void *vt;
    int32_t width, height, one;
    const uint32_t *px, *mask;
    int32_t zero;
} dw_icon = { fw_bitmap_vt, 11, 7, 1, dw_px, dw_mask, 0 };

const struct cm_machine dw_machine = {
    DW_ID, "DIGIWAVE", "DWAV", &dw_icon, 3, DW_ID, CM_UI_TAG, (const struct cm_ui *)&dw_page,
};
