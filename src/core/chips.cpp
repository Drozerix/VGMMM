// Sound chip emulation front end: two YM2612 + two SN76489 instances (the second pair plays
// the instrument preview), register queue playback and mixing to 16-bit stereo.
#include <cstring>

#include "player.h"
#include "vgmplay.h"

typedef long double ld;

extern "C" {
void *ym2612_init(int baseclock, int rate);
void ym2612_shutdown(void *chip);
void ym2612_reset(void *chip);
void ym2612_render(void *chip, int *buffer, int length, bool add);
int ym2612_write(void *chip, int a, unsigned char v);
void ym2612_set_mute(void *chip, int mute);
float ym2612_get_channel_volume(void *chip, int chn);
const char *ym2612_about(void);
void segapsg_write_stereo(void *chip, unsigned char data);
void segapsg_write_register(void *chip, unsigned char data);
void segapsg_render(void *chip, int *buffer, int samples, bool add);
void segapsg_set_gain(void *chip, int gain);
void *segapsg_init(int base_clock, int rate, bool neg);
void segapsg_shutdown(void *chip);
void segapsg_set_mute(void *chip, int mask);
float segapsg_get_channel_volume(void *chip, int channel);
const char *segapsg_about(void);
}

static void *s_ym, *s_ym2, *s_psg, *s_psg2;
static unsigned s_muteMask = 0xff, s_savedMute = 0xff;
static uint8_t s_previewEnabled = 1;
static float s_rate;
static float s_dacAcc;
static int s_mix[0x1000];
static float s_testPhase;

unsigned g_outputRate = 44100;
int g_frameCounter;
uint8_t g_clipped;
uint8_t g_testSignal;
int g_outputVolume = 0x80;
int g_fmVolume = 0x80;

bool chips_init(int fmClock, int psgClock, unsigned rate)
{
    chips_shutdown();
    memset(s_mix, 0, sizeof(s_mix));
    s_muteMask = 0xff;
    s_savedMute = 0xff;
    s_previewEnabled = 1;
    s_rate = (float)rate;
    g_clipped = 0;
    g_outputVolume = 0x80;
    g_fmVolume = 0x80;
    s_dacAcc = 0.0f;
    g_outputRate = rate;
    s_ym = ym2612_init(fmClock, (int)rate);
    if (s_ym == nullptr)
        return false;
    s_ym2 = ym2612_init(fmClock, (int)rate);
    if (s_ym2 == nullptr)
        return false;
    s_psg = segapsg_init(psgClock, (int)rate, false);
    if (s_psg == nullptr)
        return false;
    s_psg2 = segapsg_init(psgClock, (int)rate, true);
    if (s_psg2 == nullptr)
        return false;
    segapsg_set_gain(s_psg, 0);
    segapsg_set_mute(s_psg, 0xff);
    segapsg_set_gain(s_psg2, 0);
    segapsg_write_stereo(s_psg2, 0xff);
    segapsg_set_mute(s_psg2, 0xff);
    return true;
}

void chips_shutdown(void)
{
    if (s_ym) ym2612_shutdown(s_ym);
    if (s_ym2) ym2612_shutdown(s_ym2);
    if (s_psg) segapsg_shutdown(s_psg);
    if (s_psg2) segapsg_shutdown(s_psg2);
    s_ym = s_ym2 = s_psg = s_psg2 = nullptr;
}

void chips_ym_write(int port, uint8_t v)
{
    if (s_ym)
        ym2612_write(s_ym, port, v);
}

void chips_ym2_write(int port, uint8_t v)
{
    if (s_ym2)
        ym2612_write(s_ym2, port, v);
}

void chips_psg2_write(uint8_t v)
{
    if (s_psg2)
        segapsg_write_register(s_psg2, v);
}

void chips_psg_write(uint8_t v)
{
    if (s_psg)
        segapsg_write_register(s_psg, v);
}

void chips_psg_stereo(uint8_t v)
{
    if (s_psg)
        segapsg_write_stereo(s_psg, v);
}

void chips_psg2_set_mute(int mask)
{
    if (s_psg2)
        segapsg_set_mute(s_psg2, mask);
}

void chips_reset(void)
{
    if (s_ym) {
        ym2612_reset(s_ym);
        ym2612_reset(s_ym2);
        static const uint8_t regs[3] = {0x22, 0x27, 0x2b};
        for (int i = 0; i < 3; i++) {
            ym2612_write(s_ym, 0, regs[i]);
            ym2612_write(s_ym, 1, 0);
        }
        for (int i = 0; i < 3; i++) {
            ym2612_write(s_ym, 0, (uint8_t)(0xb4 + i));
            ym2612_write(s_ym, 1, 0xc0);
            ym2612_write(s_ym, 2, (uint8_t)(0xb4 + i));
            ym2612_write(s_ym, 3, 0xc0);
        }
        for (int i = 0; i < 2; i++) {
            ym2612_write(s_ym2, 0, regs[i]);
            ym2612_write(s_ym2, 1, 0);
        }
    }
    dac_set_log_mode(0);
}

void chips_set_mute(unsigned mask)
{
    s_muteMask = mask;
    if (s_ym) {
        ym2612_set_mute(s_ym, (int)(mask & 0x3f));
        segapsg_set_mute(s_psg, (int)mask >> 6 & 0xf);
    }
}

unsigned chips_get_mute(void)
{
    return s_muteMask;
}

// WAV rendering switches the preview chips off and can force all channels on.
void chips_render_mode(bool rendering, bool allChannels)
{
    if (!rendering) {
        s_previewEnabled = 1;
        chips_set_mute(s_savedMute);
    } else {
        s_savedMute = s_muteMask;
        s_previewEnabled = 0;
        if (allChannels)
            chips_set_mute(0xfff);
    }
}

void chips_set_balance(int output, int fm)
{
    if (output < 0) output = 0;
    if (output > 0xff) output = 0xff;
    if (fm < 0) fm = 0;
    if (fm > 0xff) fm = 0xff;
    g_outputVolume = output;
    g_fmVolume = fm;
}

static void chips_mix(int16_t *out, int frames)
{
    uint8_t clipped = 0;
    int gain = (int)((ld)g_outputVolume * (ld)0.00390625 * (ld)192.0);
    float dacStep = (float)((ld)(int)g_song.sampleRate / (ld)s_rate);
    int fmMul = g_fmVolume * 7 + 0x3c0;
    while (frames != 0) {
        int n = frames;
        if (n < 0x801) {
            frames = 0;
        } else {
            frames = n - 0x800;
            n = 0x800;
        }
        int *p = s_mix;
        for (int i = 0; i < n; i++) {
            s_dacAcc = (float)((ld)dacStep + (ld)s_dacAcc);
            if (1.0f <= s_dacAcc) {
                dac_tick_emul();
                s_dacAcc = (float)((ld)s_dacAcc - (ld)1.0);
            }
            ym2612_render(s_ym, p, 1, false);
            p += 2;
        }
        if (s_previewEnabled)
            ym2612_render(s_ym2, s_mix, n, true);
        for (int i = 0; i < n * 2; i++)
            s_mix[i] = s_mix[i] * fmMul >> 8;
        segapsg_render(s_psg, s_mix, n, true);
        if (s_previewEnabled)
            segapsg_render(s_psg2, s_mix, n, true);
        for (int i = 0; i < n * 2; i++) {
            int v = s_mix[i] * gain >> 8;
            if (v < -0x7fff) {
                clipped = 1;
                v = -0x7fff;    // (the original clamps this to +0x7fff, which crackles)
            }
            if (v > 0x7fff) {
                clipped = 1;
                v = 0x7fff;
            }
            *out++ = (int16_t)v;
        }
    }
    if (clipped)
        g_clipped = clipped;
}

void chips_flush_queue(int16_t *out, int frames)
{
    for (; g_regqRead < g_regqCount; g_regqRead++) {
        const RegWrite *w = &g_regq[g_regqRead];
        if (w->reg == 0xff || s_ym == nullptr)
            continue;
        if (w->type < 2) {
            ym2612_write(s_ym, w->type * 2, w->reg);
            ym2612_write(s_ym, w->type * 2 + 1, w->val);
        } else if (w->type == REG_PSG) {
            segapsg_write_register(s_psg, w->val);
        } else if (w->type == REG_GG_STEREO) {
            segapsg_write_stereo(s_psg, w->val);
        }
    }
    if (frames != 0 && s_ym != nullptr)
        chips_mix(out, frames);
}

static void render(int16_t *out, int frames)
{
    chips_flush_queue(out, frames);
    if (g_testSignal) {
        float step = (float)((ld)(1.0f / (float)(int)g_outputRate) * (ld)880.0);
        float p = s_testPhase;
        for (int i = 0; i < frames; i++) {
            ld pp = (ld)(float)((ld)p * (ld)p);
            int v = (int)((((ld)2.0 * pp + (ld)-4.0) * pp + (ld)1.0) * (ld)16384.0);
            out[0] = (int16_t)v;
            out[1] = (int16_t)v;
            out += 2;
            p = (float)((ld)step + (ld)p);
            if (1.0f <= p)
                p = (float)((ld)p - (ld)2.0);
        }
        s_testPhase = p;
    }
}

void audio_render(int16_t *out, int frames)
{
    g_rendering = 1;
    if (vgmplay_playing()) {
        vgmplay_render(out, frames);
    } else if (g_song.fps == 0) {
        memset(out, 0, (size_t)frames * 4);
    } else {
        int perTick = (int)(g_outputRate / g_song.fps);
        if (g_frameCounter + frames < perTick) {
            render(out, frames);
            g_frameCounter += frames;
        } else {
            render(out, perTick - g_frameCounter);
            int remaining = frames - (perTick - g_frameCounter);
            out += (perTick - g_frameCounter) * 2;
            g_frameCounter = 0;
            player_tick(false);
            for (; perTick <= remaining && perTick > 0; remaining -= perTick) {
                render(out, perTick);
                player_tick(false);
                out += perTick * 2;
            }
            g_frameCounter = remaining;
            render(out, remaining);
        }
    }
    g_rendering = 0;
}

float chips_fm_level(int ch)
{
    return s_ym ? ym2612_get_channel_volume(s_ym, ch) : 0.0f;
}

float chips_psg_level(int ch)
{
    return s_psg ? segapsg_get_channel_volume(s_psg, ch) : 0.0f;
}

const char *chips_about_fm(void)
{
    return ym2612_about();
}

const char *chips_about_psg(void)
{
    return segapsg_about();
}
