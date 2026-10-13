// Importers for TFM Music Maker modules (.tfe) and Amiga MOD files.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "export.h"
#include "song.h"

static uint8_t *read_file(const char *filename, int *size)
{
    FILE *f = fopen(filename, "rb");
    if (f == nullptr)
        return nullptr;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(n > 0 ? n : 1);
    if (buf != nullptr && fread(buf, 1, n, f) != (size_t)n)
        n = 0;
    fclose(f);
    *size = (int)n;
    return buf;
}

// TFM Music Maker had per-channel panning as effect E8x; VGM MM uses effect 19.
static void tfm_convert_panning(void)
{
    for (int p = 0; p < MAX_PATTERNS; p++) {
        for (int row = 0; row < MAX_ROWS; row++) {
            for (int ch = 0; ch < FM_CHANNELS; ch++) {
                Cell *c = &g_song.pattern[p].cell[row][ch];
                for (int f = 0; f < 4; f++) {
                    if (c->fx[f] == 0x0e && (c->param[f] & 0xf0) == 0x80) {
                        c->fx[f] = 0x19;
                        if ((c->param[f] & 0xf) == 1)
                            c->param[f] = 0x10;
                        else if ((c->param[f] & 0xf) == 2)
                            c->param[f] = 0x01;
                        else
                            c->param[f] = 0x11;
                    }
                }
            }
        }
    }
}

static void read_note_column(Packer *pk, Pattern *pat, int ch)
{
    for (int row = 0; row < MAX_ROWS; row++) {
        Cell *c = &pat->cell[row][ch];
        unsigned n = pack_read_byte(pk) ^ 0xff;
        if (n < 0x80) {
            c->note = (int16_t)(n % 0xc);
            c->octave = (int8_t)(n / 0xc);
        } else {
            c->note = (int16_t)n;
            c->octave = 0;
        }
    }
    for (int row = 0; row < MAX_ROWS; row++)
        pat->cell[row][ch].volume = (int8_t)pack_read_byte(pk);
    for (int row = 0; row < MAX_ROWS; row++)
        pat->cell[row][ch].ins = pack_read_byte(pk);
}

static void fix_empty_name(char *name)
{
    if ((uint8_t)name[0] == 0xff) {
        memset(name, 0, 0x10);
        memcpy(name, "empty", 6);
    }
}

// "TFMfmtV2": the newer TFM Music Maker format (6 FM channels, 4 effect columns)
static bool tfm_load_v2(uint8_t *buf, int size)
{
    if (size < 8 || memcmp(buf, "TFMfmtV2", 8) != 0)
        return false;
    Packer pk;
    pack_init(&pk, buf + 8, size - 8, true);
    g_song.speedEven = pack_read_byte(&pk);
    g_song.speedOdd = pack_read_byte(&pk);
    g_song.interleave = pack_read_byte(&pk);
    g_song.orderLen = pack_read_byte(&pk);
    if (g_song.orderLen == 0)
        g_song.orderLen = 0x100;
    g_song.loopPos = pack_read_byte(&pk);
    g_song.dateCreated = (uint16_t)pack_read16(&pk);
    g_song.dateModified = (uint16_t)pack_read16(&pk);
    g_song.saveCount = (uint16_t)pack_read16(&pk);
    pack_read_bytes(&pk, g_song.author, 0x40);
    pack_read_bytes(&pk, g_song.name, 0x40);
    pack_read_bytes(&pk, g_song.message, 0x180);
    for (int i = 0; i < MAX_ORDERS; i++)
        g_song.order[i].pattern = pack_read_byte(&pk);
    for (int i = 0; i < MAX_INSTRUMENTS; i++) {
        pack_read_bytes(&pk, g_song.ins[i].name, 0x10);
        fix_empty_name(g_song.ins[i].name);
    }
    for (int i = 0; i < MAX_INSTRUMENTS; i++) {
        Instrument *ins = &g_song.ins[i];
        ins->alg = pack_read_byte(&pk);
        ins->fb = pack_read_byte(&pk);
        for (int o = 0; o < 4; o++) {
            Operator *op = &ins->op[o];
            op->mult = pack_read_byte(&pk);
            op->dt = (int8_t)pack_read_byte(&pk);
            op->tl = (pack_read_byte(&pk) ^ 0x7f) & 0x7f;
            op->rs = pack_read_byte(&pk) & 3;
            op->ar = (pack_read_byte(&pk) ^ 0x1f) & 0x1f;
            op->dr = (pack_read_byte(&pk) ^ 0x1f) & 0x1f;
            op->sr = (pack_read_byte(&pk) ^ 0x1f) & 0x1f;
            op->rr = (pack_read_byte(&pk) ^ 0xf) & 0x1f;
            op->sl = pack_read_byte(&pk);
            op->ssg = pack_read_byte(&pk);
        }
    }
    for (int i = 0; i < MAX_PATTERNS; i++) {
        unsigned l = pack_read_byte(&pk);
        g_song.pattern[i].length = l == 0 ? 0x100 : (int)l;
    }
    for (int i = 0; i < MAX_PATTERNS; i++) {
        Pattern *pat = &g_song.pattern[i];
        for (int ch = 0; ch < FM_CHANNELS; ch++) {
            read_note_column(&pk, pat, ch);
            for (int f = 0; f < 4; f++) {
                for (int row = 0; row < MAX_ROWS; row++)
                    pat->cell[row][ch].fx[f] = pack_read_byte(&pk);
                for (int row = 0; row < MAX_ROWS; row++)
                    pat->cell[row][ch].param[f] = pack_read_byte(&pk);
            }
        }
    }
    if (abs(pk.pos - (size - 8)) > 0)
        return false;
    tfm_convert_panning();
    return true;
}

// The original TFM Music Maker format: no signature, one effect column.
static int tfm_load_v1(const char *filename)
{
    int size;
    uint8_t *buf = read_file(filename, &size);
    if (buf == nullptr)
        return 1;
    song_new();
    Packer pk;
    pack_init(&pk, buf, size, true);
    uint8_t b = pack_read_byte(&pk);
    g_song.speedEven = b >> 4;
    g_song.speedOdd = b & 0xf;
    g_song.interleave = pack_read_byte(&pk);
    g_song.orderLen = pack_read_byte(&pk);
    if (g_song.orderLen == 0)
        g_song.orderLen = 0x100;
    g_song.loopPos = pack_read_byte(&pk);
    unsigned created = pack_read16(&pk);
    unsigned modified = pack_read16(&pk);
    if (!((created & 0x7f) < 100 && ((created >> 7) & 0xf) < 0xc && (modified & 0x7f) < 100 &&
          ((modified >> 7) & 0xf) < 0xc)) {
        free(buf);
        song_new();
        return 2;
    }
    g_song.dateCreated = (uint16_t)created;
    g_song.dateModified = (uint16_t)modified;
    g_song.saveCount = (uint16_t)pack_read16(&pk);
    pack_read_bytes(&pk, g_song.author, 0x40);
    pack_read_bytes(&pk, g_song.name, 0x40);
    pack_read_bytes(&pk, g_song.message, 0x180);
    for (int i = 0; i < MAX_ORDERS; i++)
        g_song.order[i].pattern = pack_read_byte(&pk);
    char named[MAX_INSTRUMENTS];
    for (int i = 0; i < MAX_INSTRUMENTS; i++) {
        pack_read_bytes(&pk, g_song.ins[i].name, 0x10);
        named[i] = (uint8_t)g_song.ins[i].name[0] != 0xff;
    }
    for (int i = 0; i < MAX_INSTRUMENTS; i++) {
        Instrument *ins = &g_song.ins[i];
        ins->alg = pack_read_byte(&pk);
        ins->fb = pack_read_byte(&pk);
        for (int o = 0; o < 4; o++) {
            Operator *op = &ins->op[o];
            op->mult = pack_read_byte(&pk);
            op->dt = (int8_t)pack_read_byte(&pk);
            op->tl = pack_read_byte(&pk) ^ 0x7f;
            op->rs = pack_read_byte(&pk);
            op->ar = pack_read_byte(&pk);
            op->dr = pack_read_byte(&pk) & 0x1f;
            op->sr = pack_read_byte(&pk);
            op->rr = pack_read_byte(&pk);
            op->sl = pack_read_byte(&pk);
            op->ssg = pack_read_byte(&pk);
        }
        if (!named[i]) {
            int sum = ins->alg + ins->fb;
            for (int o = 0; o < 4; o++) {
                const Operator *op = &ins->op[o];
                sum += op->ar + op->dr + op->sr + op->rr + (op->tl ^ 0x7f) + op->sl + op->mult + (int)op->dt +
                       op->ssg;
            }
            if (sum == 0) {
                ins_clear(ins);
            } else {
                memset(ins->name, 0, 0x10);
                memcpy(ins->name, "empty", 6);
            }
        }
    }
    for (int i = 0; i < MAX_PATTERNS; i++) {
        unsigned l = pack_read_byte(&pk);
        g_song.pattern[i].length = l == 0 ? 0x100 : (int)l;
    }
    for (int i = 0; i < MAX_PATTERNS; i++) {
        Pattern *pat = &g_song.pattern[i];
        for (int ch = 0; ch < FM_CHANNELS; ch++) {
            read_note_column(&pk, pat, ch);
            for (int row = 0; row < MAX_ROWS; row++)
                pat->cell[row][ch].fx[0] = pack_read_byte(&pk);
            for (int row = 0; row < MAX_ROWS; row++)
                pat->cell[row][ch].param[0] = pack_read_byte(&pk);
        }
        // the old "Bxy" set two channel 3 operator offsets at once; spread it over the columns
        for (int ch = 0; ch < FM_CHANNELS; ch++) {
            for (int row = 0; row < MAX_ROWS; row++) {
                Cell *c = &pat->cell[row][ch];
                if (c->fx[0] == 0x0b && c->param[0] != 0) {
                    c->fx[1] = 0x0b;
                    c->fx[2] = 0x0b;
                    c->fx[3] = 0x0b;
                    c->param[1] = (uint8_t)((c->param[0] >> 4) | 0x10);
                    c->param[2] = (uint8_t)((c->param[0] & 0xf) | 0x20);
                    c->param[3] = 0x3c;
                    c->param[0] = 1;
                }
            }
        }
    }
    free(buf);
    if (abs(pk.pos - size) < 2) {
        tfm_convert_panning();
        return 0;
    }
    song_new();
    return 2;
}

bool import_tfm(const char *filename)
{
    int size;
    uint8_t *buf = read_file(filename, &size);
    if (buf != nullptr) {
        song_new();
        bool ok = tfm_load_v2(buf, size);
        free(buf);
        if (ok)
            return true;
        song_new();
    }
    return tfm_load_v1(filename) == 0;
}

// ---------------------------------------------------------------- MOD

bool import_mod(const char *filename)
{
    static const int periods[85] = {
        3424, 3232, 3048, 2880, 2712, 2560, 2416, 2280, 2152, 2032, 1920, 1812, 1712, 1616, 1524, 1440, 1356,
        1280, 1208, 1140, 1076, 1016, 960,  906,  856,  808,  762,  720,  678,  640,  604,  570,  538,  508,
        480,  453,  428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240,  226,  214,  202,  190,
        180,  170,  160,  151,  143,  135,  127,  120,  113,  107,  101,  95,   90,   85,   80,   75,   71,
        67,   63,   60,   56,   53,   50,   47,   45,   42,   40,   37,   35,   33,   31,   30,   28,   0};
    int size;
    uint8_t *file = read_file(filename, &size);
    if (file == nullptr)
        return false;
    if (size < 600) {
        free(file);
        return false;
    }
    // padded copy: offsets derived from the file cannot run past the buffer
    uint8_t *buf = (uint8_t *)calloc((size_t)size + 0x10000, 1);
    memcpy(buf, file, size);
    free(file);
    auto at = [&](long off) -> const uint8_t * {
        static const uint8_t zero[8] = {0};
        return off >= 0 && off + 4 <= (long)size + 0x10000 ? buf + off : zero;
    };

    song_new();
    int channels = -1;
    int nsamples;
    if (buf[0x438] < 0x20) {
        channels = 4;
        nsamples = 0xf;
    } else {
        nsamples = 0x1f;
        const uint8_t *tag = buf + 0x438;
        auto digit = [](uint8_t c) { return c > 0x2f && c < 0x3a; };
        if (memcmp(tag + 1, "CHN", 3) == 0 && digit(tag[0]))
            channels = tag[0] - 0x30;
        if (channels < 0 && tag[0] == 'M' && tag[2] == 'K' &&
            ((tag[1] == '.' && tag[3] == '.') || (tag[1] == '!' && tag[3] == '!')))
            channels = 4;
        if (channels < 0 && memcmp(tag, "FLT", 3) == 0 && digit(tag[3]))
            channels = tag[3] - 0x30;
        if (channels < 0 && memcmp(tag, "CH", 2) == 0 && digit(tag[2]) && digit(tag[3]))
            channels = tag[2] * 10 + tag[3] - 0x210;
        if (channels < 0 && memcmp(tag, "OKTA", 4) == 0)
            channels = 8;
    }
    if (channels < 0) {
        free(buf);
        return false;
    }
    memcpy(g_song.name, buf, 0x14);
    g_song.name[0x14] = 0;
    const char *base = filename;
    for (const char *p = filename; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    snprintf(g_song.message, sizeof(g_song.message), "Imported from file '%s'", base);

    int off = 0x14;
    for (int i = 0; i < nsamples; i++) {
        const uint8_t *len = buf + 0x2a + i * 0x1e;
        if (len[0] * 0x100 + len[1] > 1) {
            memcpy(g_song.ins[i].name, buf + off, 0x10);
            g_song.ins[i].name[0x10] = 0;
        }
        off += 0x1e;
    }
    g_song.orderLen = buf[off];
    g_song.loopPos = buf[off + 1];
    off += 2;
    if (g_song.orderLen <= g_song.loopPos)
        g_song.loopPos = 0;
    int npat = -1;
    for (int i = 0; i < (int16_t)g_song.orderLen; i++) {
        uint8_t b = buf[off + i];
        g_song.order[i].pattern = b;
        if (npat < b)
            npat = b;
    }
    npat++;
    int dataStart = nsamples == 0xf ? 600 : 0x43c;

    for (int p = 0; p < npat; p++) {
        Pattern *pat = &g_song.pattern[p];
        pat->length = 0x40;
        long pos = (long)p * 0x100 * channels + dataStart;
        for (int row = 0; row < 0x40; row++) {
            for (int ch = 0; ch < channels; ch++, pos += 4) {
                if (ch >= CHANNELS)
                    continue;
                const uint8_t *d = at(pos);
                Cell *cell = &pat->cell[row][ch];
                int period = (d[0] & 0xf) << 8 | d[1];
                unsigned fx = d[2] & 0xf;
                unsigned smp = (d[0] & 0xf0) | (d[2] >> 4);
                unsigned param = d[3];
                cell->ins = (int16_t)smp;
                if (period == 0) {
                    cell->note = NOTE_EMPTY;
                } else {
                    for (int k = 0; k < 0x54; k++) {
                        if (period <= periods[k] && periods[k + 1] < period) {
                            int oct = k / 12 + 1;
                            cell->octave = (int8_t)(oct > 7 ? 7 : oct);
                            cell->note = (int16_t)(k % 12);
                            break;
                        }
                    }
                }
                if (fx != 0xc && smp != 0) {
                    uint8_t v = (uint8_t)(*at((long)smp * 0x1e + 0x2d) >> 1);
                    cell->volume = (int8_t)(v > 0x1f ? 0x1f : v);
                }
                switch (fx) {
                case 0x0:
                case 0x1:
                case 0x2:
                case 0x3:
                case 0x4:
                case 0xa:
                    cell->fx[0] = (uint8_t)fx;
                    cell->param[0] = (uint8_t)param;
                    break;
                case 0x8:       // panning
                    cell->fx[0] = 0x0e;
                    if (param == 0xa4) {
                        cell->param[0] = 0x80;
                    } else {
                        if (param < 0x30)
                            cell->param[0] = 0x81;
                        else if (param < 0x50)
                            cell->param[0] = 0x80;
                        if (param > 0x4f)
                            cell->param[0] = 0x82;
                    }
                    break;
                case 0xb:       // position jump / pattern break end the pattern here
                case 0xd:
                    pat->length = row + 1;
                    row = 0x40;
                    break;
                case 0xc: {
                    uint8_t v = (uint8_t)(param >> 1);
                    cell->volume = (int8_t)(v > 0x1f ? 0x1f : v);
                    break;
                }
                case 0xe: {
                    unsigned hi = param & 0xf0;
                    if (hi == 0x90 || hi == 0x60 || hi == 0xc0 || hi == 0xd0) {
                        cell->fx[0] = 0x0e;
                        cell->param[0] = (uint8_t)param;
                    } else if (hi == 0x80) {
                        cell->fx[0] = 0x0e;
                        param &= 0xf;
                        if (param < 6)
                            cell->param[0] = 0x81;
                        else if (param < 10)
                            cell->param[0] = 0x80;
                        if (param > 9)
                            cell->param[0] = 0x82;
                    }
                    break;
                }
                case 0xf:
                    if (param < 0x20) {
                        if (param > 0xf)
                            param = 0xf;
                        cell->fx[0] = 0x0f;
                        cell->param[0] = (uint8_t)(param << 4 | param);
                    }
                    break;
                }
                // an arpeggio on the previous row must be switched off explicitly
                // (`row` may have been moved to 0x40 above, exactly as in the original)
                if (row > 0 && (fx != 0 || param == 0)) {
                    Cell *prev = &pat->cell[(row - 1) & 0xff][ch];
                    if (prev->fx[0] == 0 && prev->param[0] != 0) {
                        if (fx != 0)
                            prev->param[0] = 0xff;
                        if (fx == 0 && param == 0) {
                            cell->fx[0] = 0;
                            cell->param[0] = 0xff;
                        }
                    }
                }
            }
        }
    }

    // a backwards position jump ends the song and sets the loop point
    for (int i = 0; i < (int16_t)g_song.orderLen; i++) {
        long pos = (long)g_song.order[i].pattern * 0x100 * channels + dataStart;
        for (int row = 0; row < 0x40; row++) {
            for (int ch = 0; ch < channels; ch++) {
                const uint8_t *d = at(pos);
                unsigned fx = d[2] & 0xf;
                int param = d[3];
                if (fx == 0xb && param < i) {
                    g_song.orderLen = (uint16_t)(i + 1);
                    g_song.loopPos = (uint16_t)param;
                    i = 0x100;
                    row = 0x40;
                    break;
                }
                if (fx == 0xd) {
                    row = 0x40;
                    break;
                }
                pos += 4;
            }
        }
    }
    free(buf);
    return true;
}
