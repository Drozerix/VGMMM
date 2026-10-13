// VGM file player. Register writes go to the same chip instances the tracker uses, so the mute
// buttons, volume settings and level meters apply to VGM playback too.
#include "vgmplay.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <zlib.h>

#include "player.h"

extern "C" void segapsg_write_stereo(void *chip, unsigned char data);

namespace {

enum { VGM_RATE = 44100 };

struct Block {
    uint32_t start, size;   // within the PCM data bank
};

// DAC stream control (VGM 1.60 commands 0x90..0x95)
struct Stream {
    bool setup, playing;
    uint8_t port, reg;
    uint8_t step, base;
    uint32_t freq;
    uint32_t pos, remain;
    uint32_t acc;
};

std::vector<uint8_t> s_data;        // whole file, decompressed
std::vector<uint8_t> s_bank;        // data blocks of type 0 (YM2612 PCM), concatenated
std::vector<Block> s_blocks;
VgmInfo s_info;
uint32_t s_dataStart, s_dataEnd, s_loopPos;
bool s_loaded;

bool s_playing;
uint32_t s_pos;                     // read position
uint32_t s_wait;                    // samples until the next command
uint32_t s_played;
uint32_t s_bankPos;                 // 0xE0 seek position for 0x8n
int s_loopsLeft;
Stream s_stream[256];

uint32_t rd32le(uint32_t off)
{
    if (off + 4 > s_data.size())
        return 0;
    return (uint32_t)s_data[off] | (uint32_t)s_data[off + 1] << 8 | (uint32_t)s_data[off + 2] << 16 |
           (uint32_t)s_data[off + 3] << 24;
}

// length in bytes of a command this player does not act on; 0 = unknown
int skip_length(uint8_t c)
{
    if (c >= 0x30 && c <= 0x3f) return 2;
    if (c >= 0x40 && c <= 0x4e) return 3;
    if (c >= 0x51 && c <= 0x5f) return 3;
    if (c == 0x68) return 12;
    if (c >= 0xa0 && c <= 0xbf) return 3;
    if (c >= 0xc0 && c <= 0xdf) return 4;
    if (c >= 0xe1) return 5;
    return 0;
}

void gd3_string(uint32_t *off, uint32_t end, char *dst, int size)
{
    int n = 0;
    while (*off + 2 <= end) {
        unsigned ch = s_data[*off] | s_data[*off + 1] << 8;
        *off += 2;
        if (ch == 0)
            break;
        if (n < size - 1)
            dst[n++] = ch < 0x100 ? (char)ch : '?';
    }
    dst[n] = 0;
}

void read_gd3(void)
{
    uint32_t rel = rd32le(0x14);
    if (rel == 0)
        return;
    uint32_t off = 0x14 + rel;
    if (off + 12 > s_data.size() || memcmp(&s_data[off], "Gd3 ", 4) != 0)
        return;
    uint32_t end = off + 12 + rd32le(off + 8);
    if (end > s_data.size())
        end = (uint32_t)s_data.size();
    off += 12;
    char skip[256];
    gd3_string(&off, end, s_info.title, sizeof(s_info.title));
    gd3_string(&off, end, skip, sizeof(skip));      // Japanese variants are skipped
    gd3_string(&off, end, s_info.game, sizeof(s_info.game));
    gd3_string(&off, end, skip, sizeof(skip));
    gd3_string(&off, end, s_info.system, sizeof(s_info.system));
    gd3_string(&off, end, skip, sizeof(skip));
    gd3_string(&off, end, s_info.author, sizeof(s_info.author));
}

// collect the PCM data blocks once, so commands 0x8n/0x93/0x95 can refer to them
bool scan_blocks(char *error)
{
    uint32_t pos = s_dataStart;
    while (pos < s_dataEnd) {
        uint8_t c = s_data[pos];
        if (c == 0x66)
            return true;
        if (c == 0x67) {
            if (pos + 7 > s_dataEnd)
                break;
            uint8_t type = s_data[pos + 2];
            uint32_t size = rd32le(pos + 3) & 0x7fffffff;
            if (pos + 7 + size > s_dataEnd)
                break;
            if (type == 0) {
                s_blocks.push_back({(uint32_t)s_bank.size(), size});
                s_bank.insert(s_bank.end(), s_data.begin() + pos + 7, s_data.begin() + pos + 7 + size);
            }
            pos += 7 + size;
            continue;
        }
        int len;
        switch (c) {
        case 0x4f: case 0x50: case 0x94: len = 2; break;
        case 0x52: case 0x53: case 0x61: len = 3; break;
        case 0x62: case 0x63: len = 1; break;
        case 0x90: case 0x91: case 0x95: case 0xe0: len = 5; break;
        case 0x92: len = 6; break;
        case 0x93: len = 11; break;
        default:
            len = (c >= 0x70 && c <= 0x8f) ? 1 : skip_length(c);
            break;
        }
        if (len == 0) {
            snprintf(error, 256, "unknown VGM command 0x%02X at offset 0x%X", c, pos);
            return false;
        }
        pos += (uint32_t)len;
    }
    return true;    // no end marker: play up to the end of the data
}

void dac_write(uint8_t port, uint8_t reg, uint8_t v)
{
    chips_ym_write(port ? 2 : 0, reg);
    chips_ym_write(port ? 3 : 1, v);
}

void stream_start(Stream *s, uint32_t start, uint32_t length)
{
    if (!s->setup || start >= s_bank.size())
        return;
    if (length > s_bank.size() - start)
        length = (uint32_t)s_bank.size() - start;
    s->pos = start + s->base;
    s->remain = length;
    s->acc = VGM_RATE;      // first byte right away
    s->playing = length != 0;
}

// samples until a stream needs to output its next byte (at least 1)
uint32_t streams_next(void)
{
    uint32_t next = 0xffffffff;
    for (Stream &s : s_stream) {
        if (!s.playing || s.freq == 0)
            continue;
        uint32_t n = s.acc >= VGM_RATE ? 0 : (VGM_RATE - s.acc + s.freq - 1) / s.freq;
        if (n < next)
            next = n;
    }
    return next;
}

void streams_advance(uint32_t samples)
{
    for (Stream &s : s_stream)
        if (s.playing && s.freq != 0)
            s.acc += s.freq * samples;
}

void streams_output(void)
{
    for (Stream &s : s_stream) {
        if (!s.playing || s.freq == 0)
            continue;
        while (s.acc >= VGM_RATE && s.playing) {
            s.acc -= VGM_RATE;
            if (s.remain == 0 || s.pos >= s_bank.size()) {
                s.playing = false;
                break;
            }
            dac_write(s.port, s.reg, s_bank[s.pos]);
            s.pos += s.step ? s.step : 1;
            s.remain--;
        }
    }
}

// run commands until one of them waits; false at the end of the song
bool run_commands(void)
{
    while (s_wait == 0) {
        if (s_pos >= s_dataEnd)
            return false;
        uint8_t c = s_data[s_pos];
        const uint8_t *a = &s_data[s_pos + 1];
        uint32_t left = s_dataEnd - s_pos;
        if (c == 0x66) {
            if (s_loopPos != 0 && s_loopsLeft > 1) {
                s_loopsLeft--;
                s_pos = s_loopPos;
                continue;
            }
            return false;
        }
        if (c == 0x50 && left >= 2) {
            chips_psg_write(a[0]);
            s_pos += 2;
        } else if (c == 0x4f && left >= 2) {
            chips_psg_stereo(a[0]);
            s_pos += 2;
        } else if (c == 0x52 && left >= 3) {
            chips_ym_write(0, a[0]);
            chips_ym_write(1, a[1]);
            s_pos += 3;
        } else if (c == 0x53 && left >= 3) {
            chips_ym_write(2, a[0]);
            chips_ym_write(3, a[1]);
            s_pos += 3;
        } else if (c == 0x61 && left >= 3) {
            s_wait = a[0] | a[1] << 8;
            s_pos += 3;
        } else if (c == 0x62) {
            s_wait = 735;
            s_pos += 1;
        } else if (c == 0x63) {
            s_wait = 882;
            s_pos += 1;
        } else if (c >= 0x70 && c <= 0x7f) {
            s_wait = (c & 0xf) + 1u;
            s_pos += 1;
        } else if (c >= 0x80 && c <= 0x8f) {
            if (s_bankPos < s_bank.size())
                dac_write(0, 0x2a, s_bank[s_bankPos++]);
            s_wait = c & 0xf;
            s_pos += 1;
        } else if (c == 0xe0 && left >= 5) {
            s_bankPos = rd32le(s_pos + 1);
            s_pos += 5;
        } else if (c == 0x67 && left >= 7) {
            s_pos += 7 + (rd32le(s_pos + 3) & 0x7fffffff);
        } else if (c == 0x90 && left >= 5) {
            Stream *s = &s_stream[a[0]];
            // only the YM2612 DAC is driven through streams here
            s->setup = (a[1] & 0x7f) == 0x02;
            s->port = a[2];
            s->reg = a[3];
            s_pos += 5;
        } else if (c == 0x91 && left >= 5) {
            Stream *s = &s_stream[a[0]];
            s->step = a[2];
            s->base = a[3];
            s_pos += 5;
        } else if (c == 0x92 && left >= 6) {
            s_stream[a[0]].freq = rd32le(s_pos + 2);
            s_pos += 6;
        } else if (c == 0x93 && left >= 11) {
            Stream *s = &s_stream[a[0]];
            uint32_t start = rd32le(s_pos + 2);
            uint8_t mode = a[5];
            uint32_t length = rd32le(s_pos + 7);
            uint32_t avail = start < s_bank.size() ? (uint32_t)s_bank.size() - start : 0;
            switch (mode & 0x0f) {
            case 0: length = s->remain; break;                          // keep
            case 1: break;                                              // number of commands
            case 2: length = (uint32_t)((uint64_t)length * s->freq / 1000); break;  // milliseconds
            default: length = avail; break;                             // until the end of the data
            }
            if (start != 0xffffffff)
                stream_start(s, start, length);
            s_pos += 11;
        } else if (c == 0x94 && left >= 2) {
            s_stream[a[0]].playing = false;
            s_pos += 2;
        } else if (c == 0x95 && left >= 5) {
            Stream *s = &s_stream[a[0]];
            unsigned id = a[1] | a[2] << 8;
            if (id < s_blocks.size())
                stream_start(s, s_blocks[id].start, s_blocks[id].size);
            s_pos += 5;
        } else {
            int len = skip_length(c);
            if (len == 0)
                return false;
            s_pos += (uint32_t)len;
        }
    }
    return true;
}

} // namespace

bool vgmplay_load(const char *filename, char *error)
{
    vgmplay_free();
    error[0] = 0;
    gzFile f = gzopen(filename, "rb");      // reads plain and gzip-compressed (.vgz) files alike
    if (f == nullptr) {
        snprintf(error, 256, "can't open the file");
        return false;
    }
    uint8_t buf[0x4000];
    int n;
    while ((n = gzread(f, buf, sizeof(buf))) > 0) {
        s_data.insert(s_data.end(), buf, buf + n);
        if (s_data.size() > 0x8000000)
            break;
    }
    gzclose(f);
    if (s_data.size() < 0x40 || memcmp(s_data.data(), "Vgm ", 4) != 0) {
        snprintf(error, 256, "not a VGM file");
        s_data.clear();
        return false;
    }
    memset(&s_info, 0, sizeof(s_info));
    s_info.version = rd32le(0x08);
    s_info.psgClock = rd32le(0x0c) & 0x3fffffff;
    s_info.totalSamples = rd32le(0x18);
    s_info.loopSamples = rd32le(0x20);
    // before 1.10 the YM2413 clock field was used for every FM chip
    s_info.fmClock = (s_info.version >= 0x110 ? rd32le(0x2c) : rd32le(0x10)) & 0x3fffffff;
    uint32_t rel = s_info.version >= 0x150 ? rd32le(0x34) : 0;
    s_dataStart = rel ? 0x34 + rel : 0x40;
    uint32_t eof = rd32le(0x04) + 4;
    s_dataEnd = eof != 4 && eof <= s_data.size() ? eof : (uint32_t)s_data.size();
    uint32_t gd3 = rd32le(0x14);
    if (gd3 != 0 && 0x14 + gd3 > s_dataStart && 0x14 + gd3 < s_dataEnd)
        s_dataEnd = 0x14 + gd3;
    uint32_t loop = rd32le(0x1c);
    s_loopPos = loop ? 0x1c + loop : 0;
    if (s_loopPos < s_dataStart || s_loopPos >= s_dataEnd) {
        s_loopPos = 0;
        s_info.loopSamples = 0;
    }
    if (s_dataStart >= s_dataEnd) {
        snprintf(error, 256, "the file has no data");
        s_data.clear();
        return false;
    }
    if (s_info.fmClock == 0 && s_info.psgClock == 0) {
        snprintf(error, 256, "the file uses neither a YM2612 nor an SN76489");
        s_data.clear();
        return false;
    }
    if (!scan_blocks(error)) {
        vgmplay_free();
        return false;
    }
    read_gd3();
    s_loaded = true;
    return true;
}

void vgmplay_free(void)
{
    s_playing = false;
    s_loaded = false;
    s_data.clear();
    s_bank.clear();
    s_blocks.clear();
}

bool vgmplay_loaded(void)
{
    return s_loaded;
}

const VgmInfo *vgmplay_info(void)
{
    return &s_info;
}

void vgmplay_start(int loops)
{
    if (!s_loaded)
        return;
    memset(s_stream, 0, sizeof(s_stream));
    for (Stream &s : s_stream)
        s.step = 1;
    s_pos = s_dataStart;
    s_wait = 0;
    s_played = 0;
    s_bankPos = 0;
    s_loopsLeft = loops < 1 ? 1 : loops;
    s_playing = true;
}

void vgmplay_stop(void)
{
    s_playing = false;
}

bool vgmplay_playing(void)
{
    return s_playing;
}

uint32_t vgmplay_position(void)
{
    return s_played;
}

uint32_t vgmplay_length(void)
{
    uint32_t len = s_info.totalSamples;
    if (s_loopPos != 0 && s_loopsLeft > 1)
        len += s_info.loopSamples * (uint32_t)(s_loopsLeft - 1);
    return len;
}

void vgmplay_render(int16_t *out, int frames)
{
    while (frames > 0) {
        if (!s_playing) {
            chips_flush_queue(out, frames);     // let the chips ring out
            return;
        }
        streams_output();
        if (s_wait == 0 && !run_commands()) {
            s_playing = false;
            continue;
        }
        uint32_t n = s_wait;
        uint32_t next = streams_next();
        if (next != 0 && next < n)
            n = next;
        if (n > (uint32_t)frames)
            n = (uint32_t)frames;
        if (n == 0)
            continue;       // a stream byte is due at this very sample
        chips_flush_queue(out, (int)n);
        streams_advance(n);
        out += n * 2;
        frames -= (int)n;
        s_wait -= n;
        s_played += n;
    }
}

bool vgmplay_render_wav(const char *filename, int loops)
{
    if (!s_loaded)
        return false;
    FILE *f = fopen(filename, "wb");
    if (f == nullptr)
        return false;
    uint8_t hdr[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                       0x44, 0xac, 0, 0, 0x10, 0xb1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
    fwrite(hdr, 1, sizeof(hdr), f);
    chips_reset();
    vgmplay_start(loops);
    static int16_t buf[0x2000 * 2];
    uint32_t bytes = 0;
    uint32_t limit = vgmplay_length() + VGM_RATE * 600;     // guard against broken length fields
    while (s_playing && s_played < limit) {
        uint32_t before = s_played;
        vgmplay_render(buf, 0x2000);
        uint32_t n = s_played - before;
        fwrite(buf, 4, n, f);
        bytes += n * 4;
    }
    hdr[4] = (uint8_t)(bytes + 36);
    hdr[5] = (uint8_t)((bytes + 36) >> 8);
    hdr[6] = (uint8_t)((bytes + 36) >> 16);
    hdr[7] = (uint8_t)((bytes + 36) >> 24);
    hdr[40] = (uint8_t)bytes;
    hdr[41] = (uint8_t)(bytes >> 8);
    hdr[42] = (uint8_t)(bytes >> 16);
    hdr[43] = (uint8_t)(bytes >> 24);
    fseek(f, 0, SEEK_SET);
    fwrite(hdr, 1, sizeof(hdr), f);
    fclose(f);
    return true;
}
