// Pattern editor keyboard handling (original FUN_00419008 and helpers).
#include "mainwindow.h"

// 0x418b40: hex digit (or, for the effect type field, any letter) currently pressed; -1 if none
int MainWindow::hexKeyPressed(int field)
{
    bool repeat = CheckBoxPtnAutorepeat->isChecked();
    auto down = [&](int vk) { return repeat ? g_keyState[vk & 0xff] != 0 : g_keyState[vk & 0xff] == 1; };
    for (int i = 0; i < 10; i++)
        if (down(0x30 + i))
            return i;
    int letters = field == 5 ? 0x1a : 6;
    for (int i = 0; i < letters; i++)
        if (down(0x41 + i))
            return i + 10;
    return -1;
}

// 0x418cd8: note key pressed: 0..35 (three octaves), NOTE_OFF, or -1
int MainWindow::noteKeyPressed(bool repeat)
{
    auto down = [&](int action) { return repeat ? kbd_held(action) : kbd_pressed(action); };
    if (down(K.noteOff))
        return NOTE_OFF;
    for (int i = 0; i < 0x24; i++)
        if (down(K.note[i]))
            return i;
    return -1;
}

// 0x418c00: move the free corner of the selection
void MainWindow::selectionSetEnd(int chan, int row)
{
    U.selX2 = chan;
    U.selY2 = row;
    U.selRow = U.selY1 < U.selY2 ? U.selY1 : U.selY2;
    U.selChan = U.selX1 < U.selX2 ? U.selX1 : U.selX2;
    U.selW = abs(U.selX1 - U.selX2) + 1;
    U.selH = abs(U.selY1 - U.selY2) + 1;
}

// 0x418cac
void MainWindow::selectionFollowCursor()
{
    if (U.selecting)
        selectionSetEnd(U.curCol / 8, U.curRow);
}

// 0x418db8: move the cursor vertically; with follow-mode on it crosses into neighbouring positions
void MainWindow::cursorMoveRows(int delta, bool clamp)
{
    int order = U.curOrder;
    if (tracking())
        return;
    int len = g_song.pattern[g_song.order[U.curOrder].pattern].length;
    U.curRow += delta;
    if (!SpeedButtonFollowOrder->isChecked()) {
        if (!clamp) {
            if (U.curRow < 0)
                U.curRow += len;
            if (len <= U.curRow)
                U.curRow -= len;
        } else {
            if (U.curRow < 0)
                U.curRow = 0;
            if (len <= U.curRow)
                U.curRow = len - 1;
        }
    } else if (U.curRow < 0) {
        if (U.curOrder < 1) {
            U.curRow = 0;
        } else {
            do {
                if (U.curRow >= 0)
                    break;
                U.curOrder--;
                len = g_song.pattern[g_song.order[U.curOrder].pattern].length;
                U.curRow = len - abs(U.curRow);
            } while (U.curOrder != 0);
            if (U.curRow < 0)
                U.curRow = 0;
        }
    } else if (len <= U.curRow) {
        if (U.curOrder < g_song.orderLen - 1) {
            bool done = false;
            do {
                if (U.curRow < len) {
                    done = true;
                    break;
                }
                U.curOrder++;
                U.curRow -= len;
                len = g_song.pattern[g_song.order[U.curOrder].pattern].length;
            } while (U.curOrder < g_song.orderLen - 1);
            if (!done && len <= U.curRow)
                U.curRow = len - 1;
        } else {
            U.curRow = len - 1;
        }
    }
    if (U.curOrder != order) {
        audioLock();
        if (following())
            player_set_position(U.curOrder);
        audioUnlock();
        setCurrentPattern(g_song.order[U.curOrder].pattern);
        updateOrderList();
    }
    selectionFollowCursor();
    updateStatus(false, true);
}

void MainWindow::playFromCursor(bool loopPattern)
{
    if (SpeedButtonStop->isEnabled())
        return;
    savePosition();
    audioLock();
    player_start(U.curOrder, U.curRow, 0, loopPattern, 1, -1);
    audioUnlock();
    Timer1->start();
    SpeedButtonPlayAll->setEnabled(false);
    SpeedButtonPlayPos->setEnabled(false);
    SpeedButtonPlayPtn->setEnabled(false);
    SpeedButtonStop->setEnabled(true);
    SpeedButtonReset->setEnabled(false);
}

static void cell_clear(Cell *c)
{
    c->note = NOTE_EMPTY;
    c->volume = 0;
    c->ins = 0;
    for (int i = 0; i < 4; i++) {
        c->fx[i] = 0;
        c->param[i] = 0;
    }
}

// 0x419008. Returns true when the key event was consumed.
bool MainWindow::patternKeys()
{
    int octave = U.octave;
    int pat = U.curPattern;
    int col = U.curCol;
    int row = U.curRow;
    int chan = U.curCol / 8;
    int soloChan = -1;

    for (int i = 0; i < 8; i++) {
        if (kbd_pressed(K.octave[i])) {
            UpDownOctave->setValue(i + 1);
            return true;
        }
    }
    if (U.noPattern)
        return false;
    if (kbd_pressed(K.ptnSelectAll)) { EditSelectAllClick(); return true; }
    if (kbd_pressed(K.ptnSelectChn)) {
        U.selChan = chan;
        U.selRow = 0;
        U.selW = 1;
        U.selH = g_song.pattern[U.curPattern].length;
        U.selX1 = U.selChan;
        U.selY1 = U.selRow;
        U.selX2 = U.selX1;      // (as in the original: the anchor rectangle is left degenerate)
        U.selY2 = U.selH;
        patternRedraw();
        return true;
    }
    if (kbd_pressed(K.ptnUnselect)) { EditUnselectAllClick(); return true; }
    if (kbd_pressed(K.ptnTransSemiUp)) { blockTransposeQuick(1); return true; }
    if (kbd_pressed(K.ptnTransSemiDown)) { blockTransposeQuick(-1); return true; }
    if (kbd_pressed(K.ptnTransOctUp)) { blockTransposeQuick(12); return true; }
    if (kbd_pressed(K.ptnTransOctDown)) { blockTransposeQuick(-12); return true; }
    if (kbd_pressed(K.ptnAssignInstr)) { BlockAssignInsClick(); return true; }
    if (kbd_pressed(K.ptnCut)) { EditCutClick(); return true; }
    if (kbd_pressed(K.ptnCopy)) { EditCopyClick(); return true; }
    if (kbd_pressed(K.ptnPaste)) { EditPasteClick(); return true; }
    if (kbd_pressed(K.ptnPasteOver)) { EditPasteOverClick(); return true; }
    if (kbd_pressed(K.ptnDelete)) { EditDeleteClick(); return true; }
    if (kbd_pressed(K.ptnOrdPrev)) {
        if (U.curOrder > 0) {
            U.curOrder--;
            audioLock();
            if (following())
                player_set_position(U.curOrder);
            audioUnlock();
            setCurrentPattern(g_song.order[U.curOrder].pattern);
            updateOrderList();
            updateStatus(false, true);
        }
        return true;
    }
    if (kbd_pressed(K.ptnOrdNext)) {
        if (U.curOrder < g_song.orderLen - 1) {
            U.curOrder++;
            audioLock();
            if (following())
                player_set_position(U.curOrder);
            audioUnlock();
            setCurrentPattern(g_song.order[U.curOrder].pattern);
            updateOrderList();
            updateStatus(false, true);
        }
        return true;
    }
    if (kbd_pressed(K.ptnToOrder)) { StringGridOrd->setFocus(); return true; }
    if (kbd_pressed(K.ptnEditSong)) {
        SpeedButtonFollowOrder->setChecked(!SpeedButtonFollowOrder->isChecked());
        return true;
    }
    if (!U.selecting && kbd_pressed(K.ptnSelectKey)) {
        U.selecting = true;
        U.selX1 = chan;
        U.selY1 = U.curRow;
        U.selX2 = chan;
        U.selY2 = U.curRow;
        U.selChan = chan;
        U.selRow = U.curRow;
        U.selW = 1;
        U.selH = 1;
        return true;
    }
    if (kbd_held(K.ptnCurLeft)) {
        if (--U.curCol < 0)
            U.curCol = 0x4f;
        selectionFollowCursor();
        cursorColumnChanged();
        scrollToCursorChannel();
        return true;
    }
    if (kbd_held(K.ptnCurRight)) {
        if (++U.curCol > 0x4f)
            U.curCol = 0;
        selectionFollowCursor();
        cursorColumnChanged();
        scrollToCursorChannel();
        return true;
    }
    if (kbd_held(K.ptnCurUp)) { cursorMoveRows(-1, false); return true; }
    if (kbd_held(K.ptnCurDown)) { cursorMoveRows(1, false); return true; }
    if (kbd_held(K.ptnPageUp)) { cursorMoveRows(-UpDownPageStep->value(), true); return true; }
    if (kbd_held(K.ptnPageDown)) { cursorMoveRows(UpDownPageStep->value(), true); return true; }
    if (kbd_pressed(K.ptnHome)) {
        U.curRow = 0;
        selectionFollowCursor();
        updateStatus(false, true);
        return true;
    }
    if (kbd_pressed(K.ptnEnd)) {
        U.curRow = g_song.pattern[U.curPattern].length - 1;
        selectionFollowCursor();
        updateStatus(false, true);
        return true;
    }
    if (kbd_held(K.ptnNextChan)) {
        U.curCol += 8;
        if (U.curCol > 0x4f)
            U.curCol = col - 0x48;
        selectionFollowCursor();
        cursorColumnChanged();
        scrollToCursorChannel();
        return true;
    }
    if (kbd_held(K.ptnPrevChan)) {
        U.curCol -= 8;
        if (U.curCol < 0)
            U.curCol = col + 0x48;
        selectionFollowCursor();
        cursorColumnChanged();
        scrollToCursorChannel();
        return true;
    }
    if (kbd_held(K.ptnAutoStepInc) || kbd_held(K.ptnAutoStepDec)) {
        int step = UpDownAStep->value() + (kbd_held(K.ptnAutoStepInc) ? 1 : -1);
        if (step > 0x100)
            step = 0x100;
        if (step < 0)
            step = 0;
        U.autoStep = step;
        SpeedButtonAStep->setChecked(true);
        UpDownAStep->setEnabled(true);
        UpDownAStep->setValue(step);
        return true;
    }
    if (kbd_held(K.ptnPrevFxColumn)) {
        if (--U.fxColumn[chan] < 0)
            U.fxColumn[chan] = 3;
        updatePatternHeader();
        return true;
    }
    if (kbd_held(K.ptnNextFxColumn)) {
        if (++U.fxColumn[chan] > 3)
            U.fxColumn[chan] = 0;
        updatePatternHeader();
        return true;
    }

    Pattern *p = &g_song.pattern[U.curPattern];
    // edits that may change the song length refresh the time display afterwards
    auto edit = [&](auto fn) {
        undoPushPattern(U.curPattern);
        int before = songLength();
        fn();
        if (songLength() != before)
            updateStatus(true, true);
    };
    if (kbd_held(K.ptnInsNote)) {
        edit([&] { cell_insert(U.curPattern, U.curRow, chan); });
        return true;
    }
    if (kbd_held(K.ptnInsLine)) {
        edit([&] { for (int c = 0; c < CHANNELS; c++) cell_insert(U.curPattern, U.curRow, c); });
        return true;
    }
    if (kbd_held(K.ptnDelNote)) {
        edit([&] { cell_delete(U.curPattern, U.curRow, chan); });
        return true;
    }
    if (kbd_held(K.ptnDelLine)) {
        edit([&] { for (int c = 0; c < CHANNELS; c++) cell_delete(U.curPattern, U.curRow, c); });
        return true;
    }
    if (kbd_held(K.ptnClrNote)) {
        edit([&] {
            cell_clear(&p->cell[U.curRow][chan]);
            cursorMoveRows(U.autoStep, false);
        });
        return true;
    }
    if (kbd_held(K.ptnInpRepeat)) {
        edit([&] {
            Cell *c = &p->cell[U.curRow][chan];
            int field = U.curCol & 7;
            if (field == 0) {
                if (U.lastNote != NOTE_EMPTY) {
                    c->note = U.lastNote;
                    c->octave = U.lastOctave;
                }
            } else if (field <= 2) {
                if (U.lastVolume != 0)
                    c->volume = U.lastVolume;
            } else if (field <= 4) {
                if (U.lastIns != 0)
                    c->ins = U.lastIns;
            } else {
                if (U.lastFx >= 0)
                    c->fx[U.fxColumn[chan]] = (uint8_t)U.lastFx;
                if (U.lastParam != 0)
                    c->param[U.fxColumn[chan]] = U.lastParam;
            }
            cursorMoveRows(U.autoStep, false);
        });
        return true;
    }
    if (kbd_held(K.ptnPatternPrev)) { UpDownPtn->setValue(UpDownPtn->value() - 1); return true; }
    if (kbd_held(K.ptnPatternNext)) { UpDownPtn->setValue(UpDownPtn->value() + 1); return true; }
    if (kbd_held(K.ptnInstrPrev)) { UpDownAIns->setValue(UpDownAIns->value() - 1); return true; }
    if (kbd_held(K.ptnInstrNext)) { UpDownAIns->setValue(UpDownAIns->value() + 1); return true; }
    if (kbd_held(K.ptnClrLine)) {
        edit([&] {
            for (int c = 0; c < CHANNELS; c++)
                cell_clear(&p->cell[U.curRow][c]);
            cursorMoveRows(U.autoStep, false);
        });
        return true;
    }
    if (kbd_held(K.ptnClrField)) {
        edit([&] {
            Cell *c = &p->cell[U.curRow][chan];
            int field = U.curCol & 7;
            if (field == 0) {
                c->note = NOTE_EMPTY;
            } else if (field <= 2) {
                c->volume = 0;
            } else if (field <= 4) {
                c->ins = 0;
            } else {
                c->fx[U.fxColumn[chan]] = 0;
                c->param[U.fxColumn[chan]] = 0;
            }
            cursorMoveRows(U.autoStep, false);
        });
        return true;
    }
    if (kbd_pressed(K.ptnPlay)) { playFromCursor(false); return true; }
    if (kbd_pressed(K.ptnPlayLoop)) { playFromCursor(true); return true; }

    // ---- data entry
    bool step = false;
    bool playRow = false;
    Cell *c = &g_song.pattern[pat].cell[row][chan];
    int field = U.curCol & 7;
    int fxc = U.fxColumn[chan];
    if (field == 0) {
        bool line = kbd_pressed(K.ptnPlayLine);
        playRow = kbd_pressed(K.ptnPlayNote);
        if (playRow)
            soloChan = chan;
        playRow = playRow || line;
        int n = noteKeyPressed(CheckBoxPtnAutorepeat->isChecked());
        if (n >= 0) {
            undoPushPattern(U.curPattern);
            if (n < NOTE_OFF) {
                for (; n > 0xb; n -= 0xc)
                    octave++;
            } else {
                octave = 1;
            }
            if (octave < 9) {
                U.lastNote = (int16_t)n;
                c->note = (int16_t)n;
                c->octave = (int8_t)octave;
                U.lastOctave = (int8_t)octave;
                if (c->ins == 0) {
                    if (SpeedButtonAIns->isChecked() && n != NOTE_OFF)
                        c->ins = (int16_t)UpDownAIns->value();
                } else {
                    UpDownAIns->setValue(c->ins);
                }
                if (SpeedButtonAVol->isChecked())
                    c->volume = (int8_t)UpDownAVol->value();
                playRow = true;
                step = true;
            }
        }
    } else {
        int v = hexKeyPressed(field);
        if (v >= 0) {
            undoPushPattern(U.curPattern);
            step = true;
            int before = 0;
            if (field > 4)
                before = songLength();
            switch (field) {
            case 1: {
                int nv = (c->volume & 0xf) | v << 4;
                if (nv > 0x1f)
                    nv = 0x1f;
                c->volume = (int8_t)nv;
                step = false;
                U.lastVolume = (int8_t)nv;
                break;
            }
            case 2: {
                unsigned nv = ((int)c->volume & 0xf0u) | (v & 0xf);
                if (nv > 0x1f)
                    nv = 0x1f;
                c->volume = (int8_t)nv;
                U.lastVolume = (int8_t)nv;
                break;
            }
            case 3:
                c->ins = (int16_t)((c->ins & 0xf) | (uint16_t)v << 4);
                U.lastIns = c->ins;
                step = false;
                break;
            case 4:
                c->ins = (int16_t)((c->ins & 0xf0) | (uint16_t)v);
                U.lastIns = c->ins;
                break;
            case 5:
                c->fx[fxc] = (uint8_t)v;
                U.lastFx = (int8_t)c->fx[fxc];
                U.lastParam = c->param[fxc];
                step = false;
                break;
            case 6:
                c->param[fxc] = (uint8_t)((c->param[fxc] & 0xf) | (uint8_t)v << 4);
                U.lastParam = c->param[fxc];
                U.lastFx = (int8_t)c->fx[fxc];
                step = false;
                break;
            case 7:
                c->param[fxc] = (uint8_t)((c->param[fxc] & 0xf0) | (uint8_t)v);
                U.lastParam = c->param[fxc];
                U.lastFx = (int8_t)c->fx[fxc];
                break;
            }
            if (!CheckBoxPtnAutoHStep->isChecked()) {
                step = true;
            } else if (step) {
                // last digit of a field: back to its first digit, then step down
                if (field == 2 || field == 4)
                    U.curCol -= 1;
                else if (field == 7)
                    U.curCol -= 2;
            } else {
                U.curCol += 1;
                selectionFollowCursor();
                patternRedraw();
            }
            if (field > 4 && songLength() != before)
                updateStatus(true, true);
            playRow = field < 5;
            // effects that are audible on their own are played back immediately
            switch (c->fx[fxc]) {
            case 0x08: case 0x09: case 0x0c: case 0x0d: case 0x15: case 0x17: case 0x19: case 0x1c:
                playRow = true;
                break;
            case 0x0e: {
                unsigned hi = c->param[fxc] & 0xf0;
                if (hi <= 0x30 || hi == 0xe0 || hi == 0xf0)
                    playRow = true;
                break;
            }
            }
        }
    }
    if (playRow) {
        audioLock();
        player_start(U.curOrder, U.curRow, 1, 0, 0, soloChan);
        audioUnlock();
    }
    if (step) {
        cursorMoveRows(U.autoStep, false);
        return true;
    }
    // cursor keys and Tab never reach the focus system
    return g_keyState[0x26] || g_keyState[0x28] || g_keyState[0x25] || g_keyState[0x27] || g_keyState[0x09];
}
