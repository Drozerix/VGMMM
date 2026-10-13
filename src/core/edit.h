// Pattern editing primitives (edit.cpp).
#pragma once
#include "song.h"

// Block clipboard. Same layout as the original's ("TFM MM Clipboard" format): a pattern-sized
// cell array followed by the block size.
struct ClipBlock {
    Cell cell[MAX_ROWS][CHANNELS];
    int32_t pad;
    int32_t width;          // -1 = empty
    int32_t height;
};
static_assert(sizeof(ClipBlock) == 0x8c0c, "ClipBlock layout");
extern ClipBlock g_clip;

void cell_delete(int pat, int row, int chan);
void cell_insert(int pat, int row, int chan);
void clip_init(void);
bool block_clamp(int pat, int chan, int row, int *w, int *h);
bool block_copy(int pat, int chan, int row, int w, int h);
bool block_cut(int pat, int chan, int row, int w, int h);
bool block_clear(int pat, int chan, int row, int w, int h);
bool block_paste(int pat, int chan, int row, bool over);
bool block_expand(int pat, int chan, int row, int w, int h);
bool block_shrink(int pat, int chan, int row, int w, int h);
bool block_transpose(int pat, int order, int chan, int row, int w, int h, int semitones, int ins, bool testOnly);
bool block_volume_gradient(int pat, int chan, int row, int w, int h);
bool block_volume_scale(int pat, int chan, int row, int w, int h, int from, int to);
bool block_effect_gradient(int pat, int chan, int row, int w, int h, const int *fxColumn);
bool block_effect_scale(int pat, int chan, int row, int w, int h, int from, int to, const int *fxColumn);
bool block_duplicate(int pat, int chan, int row, int w, int h, int count);
bool block_assign_instrument(int pat, int chan, int row, int w, int h, int ins);
bool block_exchange_channels(int pat, int row, int h, int chanA, int chanB);
