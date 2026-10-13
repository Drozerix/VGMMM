// Exporters: VGM 1.50 / 1.60 (optionally gzipped), TFD register dump, text, WAV.
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <zlib.h>

#include "export.h"
#include "player.h"

typedef long double ld;

extern "C" {
unsigned char *vgmcmp_optimize(unsigned char *src, int &size);
void vgmcmp_free(unsigned char *data);
const char *vgmcmp_about(void);
}

const char *g_appVersion = "VGM Music Maker v1.1-qt";
bool g_wavFadeOut = true;

int g_vgmSampleOffset[MAX_SAMPLES];
int g_vgmSampleLen[MAX_SAMPLES];

static uint8_t *s_buf;
static int s_pos;
static int s_size;
static bool s_fm, s_dac, s_psg, s_gg;
static int s_samplesPerTick;

void vgm_put(uint8_t b)
{
    if (s_buf == nullptr)
        return;
    s_buf[s_pos++] = b;
    if (s_size - 0x100 <= s_pos) {
        s_size += 0x10000;
        s_buf = (uint8_t *)realloc(s_buf, s_size);
    }
}

static void vgm_put16(unsigned v)
{
    vgm_put((uint8_t)v);
    vgm_put((uint8_t)(v >> 8));
}

void vgm_put32(uint32_t v)
{
    vgm_put((uint8_t)v);
    vgm_put((uint8_t)(v >> 8));
    vgm_put((uint8_t)(v >> 16));
    vgm_put((uint8_t)(v >> 24));
}

static void vgm_put_str(const char *s)
{
    for (; *s; s++)
        vgm_put((uint8_t)*s);
}

// 16-bit characters plus terminator, as GD3 tags want
static void vgm_put_wstr(const char *s)
{
    for (; *s; s++) {
        vgm_put((uint8_t)*s);
        vgm_put(0);
    }
    vgm_put(0);
    vgm_put(0);
}

static int wstr_size(const char *s)
{
    return (int)strlen(s) * 2 + 2;
}

static void vgm_flush_queue(void)
{
    for (int i = 0; i < g_regqCount; i++) {
        const RegWrite *w = &g_regq[i];
        if (w->type < 2 && s_fm) {
            vgm_put((uint8_t)(w->type + 0x52));
            vgm_put(w->reg);
            vgm_put(w->val);
        }
        if (w->type == REG_PSG && s_psg) {
            vgm_put(0x50);
            vgm_put(w->val);
        }
        if (w->type == REG_GG_STEREO && s_gg) {
            vgm_put(0x4f);
            vgm_put(w->val);
        }
    }
}

static int vgm_put_gd3(int system)
{
    int start = s_pos;
    const char *exporter = "VGM Music Maker Exporter";
    const char *sys = "";
    char date[0x80];
    if (system == 0)
        sys = "Sega Mega Drive / Genesis";
    else if (system == 1)
        sys = "Sega Master System";
    else if (system == 2)
        sys = "Sega Game Gear";
    unsigned d = g_song.dateModified;
    sprintf(date, "%4.4i/%2.2i/%2.2i", (int)(d & 0x7f) + 2000, (int)((d >> 7) & 0xf) + 1, (int)(d >> 11));
    int size = wstr_size(g_song.name) + wstr_size(sys) + wstr_size(g_song.author) + wstr_size(exporter) +
               wstr_size(g_song.message) + wstr_size(date) + 10;
    vgm_put_str("Gd3 ");
    vgm_put32(0x100);
    vgm_put32((uint32_t)size);
    vgm_put_wstr(g_song.name);
    vgm_put_wstr("");
    vgm_put_wstr("");
    vgm_put_wstr("");
    vgm_put_wstr(sys);
    vgm_put_wstr("");
    vgm_put_wstr(g_song.author);
    vgm_put_wstr("");
    vgm_put_wstr(date);
    vgm_put_wstr(exporter);
    vgm_put_wstr(g_song.message);
    return start;
}

static void vgm_wait_tick(void)
{
    if (s_samplesPerTick == 735) {
        vgm_put(0x62);
    } else if (s_samplesPerTick == 882) {
        vgm_put(0x63);
    } else {
        vgm_put(0x61);
        vgm_put16((unsigned)s_samplesPerTick);
    }
}

// VGM 1.50: PCM is written with 0x8n commands, one per DAC sample.
static void vgm_log_wait(int n)
{
    if (g_player.dacSample < 0) {
        vgm_put(0x61);
        vgm_put((uint8_t)n);
        vgm_put((uint8_t)(n / 256));
        return;
    }
    if (g_player.dacSample >= MAX_SAMPLES)
        return;
    int idx = g_song.sample[g_player.dacSample].data;
    if (idx < 0 || g_sampleData[idx].conv == nullptr)
        return;
    if (g_player.dacPos == 0) {
        vgm_put(0xe0);
        vgm_put32((uint32_t)g_vgmSampleOffset[g_player.dacSample]);
        vgm_put(0x52);
        vgm_put(0x2b);
        vgm_put(0x80);
    }
    uint8_t c = g_sampleData[idx].conv[g_player.dacPos++];
    int rest;
    if (n < 0x10) {
        vgm_put((uint8_t)(n + 0x80));
        rest = 0;
    } else {
        vgm_put(0x8f);
        rest = n - 0xf;
    }
    if (c == 0x80) {
        vgm_put(0x52);
        vgm_put(0x2b);
        vgm_put(0);
        g_player.dacSample = -1;
    }
    if (rest != 0) {
        vgm_put(0x61);
        vgm_put((uint8_t)rest);
        vgm_put((uint8_t)(rest / 256));
    }
}

static void vgm_begin(int system, int headerSize)
{
    s_fm = s_dac = s_psg = s_gg = false;
    if (system == 0) {
        s_fm = s_dac = s_psg = true;
    } else if (system == 1) {
        s_psg = true;
    } else if (system == 2) {
        s_psg = true;
        s_gg = true;
    }
    s_samplesPerTick = (int)(44100u / g_song.fps);
    s_pos = headerSize;
    s_size = 0x10000;
    s_buf = (uint8_t *)malloc(0x10000);
    memset(s_buf, 0, headerSize);
    if (!s_dac)
        return;
    // PCM data block with all samples, each followed by 16 bytes of silence
    samples_convert_all();
    int total = 0;
    for (int i = 0; i < MAX_SAMPLES; i++) {
        if (!sample_is_empty(&g_song.sample[i])) {
            const SampleData *d = &g_sampleData[g_song.sample[i].data];
            if (d->conv != nullptr) {
                g_vgmSampleOffset[i] = total;
                g_vgmSampleLen[i] = d->convLen;
                total += d->convLen + 0x10;
            }
        } else {
            g_vgmSampleOffset[i] = -1;
            g_vgmSampleLen[i] = 0;
        }
    }
    vgm_put(0x67);
    vgm_put(0x66);
    vgm_put(0);
    vgm_put32((uint32_t)total);
    for (int i = 0; i < MAX_SAMPLES; i++) {
        if (!sample_is_empty(&g_song.sample[i])) {
            const SampleData *d = &g_sampleData[g_song.sample[i].data];
            for (int k = 0; k < d->convLen; k++)
                vgm_put(d->conv[k]);
            for (int k = 0; k < 0x10; k++)
                vgm_put(0x80);
        }
    }
}

static bool vgm_finish(const char *filename, int system, bool gzip, bool optimise, int version, int ticks,
                       int loopPos, int loopTicks)
{
    vgm_put(0x66);
    int gd3 = vgm_put_gd3(system);
    int size = s_pos;
    s_pos = 0;
    vgm_put_str("Vgm ");
    vgm_put32((uint32_t)(size - 4));
    vgm_put32((uint32_t)version);
    vgm_put32(s_psg ? g_song.psgClock : 0);
    vgm_put32(0);
    vgm_put32((uint32_t)(gd3 - 0x14));
    vgm_put32((uint32_t)(ticks * 882));         // (sic: always 882 samples per tick)
    vgm_put32((uint32_t)(loopPos - 0x1c));
    vgm_put32((uint32_t)(loopTicks * 882));
    vgm_put32(g_song.fps);
    vgm_put16(s_psg ? 9 : 0);                   // SN76489 feedback
    vgm_put16(s_psg ? 0x10 : 0);                // shift register width
    vgm_put32(s_fm ? g_song.fmClock : 0);
    vgm_put32(0);
    if (version == 0x150) {
        vgm_put32(0xc);
    } else {
        vgm_put32(0x4c);
        for (int i = 0; i < 0x12; i++)
            vgm_put32(0);
    }
    uint8_t *data = s_buf;
    s_buf = nullptr;
    bool fromOptimiser = false;
    if (optimise) {
        uint8_t *opt = vgmcmp_optimize(data, size);
        free(data);
        data = opt;
        fromOptimiser = true;
    }
    bool ok = false;
    FILE *f = fopen(filename, "wb");
    if (f != nullptr) {
        if (!gzip) {
            fwrite(data, size, 1, f);
            ok = true;
        } else {
            uLong cap = (uLong)size * 2 + 64;
            uint8_t *z = (uint8_t *)malloc(cap);
            z_stream zs;
            memset(&zs, 0, sizeof(zs));
            if (deflateInit2(&zs, 9, Z_DEFLATED, 0x1f, 8, Z_DEFAULT_STRATEGY) == Z_OK) {
                zs.avail_in = (uInt)size;
                zs.next_in = data;
                zs.next_out = z;
                zs.avail_out = (uInt)cap;
                if (deflate(&zs, Z_FINISH) != Z_STREAM_ERROR) {
                    fwrite(z, cap - zs.avail_out, 1, f);
                    ok = true;
                }
                deflateEnd(&zs);
            }
            free(z);
        }
        fclose(f);
    }
    if (fromOptimiser)
        vgmcmp_free(data);
    else
        free(data);
    return ok;
}

static bool export_vgm150(const char *filename, int system, bool gzip, bool optimise)
{
    vgm_begin(system, 0x40);
    player_start(0, 0, 0, 0, 0, -1);
    vgm_flush_queue();
    int ticks = 0, loopTicks = 0;
    bool looped = false;
    int loopPos = INT_MAX;      // (uninitialised in the original until the loop point is reached)
    float acc = 0.0f;
    float step = (float)((ld)(1.0 / 44100.0) * (ld)(int)g_song.sampleRate);
    for (;;) {
        if (player_tick(true)) {
            if (looped)
                break;
            looped = true;
            loopPos = s_pos;
        }
        ticks++;
        if (loopPos <= s_pos)
            loopTicks++;
        vgm_flush_queue();
        if (!s_dac) {
            vgm_wait_tick();
            continue;
        }
        int n = 0;
        for (int i = 0; i < s_samplesPerTick; i++) {
            acc = (float)((ld)step + (ld)acc);
            if (1.0f <= acc) {
                vgm_log_wait(n);
                acc = (float)((ld)acc - (ld)1.0);
                n = 0;
                if (g_player.dacSample < 0) {
                    n = s_samplesPerTick - i;
                    break;
                }
            }
            n++;
        }
        if (n != 0) {
            if (n < 0x10) {
                vgm_put((uint8_t)(n + 0x80));
            } else {
                int m = n - 0xf;
                vgm_put(0x8f);
                vgm_put(0x61);
                vgm_put((uint8_t)m);
                vgm_put((uint8_t)(m / 256));
            }
        }
    }
    player_stop();
    return vgm_finish(filename, system, gzip, optimise, 0x150, ticks, loopPos, loopTicks);
}

static bool export_vgm160(const char *filename, int system, bool gzip, bool optimise)
{
    vgm_begin(system, 0x80);
    // DAC stream 0: YM2612 register 0x2A, data bank 0, at the module's sample rate
    static const uint8_t setup[12] = {0x90, 0, 2, 0, 0x2a, 0x91, 0, 0, 1, 0, 0x92, 0};
    for (int i = 0; i < 12; i++)
        vgm_put(setup[i]);
    vgm_put32(g_song.sampleRate);
    player_start(0, 0, 0, 0, 0, -1);
    dac_set_log_mode(1);
    vgm_flush_queue();
    int ticks = 0, loopTicks = 0;
    bool looped = false;
    int loopPos = INT_MAX;
    for (;;) {
        if (player_tick(true)) {
            if (looped)
                break;
            looped = true;
            loopPos = s_pos;
        }
        ticks++;
        if (loopPos <= s_pos)
            loopTicks++;
        vgm_flush_queue();
        vgm_wait_tick();
    }
    player_stop();
    dac_stop();
    dac_set_log_mode(0);
    return vgm_finish(filename, system, gzip, optimise, 0x160, ticks, loopPos, loopTicks);
}

bool export_vgm(const char *filename, int system, bool gzip, bool v150, bool optimise)
{
    if (g_song.fps == 0)
        return false;
    song_calc_timing();
    return v150 ? export_vgm150(filename, system, gzip, optimise) : export_vgm160(filename, system, gzip, optimise);
}

// ---------------------------------------------------------------- TFD (FM register dump)

static uint8_t *s_tfd;
static int s_tfdPos, s_tfdSize;
static int s_tfdCache[2][0x100];

static void tfd_put(uint8_t b)
{
    s_tfd[s_tfdPos++] = b;
    if (s_tfdSize - 0x10 < s_tfdPos) {
        s_tfdSize += 0x10000;
        s_tfd = (uint8_t *)realloc(s_tfd, s_tfdSize);
    }
}

static void tfd_put_str(const char *s)
{
    for (; *s; s++)
        tfd_put((uint8_t)*s);
}

static void tfd_cache_reset(void)
{
    for (int i = 0; i < 0x100; i++) {
        s_tfdCache[0][i] = -1;
        s_tfdCache[1][i] = -1;
    }
}

// Writes the FM register changes of the current tick; 0xff starts a frame, 0xfd switches to
// the second register bank.
static void tfd_frame(void)
{
    int n = 0;
    for (int i = 0; i < g_regqCount; i++) {
        RegWrite w = g_regq[i];
        if (w.type >= 2)
            continue;
        if (w.reg == 0x28) {            // key on/off: channels 4-6 go to the second bank
            w.type = (w.val & 4) != 0;
            w.val &= 0xf3;
        }
        g_regq[n++] = w;
    }
    g_regqCount = n;
    // both halves of a frequency register pair must be rewritten when either changes
    for (unsigned bank = 0; bank < 2; bank++) {
        int *cache = s_tfdCache[bank];
        for (int i = 0; i < g_regqCount; i++) {
            const RegWrite *w = &g_regq[i];
            if (bank == w->type && cache[w->reg] != (int)w->val) {
                for (int base = 0xa0; base <= 0xa3; base++) {
                    if (w->reg == base || w->reg == base + 4) {
                        cache[base] = -1;
                        cache[base + 4] = -1;
                    }
                }
                for (int base = 0xa8; base <= 0xaa; base++) {
                    if (w->reg == base || w->reg == base + 4) {
                        cache[base] = -1;
                        cache[base + 4] = -1;
                    }
                }
            }
        }
    }
    tfd_put(0xff);
    bool secondBank = false;
    for (unsigned bank = 0; bank < 2; bank++) {
        int *cache = s_tfdCache[bank];
        for (int i = 0; i < g_regqCount; i++) {
            const RegWrite *w = &g_regq[i];
            if (bank == w->type) {
                if (cache[w->reg] != (int)w->val || w->reg == 0x28) {
                    tfd_put(w->reg);
                    tfd_put(w->val);
                    cache[w->reg] = w->val;
                }
            } else if (bank == 0) {
                secondBank = true;
            }
        }
        if (bank == 0 && secondBank)
            tfd_put(0xfd);
    }
}

// Run of empty frames: 1-2 stay as 0xff bytes, longer runs become 0xfe <count-3>.
static void tfd_put_run(uint8_t *out, int *pos, int n)
{
    for (;;) {
        if (n < 1)
            return;
        while (n < 0x102) {
            if (n < 3) {
                for (int i = 0; i < n; i++)
                    out[(*pos)++] = 0xff;
                n = 0;
            } else {
                out[(*pos)++] = 0xfe;
                out[(*pos)++] = (uint8_t)(n - 3);
                n = 0;
            }
            if (n < 1)
                return;
        }
        out[(*pos)++] = 0xfe;
        out[(*pos)++] = 0xff;
        n -= 0x102;
    }
}

bool export_tfd(const char *filename)
{
    if (g_song.fps == 0)
        return false;
    song_calc_timing();
    s_tfdPos = 0;
    s_tfdSize = 0x10000;
    s_tfd = (uint8_t *)malloc(0x10000);
    tfd_put_str("TFMD");
    tfd_put_str(g_song.name);
    tfd_put(0);
    tfd_put_str(g_song.author);
    tfd_put(0);
    tfd_put_str(g_song.message);
    tfd_put(0);
    player_start(0, 0, 0, 0, 0, -1);
    tfd_cache_reset();
    tfd_frame();
    bool looped = false;
    for (;;) {
        if (player_tick(true)) {
            if (looped)
                break;
            tfd_cache_reset();
            tfd_put(0xfa);              // loop point
            looped = true;
        }
        tfd_frame();
    }
    tfd_put(0xfb);                      // end
    player_stop();

    int size = s_tfdPos;
    uint8_t *out = (uint8_t *)malloc((size_t)size + 16);
    int hdr = (int)(strlen(g_song.name) + strlen(g_song.author) + strlen(g_song.message)) + 7;
    memcpy(out, s_tfd, hdr);
    int o = hdr, run = 0;
    for (int i = hdr; i < size;) {
        uint8_t b = s_tfd[i];
        if (b >= 0xfa && b <= 0xfd) {
            tfd_put_run(out, &o, run);
            run = 0;
            out[o++] = b;
            i++;
        } else if (b == 0xff) {
            run++;
            i++;
        } else {
            tfd_put_run(out, &o, run);
            run = 0;
            out[o++] = s_tfd[i];
            out[o++] = s_tfd[i + 1];
            i += 2;
        }
    }
    free(s_tfd);
    s_tfd = nullptr;
    FILE *f = fopen(filename, "wb");
    if (f != nullptr) {
        fwrite(out, o, 1, f);
        fclose(f);
    }
    free(out);
    return f != nullptr;
}

// ---------------------------------------------------------------- text

static char hex_char(int v)
{
    return (char)(v < 10 ? v + '0' : v + '7');
}

bool export_text(const char *filename)
{
    static const char *noteNames[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    static const char *noiseNames[4] = {"Hi ", "Mid", "Low", "Ch3"};
    FILE *f = fopen(filename, "w");
    if (f == nullptr)
        return false;
    fprintf(f, "[Info]\n\n");
    fprintf(f, "Version=%s\n\n", g_appVersion);
    fprintf(f, "Track=%s\n", g_song.name);
    fprintf(f, "Author=%s\n", g_song.author);
    fprintf(f, "Message=%s\n\n", g_song.message);
    fprintf(f, "FM clock=%i\n", (int)g_song.fmClock);
    fprintf(f, "PSG clock=%i\n", (int)g_song.psgClock);
    fprintf(f, "FPS=%i\n\n", (int)g_song.fps);
    fprintf(f, "[Settings]\n\n");
    fprintf(f, "SpeedEven=%i\n", (int)g_song.speedEven);
    fprintf(f, "SpeedOdd=%i\n", (int)g_song.speedOdd);
    fprintf(f, "SpeedInterleave=%i\n\n", (int)g_song.interleave);
    fprintf(f, "[Instruments]\n\n");
    int used = 0;
    for (int i = 0; i < MAX_INSTRUMENTS; i++)
        if (!ins_is_empty(&g_song.ins[i]))
            used++;
    fprintf(f, "InstrumentsUsed=%i\n\n", used);
    for (int i = 0; i < MAX_INSTRUMENTS; i++) {
        const Instrument *ins = &g_song.ins[i];
        if (ins_is_empty(ins))
            continue;
        fprintf(f, "[Instrument%i]\n\n", i);
        fprintf(f, "Name=%s\n", ins->name);
        fprintf(f, "Algorithm=%i\n", ins->alg);
        fprintf(f, "Feedback=%i\n", ins->fb);
        fprintf(f, "AMS=%i\n", ins->ams);
        fprintf(f, "FMS=%i\n\n", ins->fms);
        for (int o = 0; o < 4; o++) {
            const Operator *op = &ins->op[o];
            int n = o + 1;
            fprintf(f, "Op%iAR=%i\n", n, op->ar);
            fprintf(f, "Op%iDR=%i\n", n, op->dr & 0x1f);
            fprintf(f, "Op%iSR=%i\n", n, op->sr);
            fprintf(f, "Op%iRR=%i\n", n, op->rr);
            fprintf(f, "Op%iTL=%i\n", n, op->tl);
            fprintf(f, "Op%iSL=%i\n", n, op->sl);
            fprintf(f, "Op%iMULT=%i\n", n, op->mult);
            fprintf(f, "Op%iDT=%i\n", n, (int)op->dt);
            fprintf(f, "Op%iRS=%i\n", n, op->rs);
            fprintf(f, "Op%iEG=%i\n", n, op->ssg);
            fprintf(f, "Op%iAM=%i\n\n", n, (op->dr & 0xe0) != 0);
        }
    }
    fprintf(f, "[Order list]\n\n");
    fprintf(f, "OrderLen=%i\n", (int)(int16_t)g_song.orderLen);
    fprintf(f, "OrderLoop=%i\n", (int)(int16_t)g_song.loopPos);
    fprintf(f, "OrderPositions=");
    char usedPat[0x100];
    memset(usedPat, 0, sizeof(usedPat));
    for (int i = 0; i < g_song.orderLen; i++) {
        fprintf(f, "%i%c", g_song.order[i].pattern, i < g_song.orderLen - 1 ? ',' : '\n');
        usedPat[g_song.order[i].pattern] = 1;
    }
    fprintf(f, "\n");
    fprintf(f, "[Patterns]\n\n");
    for (int p = 0; p < MAX_PATTERNS; p++) {
        if (!usedPat[p])
            continue;
        const Pattern *pat = &g_song.pattern[p];
        fprintf(f, "[Pattern%i]\n\n", p);
        fprintf(f, "Length=%i\n\n", pat->length);
        for (int row = 0; row < pat->length; row++) {
            char line[0x200];
            line[0] = 0;
            for (int ch = 0; ch < CHANNELS; ch++) {
                const Cell *c = &pat->cell[row][ch];
                char s[0x20];
                memset(s, '.', sizeof(s));
                if (c->note != NOTE_EMPTY) {
                    if (c->note == NOTE_OFF) {
                        s[0] = 'R';
                    } else if (ch < 9) {
                        const char *nm = noteNames[(unsigned)c->note % 12];
                        s[0] = nm[0];
                        s[1] = nm[1];
                        s[2] = (char)(c->octave + '0');
                    } else {
                        const char *nm = noiseNames[c->note & 3];
                        s[0] = nm[0];
                        s[1] = nm[1];
                        s[2] = nm[2];
                    }
                }
                if (c->volume != 0) {
                    s[3] = hex_char(c->volume >> 4);
                    s[4] = hex_char(c->volume & 0xf);
                }
                if (c->ins != 0) {
                    s[5] = hex_char(c->ins >> 4);
                    s[6] = hex_char(c->ins & 0xf);
                }
                for (int k = 0; k < 4; k++) {
                    if (c->fx[k] != 0 || c->param[k] != 0) {
                        s[7 + k * 3] = hex_char((int)(int8_t)c->fx[k]);
                        s[8 + k * 3] = hex_char(c->param[k] >> 4);
                        s[9 + k * 3] = hex_char(c->param[k] & 0xf);
                    }
                }
                if (ch < 9) {
                    s[19] = '|';
                    s[20] = 0;
                } else {
                    s[19] = 0;
                }
                strcat(line, s);
            }
            fprintf(f, "%s\n", line);
        }
        fprintf(f, "\n");
    }
    fclose(f);
    return true;
}

// ---------------------------------------------------------------- WAV

bool export_wav(const char *filename, bool allChannels, bool (*progress)(int percent, void *user), void *user)
{
    static const uint8_t header[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
                                       0x10, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0x10, 0,
                                       'd', 'a', 't', 'a', 0, 0, 0, 0};
    if (g_song.fps == 0)
        return false;
    int rate = (int)g_outputRate;
    int fade = g_wavFadeOut ? 3 : 0;
    int ticks = song_calc_timing();
    int frames = (int)((uint32_t)((int64_t)(int)(ticks + fade * (int)g_song.fps) * (int64_t)rate) / g_song.fps);
    int bytes = frames * 4;
    uint8_t hdr[44];
    memcpy(hdr, header, sizeof(hdr));
    wr32(hdr + 4, (unsigned)(bytes + 0x24));
    wr32(hdr + 0x18, (unsigned)rate);
    wr32(hdr + 0x1c, (unsigned)(rate * 4));
    wr32(hdr + 0x28, (unsigned)bytes);
    FILE *f = fopen(filename, "wb");
    if (f == nullptr)
        return false;
    int16_t *buf = (int16_t *)malloc(0x8000);
    if (buf == nullptr) {
        fclose(f);
        return false;
    }
    fwrite(hdr, 0x2c, 1, f);
    chips_render_mode(true, allChannels);
    g_frameCounter = 0;
    player_start(0, 0, 0, 0, 0, -1);
    float vol = 1.0f;
    float volStep = fade == 0 ? 1.0f : (float)((ld)1.0 / (ld)(float)(fade * rate));
    int fadeStart = frames - fade * rate;
    float prog = 0.0f, lastProg = -1.0f;
    float progStep = (float)((ld)100.0 / (ld)(float)(frames / 0x2000));
    bool cancelled = false;
    for (int done = 0; done < frames; done += 0x2000) {
        if (progress != nullptr && (int)(ld)prog != (int)(ld)lastProg) {
            if (progress((int)prog, user)) {
                cancelled = true;
                break;
            }
            lastProg = prog;
        }
        audio_render(buf, 0x2000);
        if (fadeStart - 0x2000 < done) {
            for (int i = 0; i < 0x4000; i++) {
                buf[i] = (int16_t)(int)((ld)buf[i] * (ld)vol);
                vol = (float)((ld)vol - (ld)volStep);
                if (vol < 0.0f)
                    vol = 0.0f;
            }
        }
        fwrite(buf, 0x8000, 1, f);
        prog = (float)((ld)progStep + (ld)prog);
    }
    fclose(f);
    if (!cancelled && progress != nullptr)
        progress(100, user);
    player_stop();
    chips_render_mode(false, false);
    free(buf);
    return true;
}

const char *export_about_vgmcmp(void)
{
    return vgmcmp_about();
}
