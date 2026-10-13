// Undo, clipboard/block commands, order list and the pattern page's controls.
#include "mainwindow.h"

#include "dialogs.h"

static const char kClipMime[] = "application/x-tfm-mm-clipboard";     // "TFM MM Clipboard"

// ---------------------------------------------------------------- undo

enum { UNDO_PATTERN = 1, UNDO_INSTRUMENTS, UNDO_SAMPLES, UNDO_ORDERS, UNDO_HEADER, UNDO_SONG };

static void *undoArea(int type, int pattern, int *size)
{
    switch (type) {
    case UNDO_PATTERN:
        *size = sizeof(Pattern);
        return &g_song.pattern[pattern];
    case UNDO_INSTRUMENTS:
        *size = sizeof(g_song.ins);
        return g_song.ins;
    case UNDO_SAMPLES:
        *size = sizeof(g_song.sample);
        return g_song.sample;
    case UNDO_ORDERS:
        *size = 0x804;      // order list + length and loop position
        return g_song.order;
    case UNDO_HEADER:
        *size = 0x224;      // clocks, speeds, sample rate, texts, dates
        return &g_song.fmClock;
    }
    *size = 0;
    return nullptr;
}

// Packing the module counts as a save in the original; keep the counters out of undo snapshots.
static QByteArray packSong()
{
    uint16_t count = g_song.saveCount, date = g_song.dateModified;
    int len = 0;
    uint8_t *buf = song_pack(&len);
    g_song.saveCount = count;
    g_song.dateModified = date;
    QByteArray out((const char *)buf, buf ? len : 0);
    free(buf);
    return out;
}

// 0x412d2c
void MainWindow::undoReset()
{
    for (UndoEntry &e : undoRing) {
        e.data.clear();
        e.type = 0;
    }
    undoPos = 0;
    undoCount = 0;
    redoCount = 0;
    U.modified = false;
    setTitle();
}

// 0x412e24 / 0x412ec0: remember where the change happened, advance the ring
void MainWindow::undoCommit(bool withCursor)
{
    UndoEntry &e = undoRing[undoPos];
    if (!withCursor) {
        e.curPattern = -1;
    } else {
        e.curRow = U.curRow;
        e.curCol = U.curCol;
        e.curPattern = U.curPattern;
        e.curOrder = U.curOrder;
    }
    e.page = PageControlMain->currentIndex();
    e.focus = QApplication::focusWidget();
    undoPos = (undoPos + 1) % UNDO_MAX;
    redoCount = 0;
    if (undoCount < UNDO_MAX)
        undoCount++;
    U.modified = true;
    setTitle();
}

void MainWindow::undoPushPattern(int pat)
{
    UndoEntry &e = undoRing[undoPos];
    e.type = UNDO_PATTERN;
    e.pattern = pat;
    e.data = QByteArray((const char *)&g_song.pattern[pat], sizeof(Pattern));
    undoCommit(true);
}

void MainWindow::undoPushInstruments()
{
    UndoEntry &e = undoRing[undoPos];
    e.type = UNDO_INSTRUMENTS;
    e.data = QByteArray((const char *)g_song.ins, sizeof(g_song.ins));
    undoCommit(false);
}

void MainWindow::undoPushSamples()
{
    UndoEntry &e = undoRing[undoPos];
    e.type = UNDO_SAMPLES;
    e.data = QByteArray((const char *)g_song.sample, sizeof(g_song.sample));
    undoCommit(false);
}

void MainWindow::undoPushOrders()
{
    UndoEntry &e = undoRing[undoPos];
    e.type = UNDO_ORDERS;
    e.data = QByteArray((const char *)g_song.order, 0x804);
    undoCommit(true);
}

void MainWindow::undoPushHeader()
{
    UndoEntry &e = undoRing[undoPos];
    e.type = UNDO_HEADER;
    e.data = QByteArray((const char *)&g_song.fmClock, 0x224);
    undoCommit(false);
}

void MainWindow::undoPushSong()
{
    UndoEntry &e = undoRing[undoPos];
    e.type = UNDO_SONG;
    e.data = packSong();
    undoCommit(false);
}

// 0x412bd4: exchange the stored state with the live one, so the entry then holds the redo state
void MainWindow::undoSwap(int idx)
{
    UndoEntry &e = undoRing[idx];
    AudioLock lock;
    if (e.type == UNDO_SONG) {
        QByteArray now = packSong();
        song_unpack((uint8_t *)e.data.data(), (int)e.data.size(), 3);
        e.data = now;
        return;
    }
    int size;
    void *area = undoArea(e.type, e.pattern, &size);
    if (area == nullptr || e.data.size() != size)
        return;
    QByteArray now((const char *)area, size);
    memcpy(area, e.data.constData(), (size_t)size);
    e.data = now;
}

// 0x412f7c
void MainWindow::undoRestoreCursor(int idx)
{
    const UndoEntry &e = undoRing[idx];
    if (e.curPattern >= 0) {
        U.curRow = e.curRow;
        U.curCol = e.curCol;
        U.curPattern = e.curPattern;
        U.curOrder = e.curOrder;
    }
    PageControlMain->setCurrentIndex(e.page);
    if (e.focus)
        e.focus->setFocus();
}

// 0x413390
bool MainWindow::undoStep()
{
    int idx = (undoPos + UNDO_MAX - 1) % UNDO_MAX;
    if (undoCount == 0)
        return false;
    undoSwap(idx);
    undoRestoreCursor(idx);
    undoCount--;
    if (redoCount < UNDO_MAX)
        redoCount++;
    undoPos = idx;
    setTitle();
    return true;
}

// 0x413448
bool MainWindow::redoStep()
{
    int next = (undoPos + 1) % UNDO_MAX;
    if (redoCount == 0)
        return false;
    undoSwap(undoPos);
    undoRestoreCursor(undoPos);
    redoCount--;
    if (undoCount < UNDO_MAX)
        undoCount++;
    undoPos = next;
    setTitle();
    return true;
}

// 0x4271cc
void MainWindow::EditUndoClick(QObject *)
{
    unsigned rate = g_song.sampleRate;
    unsigned fm = g_song.fmClock, psg = g_song.psgClock, fps = g_song.fps;
    undoStep();
    if (rate != g_song.sampleRate) {
        AudioLock lock;
        samples_convert_all();
    }
    if (fm != g_song.fmClock || psg != g_song.psgClock || fps != g_song.fps) {
        audioClose(true);
        audioOpen(true);
        updateClockInfo();
    }
    refreshAll();
}

// 0x4271f8
void MainWindow::EditRedoClick(QObject *)
{
    unsigned rate = g_song.sampleRate;
    unsigned fm = g_song.fmClock, psg = g_song.psgClock, fps = g_song.fps;
    redoStep();
    if (rate != g_song.sampleRate) {
        AudioLock lock;
        samples_convert_all();
    }
    if (fm != g_song.fmClock || psg != g_song.psgClock || fps != g_song.fps) {
        audioClose(true);
        audioOpen(true);
        updateClockInfo();
    }
    refreshAll();
}

// ---------------------------------------------------------------- clipboard

// 0x4024f4: the block also goes to the system clipboard so it can be pasted into another instance
static void clipToSystem()
{
    QMimeData *md = new QMimeData;
    md->setData(kClipMime, QByteArray((const char *)&g_clip, sizeof(g_clip)));
    QApplication::clipboard()->setMimeData(md);
}

// 0x402560
static bool clipFromSystem()
{
    const QMimeData *md = QApplication::clipboard()->mimeData();
    if (md == nullptr || !md->hasFormat(kClipMime))
        return false;
    QByteArray b = md->data(kClipMime);
    if (b.size() != (int)sizeof(g_clip))
        return false;
    memcpy(&g_clip, b.constData(), sizeof(g_clip));
    return true;
}

// 0x41e848: changes whenever a speed effect (0F) in the current pattern changes
int MainWindow::songLength()
{
    int sum = 0;
    const Pattern *p = &g_song.pattern[U.curPattern];
    for (int r = 0; r < p->length; r++)
        for (int c = 0; c < CHANNELS; c++)
            for (int i = 0; i < 4; i++)
                if (p->cell[r][c].fx[i] == 0x0f)
                    sum += p->cell[r][c].param[i] + 0x800;
    return sum;
}

void MainWindow::EditSelectAllClick(QObject *)
{
    if (U.noPattern)
        return;
    U.selX1 = 0;
    U.selY1 = 0;
    U.selX2 = 10;
    U.selY2 = g_song.pattern[U.curPattern].length;
    U.selChan = 0;
    U.selRow = 0;
    U.selW = 10;
    U.selH = g_song.pattern[U.curPattern].length;
    patternRedraw();
}

void MainWindow::EditUnselectAllClick(QObject *)
{
    if (U.noPattern)
        return;
    U.selX1 = U.selY1 = U.selX2 = U.selY2 = 0;
    U.selChan = U.selRow = U.selW = U.selH = 0;
    patternRedraw();
}

void MainWindow::EditCutClick(QObject *)
{
    if (U.noPattern)
        return;
    undoPushPattern(U.curPattern);
    int before = songLength();
    bool ok;
    {
        AudioLock lock;
        ok = block_cut(U.curPattern, U.selChan, U.selRow, U.selW, U.selH);
    }
    if (ok) {
        clipToSystem();
        patternRedraw();
        if (before != songLength())
            updateStatus(true, true);
    }
}

void MainWindow::EditCopyClick(QObject *)
{
    if (U.noPattern)
        return;
    if (block_copy(U.curPattern, U.selChan, U.selRow, U.selW, U.selH))
        clipToSystem();
}

void MainWindow::EditPasteClick(QObject *)
{
    if (U.noPattern)
        return;
    undoPushPattern(U.curPattern);
    int before = songLength();
    if (!clipFromSystem())
        return;
    bool ok;
    {
        AudioLock lock;
        ok = block_paste(U.curPattern, U.curCol / 8, U.curRow, false);
    }
    if (ok) {
        patternRedraw();
        if (before != songLength())
            updateStatus(true, true);
    }
}

void MainWindow::EditPasteOverClick(QObject *)
{
    if (U.noPattern)
        return;
    undoPushPattern(U.curPattern);
    int before = songLength();
    if (!clipFromSystem())
        return;
    bool ok;
    {
        AudioLock lock;
        ok = block_paste(U.curPattern, U.curCol / 8, U.curRow, true);
    }
    if (ok) {
        patternRedraw();
        if (before != songLength())
            updateStatus(true, true);
    }
}

void MainWindow::EditDeleteClick(QObject *)
{
    if (U.noPattern)
        return;
    undoPushPattern(U.curPattern);
    int before = songLength();
    bool ok;
    {
        AudioLock lock;
        ok = block_clear(U.curPattern, U.selChan, U.selRow, U.selW, U.selH);
    }
    if (ok) {
        patternRedraw();
        if (before != songLength())
            updateStatus(true, true);
    }
}

// ---------------------------------------------------------------- cleanup

// 0x424284
void MainWindow::EditCleanupInstrumentsClick(QObject *)
{
    undoPushSong();
    int n;
    {
        AudioLock lock;
        n = cleanup_unused_instruments();
    }
    if (n == 0)
        return;
    updateInsList(-1, true);
    updateInstrumentControls();
    QString msg = QString("%1 unused instruments were removed.\nRearrange instruments by order?").arg(n);
    if (messageBox(msg, "Complete", 4) == 6) {
        undoPushSong();
        {
            AudioLock lock;
            rearrange_instruments();
        }
        updateInsList(-1, true);
        updateInstrumentControls();
        patternRedraw();
    }
}

void MainWindow::EditRearrangeInstrumentsClick(QObject *)
{
    undoPushSong();
    {
        AudioLock lock;
        rearrange_instruments();
    }
    updateInsList(-1, true);
    updateInstrumentControls();
    patternRedraw();
}

void MainWindow::EditRearrangePatternsClick(QObject *)
{
    if (messageBox("All unused patterns will be deleted. Continue?", "Warning", 4) != 6)
        return;
    undoPushSong();
    {
        AudioLock lock;
        rearrange_patterns();
    }
    setCurrentPattern(g_song.order[U.curOrder].pattern);
    updateStatus(true, false);
    updateOrderList();
}

// ---------------------------------------------------------------- block menu

void MainWindow::BlockExpandClick(QObject *)
{
    if (U.noPattern)
        return;
    undoPushPattern(U.curPattern);
    bool ok;
    {
        AudioLock lock;
        ok = block_expand(U.curPattern, U.selChan, U.selRow, U.selW, U.selH);
    }
    if (ok) {
        UpDownPtnLen->setValue(g_song.pattern[U.curPattern].length);
        patternRedraw();
        updateStatus(true, true);
    }
}

void MainWindow::BlockShrinkClick(QObject *)
{
    if (U.noPattern)
        return;
    undoPushPattern(U.curPattern);
    AudioLock lock;
    if (block_shrink(U.curPattern, U.selChan, U.selRow, U.selW, U.selH))
        patternRedraw();
}

void MainWindow::BlockVolumeGradientClick(QObject *)
{
    if (U.noPattern)
        return;
    undoPushPattern(U.curPattern);
    AudioLock lock;
    if (block_volume_gradient(U.curPattern, U.selChan, U.selRow, U.selW, U.selH))
        patternRedraw();
}

void MainWindow::BlockEffectGradientClick(QObject *)
{
    if (U.noPattern)
        return;
    undoPushPattern(U.curPattern);
    AudioLock lock;
    if (block_effect_gradient(U.curPattern, U.selChan, U.selRow, U.selW, U.selH, U.fxColumn))
        patternRedraw();
}

// 0x424ed4
void MainWindow::BlockVolumeScaleClick(QObject *)
{
    if (U.noPattern)
        return;
    kbd_clear();
    ScaleDialog *d = ScaleDialog::instance(this);
    d->setWindowTitle("Volume scale");
    d->exec();
    if (!d->ok)
        return;
    undoPushPattern(U.curPattern);
    AudioLock lock;
    if (block_volume_scale(U.curPattern, U.selChan, U.selRow, U.selW, U.selH, d->from, d->to))
        patternRedraw();
}

// 0x426488
void MainWindow::BlockEffectScaleClick(QObject *)
{
    if (U.noPattern)
        return;
    kbd_clear();
    ScaleDialog *d = ScaleDialog::instance(this);
    d->setWindowTitle("Effect scale");
    d->exec();
    if (!d->ok)
        return;
    undoPushPattern(U.curPattern);
    AudioLock lock;
    if (block_effect_scale(U.curPattern, U.selChan, U.selRow, U.selW, U.selH, d->from, d->to, U.fxColumn))
        patternRedraw();
}

// 0x424190
void MainWindow::BlockDuplicateClick(QObject *)
{
    if (U.noPattern)
        return;
    kbd_clear();
    DuplicateDialog *d = DuplicateDialog::instance(this);
    d->exec();
    if (!d->ok)
        return;
    undoPushPattern(U.curPattern);
    AudioLock lock;
    if (block_duplicate(U.curPattern, U.selChan, U.selRow, U.selW, U.selH, d->count))
        patternRedraw();
}

// 0x425f28
void MainWindow::BlockAssignInsClick(QObject *)
{
    if (U.noPattern)
        return;
    undoPushPattern(U.curPattern);
    AudioLock lock;
    if (block_assign_instrument(U.curPattern, U.selChan, U.selRow, U.selW, U.selH, UpDownAIns->value()))
        patternRedraw();
}

void MainWindow::PtnPopupAssignInsClick(QObject *s) { BlockAssignInsClick(s); }

// 0x41dde8
void MainWindow::blockTransposeQuick(int semitones)
{
    if (U.noPattern)
        return;
    undoPushPattern(U.curPattern);
    AudioLock lock;
    if (block_transpose(U.curPattern, U.curOrder, U.selChan, U.selRow, U.selW, U.selH, semitones, -1, false))
        patternRedraw();
}

void MainWindow::PtnPopupOctaveUpClick(QObject *) { blockTransposeQuick(12); }
void MainWindow::PtnPopupOctaveDownClick(QObject *) { blockTransposeQuick(-12); }
void MainWindow::PtnPopupSemitoneUpClick(QObject *) { blockTransposeQuick(1); }
void MainWindow::PtnPopupSemiToneDownClick(QObject *) { blockTransposeQuick(-1); }

// 0x423eb8: the dialog's "Check" button runs a dry pass and brings the dialog back
void MainWindow::BlockTransposeClick(QObject *)
{
    if (U.noPattern)
        return;
    kbd_clear();
    TransposeDialog *d = TransposeDialog::instance(this);
    d->flags |= TransposeDialog::CHECK;
    while (d->flags & TransposeDialog::CHECK) {
        d->exec();
        bool result = false;
        if (!d->ok)
            continue;
        int ins = (d->flags & TransposeDialog::CUR_INSTRUMENT) ? UpDownAIns->value() : -1;
        bool test = (d->flags & TransposeDialog::CHECK) != 0;
        int semis = d->UpDownAmount->value();
        if (d->flags & TransposeDialog::WHOLE_TRACK) {
            if (!test)
                undoPushSong();
            int chan = 0, width = CHANNELS;
            if (d->flags & TransposeDialog::CUR_CHANNEL) {
                chan = U.curCol / 8;
                width = 1;
            }
            bool used[MAX_PATTERNS] = {};
            for (int i = 0; i < g_song.orderLen; i++)
                used[g_song.order[i].pattern] = true;
            result = test;
            AudioLock lock;
            for (int pat = 0; pat < MAX_PATTERNS; pat++) {
                if (!used[pat])
                    continue;
                bool r = block_transpose(pat, U.curOrder, chan, 0, width, g_song.pattern[pat].length, semis, ins, test);
                if (test) {
                    if (!r) {
                        result = false;
                        break;
                    }
                } else if (r) {
                    result = true;
                }
            }
        } else {
            if (!test)
                undoPushPattern(U.curPattern);
            AudioLock lock;
            result = block_transpose(U.curPattern, U.curOrder, U.selChan, U.selRow, U.selW, U.selH, semis, ins, test);
        }
        if (!test) {
            if (result)
                patternRedraw();
        } else if (!result) {
            messageBox("Some notes are out of range!\n", "Warning", 0);
        } else {
            messageBox("None of notes are out of range.\n", "OK", 0);
        }
    }
}

// 0x427074
void MainWindow::BlockChanExchangeClick(QObject *)
{
    if (U.noPattern)
        return;
    kbd_clear();
    ChnExchangeDialog *d = ChnExchangeDialog::instance(this);
    d->exec();
    if (!d->ok)
        return;
    undoPushSong();
    bool changed = false;
    {
        AudioLock lock;
        if (d->range == 0) {
            changed = block_exchange_channels(U.curPattern, U.selRow, U.selH, d->chanA, d->chanB);
        } else if (d->range == 1) {
            changed = block_exchange_channels(U.curPattern, 0, 0x100, d->chanA, d->chanB);
        } else if (d->range == 2) {
            bool used[MAX_PATTERNS] = {};
            for (int i = 0; i < g_song.orderLen; i++)
                used[g_song.order[i].pattern] = true;
            for (int pat = 0; pat < MAX_PATTERNS; pat++)
                if (used[pat])
                    changed |= block_exchange_channels(pat, 0, 0x100, d->chanA, d->chanB);
        }
    }
    if (changed)
        patternRedraw();
}

// ---------------------------------------------------------------- pattern menu

void MainWindow::PatternExpandClick(QObject *)
{
    if (U.noPattern)
        return;
    undoPushPattern(U.curPattern);
    {
        AudioLock lock;
        pattern_expand(U.curPattern);
    }
    patternRedraw();
    UpDownPtnLen->setValue(g_song.pattern[U.curPattern].length);
    updateStatus(true, true);
}

void MainWindow::PatternShrinkClick(QObject *)
{
    if (U.noPattern)
        return;
    undoPushPattern(U.curPattern);
    {
        AudioLock lock;
        pattern_shrink(U.curPattern);
    }
    patternRedraw();
    UpDownPtnLen->setValue(g_song.pattern[U.curPattern].length);
    updateStatus(true, true);
}

// ---------------------------------------------------------------- pattern page controls

// 0x421450: the pattern number edits the current position of the order list
void MainWindow::EditPtnChange(QObject *)
{
    U.curPattern = UpDownPtn->value();
    if (!Timer1->isActive() && U.curPattern != g_song.order[U.curOrder].pattern) {
        undoPushOrders();
        {
            AudioLock lock;
            g_song.order[U.curOrder].pattern = (uint8_t)U.curPattern;
        }
        UpDownPtnLen->setValue(g_song.pattern[U.curPattern].length);
        patternRedraw();
        updateOrderList();
    }
}

// 0x421540
void MainWindow::EditPtnLenChange(QObject *)
{
    int len = UpDownPtnLen->value();
    if (!Timer1->isActive() && len != g_song.pattern[U.curPattern].length) {
        undoPushPattern(U.curPattern);
        {
            AudioLock lock;
            g_song.pattern[U.curPattern].length = len;
        }
        updateStatus(true, true);
        if (len <= U.curRow) {
            U.curRow = len - 1;
            updateStatus(false, true);
        }
        patternRedraw();
    }
    PatternExpand->setEnabled(g_song.pattern[U.curPattern].length < 0x81);
    PatternShrink->setEnabled(g_song.pattern[U.curPattern].length > 1);
}

void MainWindow::EditOctaveChange(QObject *)
{
    U.octave = UpDownOctave->value();
    PaintBoxKeys->update();
}

// 0x421cdc
void MainWindow::EditSpdEvenChange(QObject *)
{
    if (Timer1->isActive())
        return;
    if ((unsigned)UpDownSpdEven->value() != g_song.speedEven) {
        undoPushHeader();
        g_song.speedEven = (uint32_t)UpDownSpdEven->value();
        updateStatus(true, true);
    }
    updateBpm();
}

void MainWindow::EditSpdOddChange(QObject *)
{
    if (Timer1->isActive())
        return;
    if ((unsigned)UpDownSpdOdd->value() != g_song.speedOdd) {
        undoPushHeader();
        g_song.speedOdd = (uint32_t)UpDownSpdOdd->value();
        updateStatus(true, true);
    }
    updateBpm();
}

void MainWindow::SpeedButtonSpdEqualClick(QObject *)
{
    UpDownSpdOdd->setValue(UpDownSpdEven->value());
}

// 0x4239d0
void MainWindow::EditInterChange(QObject *)
{
    if (Timer1->isActive())
        return;
    if ((unsigned)UpDownInter->value() != g_song.interleave) {
        undoPushHeader();
        g_song.interleave = (uint32_t)UpDownInter->value();
        updateStatus(true, true);
    }
}

// 0x42138c
void MainWindow::EditAStepChange(QObject *)
{
    if (!UpDownAStep->isEnabled())
        return;
    U.autoStep = UpDownAStep->value();
    if (UpDownAStep->value() == 0) {
        UpDownAStep->setEnabled(false);
        SpeedButtonAStep->setChecked(false);
    }
}

// 0x42492c
void MainWindow::SpeedButtonAStepClick(QObject *)
{
    UpDownAStep->setEnabled(SpeedButtonAStep->isChecked());
    if (SpeedButtonAStep->isChecked()) {
        if (UpDownAStep->value() == 0)
            UpDownAStep->setValue(1);
        U.autoStep = UpDownAStep->value();
    } else {
        U.autoStep = 0;
    }
}

void MainWindow::SpeedButtonAVolClick(QObject *)
{
    UpDownAVol->setEnabled(SpeedButtonAVol->isChecked());
}

// 0x4249ac
void MainWindow::EditAInsChange(QObject *)
{
    if (UpDownAIns->value() > 0)
        U.curIns = UpDownAIns->value() - 1;
}

void MainWindow::EditAVolChange(QObject *) {}

void MainWindow::SpeedButtonFollowOrderClick(QObject *)
{
    patternRedraw();
    PaintBoxPtnPreview->update();
}

// ---------------------------------------------------------------- order list

// 0x426c00
void MainWindow::orderMouseDown(int col)
{
    StringGridOrd->setSelected(col);
    if (col < 0 || col >= g_song.orderLen) {
        U.curOrder = col;
        setNoPattern(true);
        return;
    }
    U.curOrder = col;
    {
        AudioLock lock;
        if (following())
            player_set_position(col);
    }
    setCurrentPattern(g_song.order[U.curOrder].pattern);
    updateOrderList();
    updateStatus(false, true);
    if (U.noPattern)
        setNoPattern(false);
}

// 0x426740: dragging a position onto another one exchanges them
void MainWindow::orderDrop(int from, int to)
{
    if (Timer1->isActive())
        return;
    undoPushOrders();
    bool ok;
    {
        AudioLock lock;
        ok = order_swap(from, to);
    }
    if (ok) {
        U.curOrder = to;
        setCurrentPattern(g_song.order[to].pattern);
        updateOrderList();
        updateStatus(false, true);
    }
}

// 0x421754
void MainWindow::orderKeyDown(int vk)
{
    if (vk == 0x25 || vk == 0x27) {
        int col = StringGridOrd->selected();
        if (vk == 0x25 && col > 0)
            col--;
        if (vk == 0x27 && col < 0xff)
            col++;
        StringGridOrd->setSelected(col);
        U.curOrder = col;
        if (col < g_song.orderLen) {
            {
                AudioLock lock;
                if (following())
                    player_set_position(col);
            }
            setCurrentPattern(g_song.order[U.curOrder].pattern);
            updateOrderList();
            updateStatus(false, true);
            if (U.noPattern)
                setNoPattern(false);
        } else {
            setNoPattern(true);
        }
        return;
    }
    if (kbd_pressed(K.ordToPattern)) {
        if (U.curOrder >= g_song.orderLen) {
            U.curOrder = g_song.orderLen - 1;
            StringGridOrd->setSelected(U.curOrder);
            setCurrentPattern(g_song.order[U.curOrder].pattern);
            setNoPattern(false);
        }
        ScrollBoxPtn->setFocus();
        return;
    }
    if (Timer1->isActive())
        return;
    if (U.curOrder <= g_song.orderLen && U.curOrder < MAX_ORDERS) {
        // typing at the first free position appends it to the list
        int pat = g_song.order[U.curOrder].pattern;
        bool changed = false;
        if (vk > 0x2f && vk < 0x3a) {
            pat = vk + pat * 10 - 0x30;
            if (pat > 0xff)
                pat = vk - 0x30;
            changed = true;
        }
        if (!changed && kbd_held(K.ordPatternInc)) {
            if (g_song.orderLen == U.curOrder)
                pat = 0;
            else if (pat < 0xff)
                pat++;
            changed = true;
        }
        if (!changed && kbd_held(K.ordPatternDec)) {
            if (g_song.orderLen == U.curOrder)
                pat = 0;
            else if (pat > 0)
                pat--;
            changed = true;
        }
        if (changed) {
            undoPushOrders();
            {
                AudioLock lock;
                if (g_song.orderLen == U.curOrder && U.curOrder < 0x100)
                    g_song.orderLen++;
                g_song.order[U.curOrder].pattern = (uint8_t)pat;
            }
            setCurrentPattern(pat);
            updateOrderList();
            if (U.noPattern)
                setNoPattern(false);
            return;
        }
    }
    if (U.curOrder >= g_song.orderLen)
        return;
    if (vk == 8) {
        undoPushOrders();
        int pat = g_song.order[U.curOrder].pattern / 10;
        {
            AudioLock lock;
            g_song.order[U.curOrder].pattern = (uint8_t)pat;
        }
        setCurrentPattern(pat);
        updateOrderList();
        if (U.noPattern)
            setNoPattern(false);
        return;
    }
    if (kbd_pressed(K.ordLoopPosition))
        OrderSetLoopPosClick();
    else if (kbd_pressed(K.ordDelete))
        OrderDeletePosClick();
    else if (kbd_pressed(K.ordInsertSame))
        OrderInsertSameClick();
    else if (kbd_pressed(K.ordInsertCopy))
        OrderInsertCopyClick();
    else if (kbd_pressed(K.ordInsertNew))
        OrderInsertNewClick();
    else if (kbd_pressed(K.ordMovePrev))
        OrderMovePrevClick();
    else if (kbd_pressed(K.ordMoveNext))
        OrderMoveNextClick();
}

// 0x426ac4
void MainWindow::PopupMenuOrdPopup(QObject *)
{
    OrderDeletePos->setEnabled(g_song.orderLen > 1);
    OrderMovePrev->setEnabled(U.curOrder > 0);
    OrderMoveNext->setEnabled(U.curOrder < g_song.orderLen - 1);
}

void MainWindow::OrderSetLoopPosClick(QObject *)
{
    if (Timer1->isActive() || U.curOrder >= g_song.orderLen)
        return;
    undoPushOrders();
    g_song.loopPos = (uint16_t)StringGridOrd->selected();
    updateOrderList();
}

void MainWindow::OrderInsertSameClick(QObject *)
{
    if (Timer1->isActive() || U.curOrder >= g_song.orderLen)
        return;
    undoPushOrders();
    AudioLock lock;
    if (order_insert(U.curOrder, 0)) {
        updateOrderList();
        updateStatus(false, true);
        setCurrentPattern(g_song.order[U.curOrder].pattern);
    }
}

void MainWindow::OrderInsertNewClick(QObject *)
{
    if (Timer1->isActive() || U.curOrder >= g_song.orderLen)
        return;
    undoPushSong();
    AudioLock lock;
    if (order_insert(U.curOrder, 1)) {
        updateOrderList();
        updateStatus(false, true);
        setCurrentPattern(g_song.order[U.curOrder].pattern);
    }
}

void MainWindow::OrderInsertCopyClick(QObject *)
{
    if (Timer1->isActive() || U.curOrder >= g_song.orderLen)
        return;
    undoPushSong();
    AudioLock lock;
    if (order_insert(U.curOrder, 2)) {
        updateOrderList();
        updateStatus(false, true);
        setCurrentPattern(g_song.order[U.curOrder].pattern);
    }
}

// 0x4226c0
void MainWindow::OrderDeletePosClick(QObject *)
{
    if (Timer1->isActive() || U.curOrder >= g_song.orderLen)
        return;
    undoPushOrders();
    bool ok;
    {
        AudioLock lock;
        ok = order_delete(U.curOrder);
    }
    if (ok) {
        if (g_song.orderLen <= U.curOrder)
            U.curOrder = g_song.orderLen - 1;
        setCurrentPattern(g_song.order[U.curOrder].pattern);
        updateOrderList();
        updateStatus(false, true);
    }
}

void MainWindow::OrderMovePrevClick(QObject *)
{
    if (Timer1->isActive())
        return;
    bool ok;
    {
        AudioLock lock;
        ok = order_swap(U.curOrder, U.curOrder - 1);
    }
    if (ok) {
        U.curOrder--;
        setCurrentPattern(g_song.order[U.curOrder].pattern);
        updateOrderList();
        updateStatus(false, true);
    }
}

void MainWindow::OrderMoveNextClick(QObject *)
{
    if (Timer1->isActive())
        return;
    bool ok;
    {
        AudioLock lock;
        ok = order_swap(U.curOrder, U.curOrder + 1);
    }
    if (ok) {
        U.curOrder++;
        setCurrentPattern(g_song.order[U.curOrder].pattern);
        updateOrderList();
        updateStatus(false, true);
    }
}
