// Playback of finished VGM/VGZ files (YM2612 + SN76489 streams, as the editor exports them).
// Not part of the original program, which can only write VGM files.
#pragma once
#include <cstdint>

struct VgmInfo {
    uint32_t version;
    uint32_t fmClock, psgClock;     // 0 = chip not used
    uint32_t totalSamples;          // at 44100 Hz
    uint32_t loopSamples;           // 0 = no loop
    char title[256], game[256], system[256], author[256];   // GD3 tag (English fields, Latin-1)
};

bool vgmplay_load(const char *filename, char *error);   // error: at least 256 bytes
void vgmplay_free(void);
bool vgmplay_loaded(void);
const VgmInfo *vgmplay_info(void);
// The caller initialises the chips with the file's clocks first and holds the audio lock.
void vgmplay_start(int loops);      // loops: how many times the looped part is played
void vgmplay_stop(void);
bool vgmplay_playing(void);
uint32_t vgmplay_position(void);    // samples played so far
uint32_t vgmplay_length(void);      // samples for the requested number of loops
void vgmplay_render(int16_t *out, int frames);
// Render the whole file to a 16-bit stereo WAV (chips must be initialised, audio device closed).
bool vgmplay_render_wav(const char *filename, int loops);
