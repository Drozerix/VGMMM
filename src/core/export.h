// File exporters and importers.
#pragma once

extern const char *g_appVersion;
extern bool g_wavFadeOut;               // add a 3 second fade-out to rendered WAV files

// system: 0 = Mega Drive/Genesis (FM+PSG+PCM), 1 = Master System (PSG), 2 = Game Gear (PSG stereo)
bool export_vgm(const char *filename, int system, bool gzip, bool v150, bool optimise = true);
bool export_tfd(const char *filename);
bool export_text(const char *filename);
// progress() is called with 0..100 and returns true to cancel
bool export_wav(const char *filename, bool allChannels, bool (*progress)(int percent, void *user), void *user);
const char *export_about_vgmcmp(void);

bool import_tfm(const char *filename);  // TFM Music Maker .tfe (both format generations)
bool import_mod(const char *filename);  // Amiga .mod (notes and a few effects only)
