// Pattern editing primitives: cell insert/delete, block clipboard and block transforms.
// Original functions 0x4074e8..0x408cb8.
#include <cstring>

#include "edit.h"

ClipBlock g_clip;

static void cell_reset(Cell *c)
{
    memset(c, 0, sizeof(Cell));
    c->note = NOTE_EMPTY;
}

// 0x4074e8: remove the cell, pulling up the rest of the channel
void cell_delete(int pat, int row, int chan)
{
    Pattern *p = &g_song.pattern[pat];
    for (int r = row; r < MAX_ROWS - 1; r++)
        p->cell[r][chan] = p->cell[r + 1][chan];
    cell_reset(&p->cell[MAX_ROWS - 1][chan]);
}

// 0x4075b0: insert an empty cell, pushing down the rest of the channel
void cell_insert(int pat, int row, int chan)
{
    Pattern *p = &g_song.pattern[pat];
    if (row < 0 || row >= MAX_ROWS)
        return;
    for (int r = MAX_ROWS - 1; r > row; r--)
        p->cell[r][chan] = p->cell[r - 1][chan];
    cell_reset(&p->cell[row][chan]);
}

// 0x4078dc
void pattern_expand(int pat)
{
    Pattern *p = &g_song.pattern[pat];
    p->length *= 2;
    for (int r = 1; r < p->length; r += 2)
        for (int c = 0; c < CHANNELS; c++)
            cell_insert(pat, r, c);
}

// 0x40794c
void pattern_shrink(int pat)
{
    Pattern *p = &g_song.pattern[pat];
    for (int r = 1; r < p->length; r++)
        for (int c = 0; c < CHANNELS; c++)
            cell_delete(pat, r, c);
    p->length /= 2;
}

// 0x4079c0
void clip_init(void)
{
    memset(&g_clip, 0, sizeof(g_clip));
    g_clip.width = -1;
}

// 0x4079e4: clip the block to the pattern; false if nothing is left
bool block_clamp(int pat, int chan, int row, int *w, int *h)
{
    int len = g_song.pattern[pat].length;
    if (*w < 1 || *h < 1)
        return false;
    if (len < row)
        return false;
    if (chan >= CHANNELS)
        return false;
    if (len < *h + row)
        *h = len - row;
    if (CHANNELS < *w + chan)
        *w = CHANNELS - chan;
    return true;
}

// 0x407a88
bool block_copy(int pat, int chan, int row, int w, int h)
{
    if (!block_clamp(pat, chan, row, &w, &h))
        return false;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            g_clip.cell[r][c] = g_song.pattern[pat].cell[row + r][chan + c];
    g_clip.width = w;
    g_clip.height = h;
    return true;
}

// 0x407b64
bool block_cut(int pat, int chan, int row, int w, int h)
{
    if (!block_clamp(pat, chan, row, &w, &h))
        return false;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            g_clip.cell[r][c] = g_song.pattern[pat].cell[row + r][chan + c];
            cell_reset(&g_song.pattern[pat].cell[row + r][chan + c]);
        }
    g_clip.width = w;
    g_clip.height = h;
    return true;
}

// 0x407cc8
bool block_clear(int pat, int chan, int row, int w, int h)
{
    if (!block_clamp(pat, chan, row, &w, &h))
        return false;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            cell_reset(&g_song.pattern[pat].cell[row + r][chan + c]);
    return true;
}

// 0x407dc0. "over" merges: only fields that are set in the clipboard replace the pattern's.
bool block_paste(int pat, int chan, int row, bool over)
{
    if (g_clip.width < 0)
        return false;
    int w = g_clip.width;
    int h = g_clip.height;
    if (!block_clamp(pat, chan, row, &w, &h))
        return false;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            const Cell *s = &g_clip.cell[r][c];
            Cell *d = &g_song.pattern[pat].cell[row + r][chan + c];
            if (!over) {
                *d = *s;
                continue;
            }
            if (s->volume != 0)
                d->volume = s->volume;
            if (s->ins != 0)
                d->ins = s->ins;
            if (s->note < 0xff) {
                d->note = s->note;
                d->octave = s->octave;
            }
            // (sic) the original tests for an *empty* source effect here, so effects are
            // never merged and an empty source column erases the destination's
            for (int i = 0; i < 4; i++)
                if (s->fx[i] == 0 && s->param[i] == 0) {
                    d->fx[i] = s->fx[i];
                    d->param[i] = s->param[i];
                }
        }
    return true;
}

// 0x407fc0: double the block's duration by inserting an empty row after each of its rows
bool block_expand(int pat, int chan, int row, int w, int h)
{
    if (!block_clamp(pat, chan, row, &w, &h))
        return false;
    Pattern *p = &g_song.pattern[pat];
    p->length += h;
    if (p->length > 0xff)
        p->length = 0xff;
    for (int i = 1; i < h * 2; i += 2)
        for (int c = 0; c < w; c++)
            cell_insert(pat, row + i, chan + c);
    return true;
}

// 0x408060: drop every second row of the block
bool block_shrink(int pat, int chan, int row, int w, int h)
{
    if (!block_clamp(pat, chan, row, &w, &h))
        return false;
    for (int i = 1; i < h; i++)
        for (int c = 0; c < w; c++)
            cell_delete(pat, row + i, chan + c);
    return true;
}

// 0x4080cc. ins > 0 restricts the transposition to notes played with that instrument
// (instrument numbers carry over from earlier rows and earlier positions of the order list).
// With testOnly nothing is changed; the result tells whether every note stays in range.
bool block_transpose(int pat, int order, int chan, int row, int w, int h, int semitones, int ins, bool testOnly)
{
    if (!block_clamp(pat, chan, row, &w, &h))
        return false;
    int curIns[CHANNELS];
    if (ins >= 1) {
        for (int c = 0; c < w; c++) {
            curIns[chan + c] = 1;
            for (int o = order; o >= 0; o--) {
                const Pattern *p = &g_song.pattern[g_song.order[o].pattern];
                int r = o == order ? row : p->length - 1;
                bool found = false;
                for (; r >= 0; r--) {
                    if (p->cell[r][chan + c].ins > 0) {
                        curIns[chan + c] = p->cell[r][chan + c].ins;
                        found = true;
                        break;
                    }
                }
                if (found)
                    break;
            }
        }
    }
    for (int c = 0; c < w; c++)
        for (int r = 0; r < h; r++) {
            Cell *cell = &g_song.pattern[pat].cell[row + r][chan + c];
            if (ins >= 1) {
                if (cell->ins > 0)
                    curIns[chan + c] = cell->ins;
                if (curIns[chan + c] != ins)
                    continue;
            }
            if (cell->note >= 12)
                continue;
            int n = cell->note + cell->octave * 12 - 12 + semitones;
            if (!testOnly) {
                if (n < 0)
                    n = 0;
                if (n > 0x5f)
                    n = 0x5f;
                cell->note = (int16_t)(n % 12);
                cell->octave = (int8_t)(n / 12 + 1);
            } else if (n < 0 || n > 0x5f) {
                return false;
            }
        }
    return true;
}

// 0x4083b0: linear ramps between the volume values present in each channel of the block
bool block_volume_gradient(int pat, int chan, int row, int w, int h)
{
    if (!block_clamp(pat, chan, row, &w, &h))
        return false;
    Pattern *p = &g_song.pattern[pat];
    for (int c = chan; c < chan + w; c++) {
        bool done[MAX_ROWS + 1] = {};
        for (int r = row; r < row + h; r++) {
            int v = p->cell[r][c].volume;
            if (v == 0 || done[r])
                continue;
            done[r] = true;
            float cur = (float)v;
            for (int r2 = r; r2 < row + h; r2++) {
                if (p->cell[r2][c].volume == 0 || done[r2])
                    continue;
                float step = ((float)p->cell[r2][c].volume - cur) / ((float)r2 - (float)r);
                for (; r < r2; r++) {
                    p->cell[r][c].volume = (int8_t)(int)cur;
                    cur = step + cur;
                    done[r] = true;
                }
                r = r2 - 1;
                break;
            }
        }
    }
    return true;
}

// 0x4085d8: scale volumes by a factor sliding from `from` to `to` percent over the block
bool block_volume_scale(int pat, int chan, int row, int w, int h, int from, int to)
{
    if (!block_clamp(pat, chan, row, &w, &h))
        return false;
    float a = (float)(0.01L * (long double)from);
    float b = (float)(0.01L * (long double)to);
    Pattern *p = &g_song.pattern[pat];
    for (int c = chan; c < chan + w; c++) {
        float scale = a;
        for (int r = row; r < row + h; r++) {
            Cell *cell = &p->cell[r][c];
            if (cell->volume != 0) {
                float v = (float)cell->volume * scale;
                if (v < 1.0f)
                    v = 1.0f;
                if (v > 31.0f)
                    v = 31.0f;
                cell->volume = (int8_t)(int)v;
            }
            scale = (b - a) / (float)h + scale;
        }
    }
    return true;
}

// 0x408718: as the volume gradient, for the parameters of the visible effect columns
bool block_effect_gradient(int pat, int chan, int row, int w, int h, const int *fxColumn)
{
    if (!block_clamp(pat, chan, row, &w, &h))
        return false;
    Pattern *p = &g_song.pattern[pat];
    for (int c = chan; c < chan + w; c++) {
        int fx = fxColumn[c];
        bool done[MAX_ROWS + 1] = {};
        for (int r = row; r < row + h; r++) {
            int v = p->cell[r][c].param[fx];
            if (v == 0 || done[r])
                continue;
            done[r] = true;
            float cur = (float)v;
            for (int r2 = r; r2 < row + h; r2++) {
                if (p->cell[r2][c].param[fx] == 0 || done[r2])
                    continue;
                float step = ((float)p->cell[r2][c].param[fx] - cur) / ((float)r2 - (float)r);
                for (; r < r2; r++) {
                    p->cell[r][c].param[fx] = (uint8_t)(int)cur;
                    cur = step + cur;
                    done[r] = true;
                }
                r = r2 - 1;
                break;
            }
        }
    }
    return true;
}

// 0x408980
bool block_effect_scale(int pat, int chan, int row, int w, int h, int from, int to, const int *fxColumn)
{
    if (!block_clamp(pat, chan, row, &w, &h))
        return false;
    float a = (float)(0.01L * (long double)from);
    float b = (float)(0.01L * (long double)to);
    Pattern *p = &g_song.pattern[pat];
    for (int c = chan; c < chan + w; c++) {
        float scale = a;
        for (int r = row; r < row + h; r++) {
            uint8_t *prm = &p->cell[r][c].param[fxColumn[c]];
            float v = (float)*prm * scale;
            if (v < 0.0f)
                v = 0.0f;
            if (v > 255.0f)
                v = 255.0f;
            *prm = (uint8_t)(int)v;
            scale = (b - a) / (float)h + scale;
        }
    }
    return true;
}

// 0x408ae0: repeat the block `count` times below itself, up to the end of the pattern
bool block_duplicate(int pat, int chan, int row, int w, int h, int count)
{
    if (!block_clamp(pat, chan, row, &w, &h))
        return false;
    Pattern *p = &g_song.pattern[pat];
    int dst = row + h;
    for (int n = 0; n < count && dst < p->length; n++)
        for (int i = 0; i < h && dst < p->length; i++, dst++)
            for (int c = chan; c < chan + w; c++)
                p->cell[dst][c] = p->cell[row + i][c];
    return true;
}

// 0x408c18
bool block_assign_instrument(int pat, int chan, int row, int w, int h, int ins)
{
    if (!block_clamp(pat, chan, row, &w, &h))
        return false;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            Cell *cell = &g_song.pattern[pat].cell[row + r][chan + c];
            if (cell->note < 12)
                cell->ins = (int16_t)ins;
        }
    return true;
}

// 0x408cb8
bool block_exchange_channels(int pat, int row, int h, int chanA, int chanB)
{
    if (chanA == chanB)
        return false;
    int w = CHANNELS;
    if (!block_clamp(pat, 0, row, &w, &h))
        return false;
    Pattern *p = &g_song.pattern[pat];
    for (int r = row; r < row + h; r++) {
        Cell t = p->cell[r][chanA];
        p->cell[r][chanA] = p->cell[r][chanB];
        p->cell[r][chanB] = t;
    }
    return true;
}
