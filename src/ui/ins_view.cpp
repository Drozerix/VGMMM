// Instrument and sample pages: knobs, envelope/keyboard/waveform drawing, note preview.
#include "mainwindow.h"

#include <cmath>

#include "dialogs.h"

// ---------------------------------------------------------------- knobs

// 0x415224
void MainWindow::createKnobs()
{
    static const int spacing[9] = {36, 36, 36, 42, 36, 42, 36, 42, 36};
    static const int mins[9] = {0, 0, 0, 0, 127, 15, 0, -3, 0};
    static const int maxs[9] = {31, 31, 31, 15, 0, 0, 15, 3, 3};
    static const int ticks[9] = {8, 8, 8, 8, 16, 8, 8, 7, 4};
    QWidget *panels[4] = {PanelOp1, PanelOp2, PanelOp3, PanelOp4};
    auto begin = [this](Knob *k) {
        if (k->tag < 1000)
            undoPushInstruments();
        else
            undoPushSamples();
    };
    int n = 0;
    for (int op = 0; op < 4; op++) {
        int x = 0x22;
        for (int i = 0; i < 9; i++) {
            Knob *k = new Knob(panels[op], x, 6, mins[i], maxs[i], ticks[i], op * 10 + i);
            k->setValue(i == 7 ? 0 : mins[i]);
            k->onChange = [this](int tag, int value) { knobChanged(tag, value); };
            k->onBeginEdit = begin;
            knobs[n++] = k;
            x += spacing[i];
        }
    }
    static const int extraX[3] = {0x226, 0x24a, 0x26e};
    static const int extraMax[3] = {7, 3, 7};
    static const int extraTicks[3] = {8, 4, 8};
    for (int i = 0; i < 3; i++) {
        Knob *k = new Knob(PanelOp1, extraX[i], 6, 0, extraMax[i], extraTicks[i], 100 + i);
        k->onChange = [this](int tag, int value) { knobChanged(tag, value); };
        k->onBeginEdit = begin;
        knobs[n++] = k;
    }
    knobPitch = new Knob(GroupBoxSample, 0x82, 0x1e, -0xff, 0xff, 7, 1000);
    knobPitch->setValue(0);
    // 0x4161cc
    knobPitch->onChange = [](int, int value) {
        U.samplePending = U.curSample;
        g_song.sample[U.curSample].tune = value;
    };
    knobPitch->onBeginEdit = begin;
    knobGain = new Knob(GroupBoxSample, 0xaa, 0x1e, 0, 0xff, 0x10, 1001);
    knobGain->setValue(0x80);
    // 0x4161b4
    knobGain->onChange = [](int, int value) {
        U.samplePending = U.curSample;
        g_song.sample[U.curSample].volume = (uint8_t)value;
    };
    knobGain->onBeginEdit = begin;
}

// 0x414fec: a knob (or the SSG-EG list) changed a parameter of the current instrument
void MainWindow::knobChanged(int tag, int value)
{
    Instrument *ins = &g_song.ins[U.curIns];
    bool wasEmpty = ins_is_empty(ins);
    bool refresh = false;
    uint8_t v = (uint8_t)value;
    if (tag < 100) {
        int opn = tag / 10;
        Operator *op = &ins->op[opn];
        switch (tag % 10) {
        case 0: op->ar = v; break;
        case 1: op->dr = (uint8_t)((op->dr & 0x80) | v); break;
        case 2: op->sr = v; break;
        case 3: op->rr = v; break;
        case 4: op->tl = v; refresh = true; break;
        case 5: op->sl = v; break;
        case 6: op->mult = v; refresh = true; break;
        case 7: op->dt = (int8_t)value; refresh = true; break;
        case 8: op->rs = v; break;
        case 9: {
            op->ssg = v;
            if (op->ssg < 8)
                op->ssg = 0;
            PaintBox *eg[4] = {ImageEG1, ImageEG2, ImageEG3, ImageEG4};
            eg[opn]->update();
            break;
        }
        }
        if (tag % 10 < 6) {
            PaintBox *env[4] = {PaintBoxEnv1, PaintBoxEnv2, PaintBoxEnv3, PaintBoxEnv4};
            env[opn]->update();
        }
    } else if (tag == 100) {
        refresh = true;
        ins->fb = v;
    } else if (tag == 101) {
        refresh = true;
        ins->ams = v;
    } else if (tag == 102) {
        refresh = true;
        ins->fms = v;
    }
    if (refresh) {
        AudioLock lock;
        player_refresh_instrument(U.curIns);
    }
    if (ins_is_empty(ins) != wasEmpty)
        updateInsList(U.curIns, true);
}

// 0x414d00
void MainWindow::updateInstrumentControls()
{
    const Instrument *ins = &g_song.ins[U.curIns];
    for (int op = 0; op < 4; op++) {
        // (knobs report back through knobChanged, so read each value before setting it)
        Operator o = ins->op[op];
        Knob **k = &knobs[op * 9];
        k[0]->setValue(o.ar);
        k[1]->setValue(o.dr & 0x1f);
        k[2]->setValue(o.sr);
        k[3]->setValue(o.rr);
        k[4]->setValue(o.tl);
        k[5]->setValue(o.sl);
        k[6]->setValue(o.mult);
        k[7]->setValue(o.dt);
        k[8]->setValue(o.rs);
    }
    knobs[36]->setValue(ins->fb);
    knobs[37]->setValue(ins->ams);
    knobs[38]->setValue(ins->fms);
    for (PaintBox *pb : {ImageAM1, ImageAM2, ImageAM3, ImageAM4, PaintBoxEnv1, PaintBoxEnv2, PaintBoxEnv3,
                         PaintBoxEnv4, PaintBoxAlgo, ImageEG1, ImageEG2, ImageEG3, ImageEG4})
        pb->update();
}

// 0x414f50
void MainWindow::setAlgorithm(int alg)
{
    if (alg != g_song.ins[U.curIns].alg) {
        undoPushInstruments();
        g_song.ins[U.curIns].alg = (uint8_t)alg;
        updateInstrumentControls();
    }
}

// 0x414f9c: list entry 0 is "off", 1..8 are the SSG-EG modes 8..15
void MainWindow::setSsgEg(int value, int op)
{
    if (value > 0)
        value += 7;
    if (value != g_song.ins[U.curIns].op[op].ssg) {
        undoPushInstruments();
        knobChanged(op * 10 + 9, value);
    }
}

// ---------------------------------------------------------------- instrument list

static QString slotText(int idx, const char *name)
{
    return QString("%1: ").arg(idx + 1, 2, 16, QChar('0')).toUpper() + QString::fromLatin1(name);
}

// 0x414404
void MainWindow::updateInsList(int idx, bool setName)
{
    updating = true;
    if (idx < 0) {
        ListBoxIns->clear();
        for (int i = 0; i < MAX_INSTRUMENTS; i++)
            ListBoxIns->addItem(slotText(i, g_song.ins[i].name));
        ListBoxIns->setCurrentRow(U.curIns);
    } else if (idx < ListBoxIns->count()) {
        ListBoxIns->item(idx)->setText(slotText(idx, g_song.ins[idx].name));
        ListBoxIns->item(idx)->setSelected(true);
    }
    updating = false;
    if (setName) {
        QString name = QString::fromLatin1(g_song.ins[U.curIns].name);
        if (EditInsName->text() != name)
            EditInsName->setText(name);
    }
}

// 0x415abc
void MainWindow::setInsName(int idx, const QString &s)
{
    QByteArray b = s.toLatin1();
    memset(g_song.ins[idx].name, 0, 0x10);
    memcpy(g_song.ins[idx].name, b.constData(), (size_t)qMin(b.size(), 0x10));
}

// 0x422228
void MainWindow::selectInstrument(int idx)
{
    U.curIns = idx;
    EditInsName->setText(QString::fromLatin1(g_song.ins[U.curIns].name));
    updateInstrumentControls();
    UpDownAIns->setValue(U.curIns + 1);
}

// 0x421d98
void MainWindow::EditInsNameChange(QObject *)
{
    setInsName(U.curIns, EditInsName->text());
    updateInsList(U.curIns, true);
}

// 0x42422c
void MainWindow::SpeedButtonInsNewClick(QObject *)
{
    if (messageBox("Clear instrument?", "Confirm", 4) != 6)
        return;
    undoPushInstruments();
    ins_clear(&g_song.ins[U.curIns]);
    updateInsList(U.curIns, true);
    updateInstrumentControls();
}

// 0x423054: several files go into the following empty slots
void MainWindow::SpeedButtonInsOpenClick(QObject *)
{
    kbd_clear();
    QStringList files = QFileDialog::getOpenFileNames(
        this, "Open instrument", U.pathInstrument,
        "All supported formats (*.vgi *.tfi *.dat *.y12 *.ins *.bin);;VGM Music Maker FM instrument (*.vgi);;"
        "TFM Music Maker instrument (*.tfi);;Yamaha 2612 Instrument Editor (*.dat);;Gens KMod channel dump (*.y12);;"
        "MVS Tracker MD instrument (*.ins);;SMPS instrument (*.bin)");
    if (files.isEmpty())
        return;
    undoPushInstruments();
    for (int n = 0; n < files.size(); n++) {
        int slot = U.curIns;
        if (n != 0) {
            slot = -1;
            for (int i = U.curIns; i < MAX_INSTRUMENTS; i++)
                if (ins_is_empty(&g_song.ins[i])) {
                    slot = i;
                    break;
                }
            if (slot < 0) {
                messageBox("No room to load all the instruments", "Error", 0);
                break;
            }
        }
        if (!ins_load(&g_song.ins[slot], files[n].toLocal8Bit().constData())) {
            messageBox("Can't open instrument '" + files[n] + "'", "Error", 0);
            continue;
        }
        setInsName(slot, QFileInfo(files[n]).completeBaseName());
        if (n == 0) {
            updateInstrumentControls();
            updateInsList(slot, true);
        } else {
            updateInsList(slot, false);
        }
    }
    U.pathInstrument = QFileInfo(files.first()).absolutePath();
}

// 0x41ba78
static QString safeFileName(QString s)
{
    for (QChar &c : s)
        if (QString("\\/:*?<>").contains(c))
            c = '_';
    return s;
}

// 0x4233c8: with several list entries selected each goes to "<name>.vgi" in the chosen folder
void MainWindow::SpeedButtonInsSaveClick(QObject *)
{
    kbd_clear();
    QString start = QDir(U.pathInstrument).filePath(safeFileName(EditInsName->text()) + ".vgi");
    QString fn = QFileDialog::getSaveFileName(this, "Save instrument", start, "VGM Music Maker FM instrument (*.vgi)");
    if (fn.isEmpty())
        return;
    if (QFileInfo(fn).suffix().isEmpty())
        fn += ".vgi";
    QString dir = QFileInfo(fn).absolutePath();
    QList<QListWidgetItem *> sel = ListBoxIns->selectedItems();
    if (sel.size() < 2) {
        ins_save_vgi(&g_song.ins[U.curIns], fn.toLocal8Bit().constData());
    } else {
        for (int i = 0; i < ListBoxIns->count(); i++) {
            if (!ListBoxIns->item(i)->isSelected())
                continue;
            QString name = QDir(dir).filePath(safeFileName(ListBoxIns->item(i)->text().mid(4, 0x10)) + ".vgi");
            if (QFile::exists(name) && messageBox("File " + name + " already exists, overwrite?", "Confirm", 4) != 6)
                continue;
            ins_save_vgi(&g_song.ins[i], name.toLocal8Bit().constData());
        }
    }
    U.pathInstrument = dir;
}

// 0x422e14
void MainWindow::SpeedButtonInsExchClick(QObject *)
{
    kbd_clear();
    InsCopyDialog *d = InsCopyDialog::instance(this);
    d->instruments = true;
    d->source = U.curIns;
    d->dest = U.curIns + 1;
    if (d->dest > 0xfe)
        d->dest = U.curIns - 1;
    d->exec();
    if (d->copy) {
        undoPushInstruments();
        g_song.ins[d->dest] = g_song.ins[d->source];
        U.curIns = d->dest;
        updateInstrumentControls();
        updateInsList(U.curIns, true);
        updating = true;
        ListBoxIns->setCurrentRow(U.curIns);
        updating = false;
        UpDownAIns->setValue(U.curIns + 1);
    }
    if (d->exchange) {
        if (!d->exchangeNumbers)
            undoPushInstruments();
        else
            undoPushSong();
        Instrument t = g_song.ins[d->source];
        g_song.ins[d->source] = g_song.ins[d->dest];
        g_song.ins[d->dest] = t;
        if (d->exchangeNumbers)
            ins_swap_refs(d->source, d->dest);
        U.curIns = d->dest;
        updateInstrumentControls();
        updateInsList(d->source, true);
        updateInsList(d->dest, true);
        updating = true;
        ListBoxIns->setCurrentRow(U.curIns);
        updating = false;
        UpDownAIns->setValue(U.curIns + 1);
        patternRedraw();
    }
}

void MainWindow::SpeedButtonOpExchClick(QObject *)
{
    kbd_clear();
    OpCopyDialog::instance(this)->exec();
    applyOperatorCopy();
}

// 0x41590c
void MainWindow::applyOperatorCopy()
{
    OpCopyDialog *d = OpCopyDialog::instance(this);
    Instrument *ins = &g_song.ins[U.curIns];
    undoPushInstruments();
    bool wasEmpty = ins_is_empty(ins);
    if (d->copy) {
        ins->op[d->dstOp] = ins->op[d->srcOp];
        d->copy = false;
    }
    if (d->exchange) {
        Operator t = ins->op[d->srcOp];
        ins->op[d->srcOp] = ins->op[d->dstOp];
        ins->op[d->dstOp] = t;
        d->exchange = false;
    }
    updateInstrumentControls();
    if (wasEmpty != ins_is_empty(ins))
        updateInsList(U.curIns, true);
}

// 0x4265b4
void MainWindow::SpeedButtonResetOp1Click(QObject *Sender)
{
    int opn = Sender ? Sender->property("tag").toInt() : 0;
    if (messageBox(QString("Reset operator #%1 settings?").arg(opn + 1), "Confirm", 4) != 6)
        return;
    undoPushInstruments();
    Operator *op = &g_song.ins[U.curIns].op[opn];
    memset(op, 0, sizeof(*op));
    op->ar = 0x1f;
    op->dr = 0x1f;
    op->sr = 0x1f;
    op->rr = 0xf;
    op->tl = 0x7f;
    updateInstrumentControls();
    updateInsList(U.curIns, true);
}

// 0x4272a0
void MainWindow::InstrumentChangeVolumeClick(QObject *)
{
    kbd_clear();
    ChangeVolumeDialog *d = ChangeVolumeDialog::instance(this);
    d->exec();
    if (!d->ok)
        return;
    undoPushInstruments();
    if (d->RadioButtonCur->isChecked()) {
        if (!ins_is_empty(&g_song.ins[U.curIns]))
            ins_change_volume(&g_song.ins[U.curIns], d->offset);
    }
    if (d->RadioButtonSel->isChecked()) {
        // (the original applies every selected entry's change to the current instrument)
        for (int i = 0; i < ListBoxIns->count(); i++)
            if (ListBoxIns->item(i)->isSelected() && !ins_is_empty(&g_song.ins[i]))
                ins_change_volume(&g_song.ins[i], d->offset);
    }
    if (d->RadioButtonAll->isChecked()) {
        for (int i = 0; i < MAX_INSTRUMENTS; i++)
            if (!ins_is_empty(&g_song.ins[i]))
                ins_change_volume(&g_song.ins[i], d->offset);
    }
    updateInstrumentControls();
}

void MainWindow::InstrumentClearClick(QObject *s) { SpeedButtonInsNewClick(s); }
void MainWindow::InstrumentOpenClick(QObject *s) { SpeedButtonInsOpenClick(s); }
void MainWindow::InstrumentSaveClick(QObject *s) { SpeedButtonInsSaveClick(s); }
void MainWindow::InstrumentCopyExchangeClick(QObject *s) { SpeedButtonInsExchClick(s); }

// ---------------------------------------------------------------- envelope view

// 0x41460c: rough picture of the operator's ADSR envelope
void MainWindow::paintEnvelope(QPainter &p, int opn, const QSize &size)
{
    static const float rates[32] = {1e-5f, 1, 1.5f, 2, 3, 4, 6, 8, 12, 16, 24, 32, 56, 70, 100, 152,
                                    228, 334, 400, 500, 600, 700, 700, 700, 800, 800, 800, 800, 900, 900, 900, 10000};
    static const float slDiv[16] = {1, 2, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8};
    static const int carrierMask[8] = {8, 8, 8, 8, 12, 14, 14, 15};
    const Instrument *ins = &g_song.ins[U.curIns];
    const Operator *op = &ins->op[opn];
    const int w = size.width(), h = size.height();
    const bool carrier = ((1 << opn) & carrierMask[ins->alg & 7]) != 0;
    const int ar = op->ar & 0x1f, dr = op->dr & 0x1f, sr = op->sr & 0x1f, rr = op->rr & 0xf, sl = op->sl & 0xf;
    const int tl = op->tl & 0x7f;
    const float xs = 10.0f, BIG = 10000.0f;

    float tAR = ar == 0 ? BIG : 9.15f / rates[ar];
    float tDR;
    if (sl == 0)
        tDR = 0.02f;
    else if (dr == 0)
        tDR = BIG;
    else
        tDR = (4.02f / rates[dr]) * (float)sl;
    tDR += tAR;
    float tSR = (sr == 0 ? BIG : (81.84f / rates[sr]) / slDiv[sl]) + tDR;
    float tRR = (rr == 0 ? BIG : (31.23f / rates[rr * 2 + 1]) / slDiv[sl]) + tDR;
    auto px = [&](float t) { return (int)qMin(xs * t, 30000.0f); };
    int x1 = px(tAR), y1 = (int)((float)(h - 1) - (float)(127 - tl) / 127.0f * (float)h);
    int x2 = px(tDR), y2 = (int)((float)(h - 1) - (float)h / 127.0f * ((float)(15 - sl) / 15.0f) * (float)(127 - tl));
    int x3 = px(tSR), y3 = h - 1;
    int x4 = px(tRR), y4 = h - 1;

    // carriers near full level get a red background
    int red = (int)(192.0f * (carrier && tl <= 15 ? 1.0f - (float)tl / 15.0f : 0.0f));
    p.fillRect(0, 0, w, h, QColor(red, 0, 0));
    p.setPen(QColor(qMin(red + 0x20, 255), 0x20, 0x20));
    for (int x = 0; x < w; x += 10)
        p.drawLine(x, 0, x, h);
    const QColor attack = carrier ? QColor(0, 0xff, 0) : QColor(0xc0, 0xc0, 0xc0);
    const QColor release = carrier ? QColor(0xff, 0xff, 0) : QColor(0x80, 0x80, 0x80);
    p.setClipRect(0, 0, w, h);
    p.setPen(attack);
    p.drawLine(0, h, x1, y1);
    p.drawLine(x1, y1, x2, y2);
    p.setPen(release);
    p.drawLine(x2, y2, x4, y4);
    p.setPen(attack);
    p.drawLine(x2, y2, x3, y3);
    p.fillRect(x1 - 1, y1 - 1, 2, 2, QColor(0xff, 0, 0));
    p.fillRect(x2 - 1, y2 - 1, 2, 2, QColor(0xff, 0, 0));
}

// ---------------------------------------------------------------- piano keyboard

static const int kKeyX[12] = {0, 16, 26, 42, 52, 78, 94, 104, 120, 130, 146, 156};
static const int kKeyType[12] = {0, 3, 1, 3, 2, 0, 3, 1, 3, 1, 3, 2};

// 0x413f24: three octaves; key shapes come from the ImageKeys sprite sheet
void MainWindow::paintKeys(QPainter &p)
{
    int oct = U.octave - 1;
    if (oct < 1)
        oct = 1;
    if (oct > 6)
        oct = 6;
    const bool focused = ScrollBoxKeys->hasFocus();
    p.fillRect(PaintBoxKeys->rect(), Qt::black);
    for (int ox = 0; ox < 0x222; ox += 0xb6, oct++) {
        for (int n = 0; n < 12; n++) {
            bool pressed = false;
            for (int i = 0; i < 6; i++) {
                int note = U.preview[i].note;
                if (note >= 0 && note % 12 == n && note / 12 == oct - 1)
                    pressed = true;
            }
            int x = kKeyX[n] + ox;
            int type = kKeyType[n];
            int sx = ((pressed ? 4 : 0) + type) * 0x1a;
            if (type == 0)
                p.drawPixmap(x, 0x10, pixKeys, sx, 0, 0x11, 0x24);
            else if (type == 1)
                p.drawPixmap(x + 10, 0x10, pixKeys, sx + 10, 0, 7, 0x24);
            else if (type == 2)
                p.drawPixmap(x + 10, 0x10, pixKeys, sx + 10, 0, 0x10, 0x24);
            else
                p.drawPixmap(x, 0x10, pixKeys, sx, 0, 0x13, 0x24);
            if (type < 3)
                p.drawPixmap(x, 0x34, pixKeys, sx, 0x24, 0x1a, 0x1c);
        }
        QColor text, fill;
        bool current = oct == U.octave;
        if (focused) {
            text = Qt::black;
            fill = current ? QColor(Qt::white) : QColor(0xc0, 0xc0, 0xc0);
        } else {
            text = current ? QColor(0xc0, 0xc0, 0xc0) : QColor(0x80, 0x80, 0x80);
            fill = current ? QColor(0x80, 0x80, 0x80) : QColor(0xc0, 0xc0, 0xc0);
        }
        p.setPen(Qt::black);
        p.setBrush(fill);
        p.drawRect(ox, 0, 0xb6, 0xf);
        p.setPen(text);
        p.drawText(QRect(ox + 0x46, 0, 0x70, 0x10), Qt::AlignLeft | Qt::AlignVCenter, QString("Octave %1").arg(oct));
    }
}

// 0x413db0: note under a point of the keyboard, -1 if none
int MainWindow::keysNoteAt(int x, int y)
{
    static const int white[7] = {0, 2, 4, 5, 7, 9, 11};
    const int keyW = pixKeys.width() >> 3;
    if (x < 0 || y < 0 || x >= PaintBoxKeys->width() || y > 99 || keyW < 1)
        return -1;
    int oct = U.octave - 1;
    if (oct < 1)
        oct = 1;
    if (oct > 6)
        oct = 6;
    if (y < 0x10)
        return -1;
    int note = -1;
    if (y > 0x32) {
        note = white[(x / keyW) % 7] + ((x / keyW) / 7) * 12 + oct * 12 - 12;
        if (note > 0x5f)
            note = 0x5f;
    }
    if (y < 0x2c) {
        int k = x % (keyW * 7);
        if (k > 0xf && k < 0x23)
            note = 1;
        if (k > 0x29 && k < 0x3f)
            note = 3;
        if (k > 0x5d && k < 0x71)
            note = 6;
        if (k > 0x77 && k < 0x8b)
            note = 8;
        if (k > 0x91 && k < 0xa5)
            note = 10;
        if (note >= 0) {
            note = note + ((x / keyW) / 7) * 12 + oct * 12 - 12;
            if (note > 0x5f)
                note = 0x5f;
        }
    }
    return note;
}

// ---------------------------------------------------------------- note preview (second chip set)

// 0x413504
static void preview_fm_write(int ch, int reg, uint8_t val)
{
    if (ch < 3) {
        chips_ym2_write(0, (uint8_t)(ch + reg));
        chips_ym2_write(1, val);
    } else {
        chips_ym2_write(2, (uint8_t)(ch - 3 + reg));
        chips_ym2_write(3, val);
    }
}

// 0x413570
static void preview_fm_key(int ch, bool on)
{
    if (ch > 2)
        ch++;
    chips_ym2_write(0, 0x28);
    chips_ym2_write(1, (uint8_t)((on ? 0xf0 : 0) | ch));
}

// 0x4135b4: start `note` in preview slot `slot` with the current instrument
void MainWindow::previewStart(int key, int note, int slot)
{
    U.preview[slot].note = note;
    U.preview[slot].key = key;
    const Instrument *ins = &g_song.ins[U.curIns];
    AudioLock lock;
    if (!previewFm()) {
        // PSG: operator 4 supplies detune and the software envelope
        const Operator *op = &ins->op[3];
        int idx = note * 0x20 + op->dt;
        if (idx < 0)
            idx = 0;
        if (idx > 0xbff)
            idx = 0xbff;
        int period = g_psgPeriodTab[idx];
        chips_psg2_write((uint8_t)((period & 0xf) | 0xc0));
        chips_psg2_write((uint8_t)((period >> 4) & 0x3f));
        if (!SpeedButtonPSGNoise->isChecked() && !SpeedButtonPSGPeriodic->isChecked()) {
            chips_psg2_write(0xe0);
            chips_psg2_set_mute(4);
        } else {
            chips_psg2_write((uint8_t)((SpeedButtonPSGPeriodic->isChecked() ? 0 : 4) | 0xe3));
            chips_psg2_set_mute(8);
        }
        PsgEnv *e = &g_psgEnv[4];
        e->attack = g_psgRateTab[op->ar & 0x1f];
        e->decay = g_psgRateTab[op->dr & 0x1f];
        e->sustain = g_psgRateTab[op->sr & 0x1f];
        e->release = g_psgRateTab[(op->rr & 0xf) * 2];
        e->sustainLevel = (float)((1.0L / 15.0L) * (long double)(0xf - (op->sl & 0xf)));
        e->scale = (float)((1.0L / 127.0L) * (long double)(0x7f - (op->tl & 0x7f)));
        e->state = 0;
        e->level = e->attack < 1.0f ? 0.0f : 1.0f;
        return;
    }
    int fnum = g_fnumTab[(note % 12) * 0x20];
    for (int i = 0; i < 4; i++)
        preview_fm_write(slot, i * 4 + 0x30, (uint8_t)(dt_to_reg(ins->op[i].dt) << 4 | (ins->op[i].mult & 0xf)));
    for (int i = 0; i < 4; i++)
        preview_fm_write(slot, i * 4 + 0x40, ins->op[i].tl & 0x7f);
    for (int i = 0; i < 4; i++)
        preview_fm_write(slot, i * 4 + 0x50, (uint8_t)((ins->op[i].ar & 0x1f) | ins->op[i].rs << 6));
    for (int i = 0; i < 4; i++)
        preview_fm_write(slot, i * 4 + 0x60, ins->op[i].dr & 0x9f);
    for (int i = 0; i < 4; i++)
        preview_fm_write(slot, i * 4 + 0x70, ins->op[i].sr & 0x1f);
    for (int i = 0; i < 4; i++)
        preview_fm_write(slot, i * 4 + 0x80, (uint8_t)((ins->op[i].rr & 0xf) | ins->op[i].sl << 4));
    for (int i = 0; i < 4; i++)
        preview_fm_write(slot, i * 4 + 0x90, ins->op[i].ssg & 0xf);
    preview_fm_write(slot, 0xb0, (uint8_t)((ins->alg & 7) | (ins->fb & 7) << 3));
    preview_fm_write(slot, 0xa4, (uint8_t)(((fnum >> 8) & 7) | (note / 12) << 3));
    preview_fm_write(slot, 0xa0, (uint8_t)fnum);
    preview_fm_write(slot, 0xb4, (uint8_t)(ins->ams << 4 | ins->fms | 0xc0));
    preview_fm_write(0, 0x22, 0x0b);
    preview_fm_key(slot, true);
}

// 0x413a88
bool MainWindow::previewKeyDown(int key, int note, bool poly)
{
    if (note < 0 || note > 0x5f)
        return true;
    if (!poly) {
        if (note != U.preview[0].note) {
            {
                AudioLock lock;
                if (!previewFm())
                    g_psgEnv[4].state = 3;
                for (int i = 0; i < 6; i++) {
                    preview_fm_key(i, false);
                    U.preview[i].note = -1;
                }
            }
            previewStart(key, note, 0);
        }
        return true;
    }
    for (int i = 0; i < 6; i++)
        if (note == U.preview[i].note)
            return false;
    for (int i = 0; i < 6; i++)
        if (U.preview[i].note < 0) {
            previewStart(key, note, i);
            return true;
        }
    return false;
}

// 0x413b74: retune the sounding note (mouse glide)
bool MainWindow::previewSlide(int note, bool flag)
{
    bool fm = previewFm();
    if (note < 0 || note >= 0x60)
        return true;
    AudioLock lock;
    if (!flag && fm) {
        int fnum = g_fnumTab[(note % 12) * 0x20];
        preview_fm_write(0, 0xa4, (uint8_t)(((fnum >> 8) & 7) | (note / 12) << 3));
        preview_fm_write(0, 0xa0, (uint8_t)fnum);
        return true;
    }
    if (!fm) {
        int idx = note * 0x20 + g_song.ins[U.curIns].op[3].dt;
        if (idx < 0)
            idx = 0;
        if (idx > 0xbff)
            idx = 0xbff;
        int period = g_psgPeriodTab[idx];
        chips_psg2_write((uint8_t)((period & 0xf) | 0xc0));
        chips_psg2_write((uint8_t)((period >> 4) & 0x3f));
        return false;
    }
    return true;
}

// 0x413c7c: key < 0 releases by note number, otherwise by the key that started the note
bool MainWindow::previewKeyUp(int key, int note)
{
    bool fm = previewFm();
    bool changed = false;
    AudioLock lock;
    if (key < 0) {
        if (note < 0 || note > 0x5f)
            return true;
        for (int i = 0; i < 6; i++)
            if (note == U.preview[i].note) {
                if (fm)
                    preview_fm_key(i, false);
                else
                    g_psgEnv[4].state = 3;
                U.preview[i].note = -1;
                changed = true;
            }
    } else {
        for (int i = 0; i < 6; i++)
            if (key == U.preview[i].key && U.preview[i].note >= 0) {
                if (fm)
                    preview_fm_key(i, false);
                else
                    g_psgEnv[4].state = 3;
                U.preview[i].note = -1;
                changed = true;
            }
    }
    return changed;
}

// 0x413d50
bool MainWindow::previewReleaseAll()
{
    bool changed = false;
    AudioLock lock;
    for (int i = 0; i < 6; i++)
        if (U.preview[i].note >= 0) {
            if (!previewFm())
                g_psgEnv[4].state = 3;
            preview_fm_key(i, false);
            U.preview[i].note = -1;
            changed = true;
        }
    return changed;
}

// ---------------------------------------------------------------- level meters

// 0x41eee4
void MainWindow::updateLevels()
{
    const int h2 = PaintBoxLvl->height() - 2;
    {
        AudioLock lock;
        for (int c = 0; c < CHANNELS; c++)
            levelNow[c] = c < 6 ? chips_fm_level(c) : chips_psg_level(c - 6);
        levelsClipped = g_clipped != 0;
        g_clipped = 0;
    }
    for (int c = 0; c < CHANNELS; c++) {
        int v = (int)((long double)h2 * (long double)levelNow[c]);
        if (v < 1)
            v = 1;
        if (v > h2)
            v = h2;
        if (U.levelPeak[c] < v)
            U.levelPeak[c] = v;
        if (U.levelPeak[c] > 1)
            U.levelPeak[c]--;
    }
    PaintBoxLvl->update();
}

void MainWindow::paintLevels(QPainter &p)
{
    static const int muteBtn[CHANNELS] = {1, 5, 9, 13, 17, 21, 25, 29, 33, 37};
    const int h = PaintBoxLvl->height();
    const int h2 = h - 2;
    p.fillRect(PaintBoxLvl->rect(), Qt::black);
    for (int c = 0, x = 0; c < CHANNELS; c++, x += 0x12) {
        int peak = U.levelPeak[c];
        QColor col;
        if (levelsClipped)
            col = QColor(0xff, 0, 0);
        else if (hdr[muteBtn[c]] && !hdr[muteBtn[c]]->isChecked())
            col = QColor(0, 0xff, 0xff);
        else
            col = QColor(0x80, 0x80, 0x80);
        int top = (h2 - peak) + 1;
        p.fillRect(x, top, 16, (h - 1) - top, col);
    }
}

// ---------------------------------------------------------------- samples

// 0x415b70
void MainWindow::updateSampleList(int idx, bool setName)
{
    updating = true;
    if (idx < 0) {
        ListBoxSample->clear();
        for (int i = 0; i < MAX_SAMPLES; i++)
            ListBoxSample->addItem(slotText(i, g_song.sample[i].name));
        ListBoxSample->setCurrentRow(U.curSample);
    } else if (idx < ListBoxSample->count()) {
        ListBoxSample->item(idx)->setText(slotText(idx, g_song.sample[idx].name));
        ListBoxSample->item(idx)->setSelected(true);
    }
    updating = false;
    if (setName) {
        QString name = QString::fromLatin1(g_song.sample[U.curSample].name);
        if (EditSampleName->text() != name)
            EditSampleName->setText(name);
    }
    updateSampleInfo();
}

// 0x415d74
void MainWindow::updateSampleInfo()
{
    LabelSampleSetInfo->setText(QString("Total bank size %1 bytes").arg(samples_total_size()));
    LabelSampleSetInfo->adjustSize();
    const Sample *s = &g_song.sample[U.curSample];
    if (s->data < 0 || g_sampleData[s->data].raw == nullptr)
        LabelSampleInfo->setText("No sample loaded");
    else
        LabelSampleInfo->setText(QString("Original sample %1Hz %2bits")
                                     .arg(g_sampleData[s->data].rate)
                                     .arg(g_sampleData[s->data].bits));
    switch (g_song.sampleRate) {
    case 8000: SpeedButtonRate8000->setChecked(true); break;
    case 11025: SpeedButtonRate11025->setChecked(true); break;
    case 16000: SpeedButtonRate16000->setChecked(true); break;
    case 22050: SpeedButtonRate22050->setChecked(true); break;
    case 32000: SpeedButtonRate32000->setChecked(true); break;
    }
    int vol = s->volume, tune = s->tune;
    knobGain->setValue(vol);
    knobPitch->setValue(tune);
    PaintBoxSample->update();
}

// 0x41610c
void MainWindow::setSampleName(int idx, const QString &s)
{
    QByteArray b = s.toLatin1();
    memset(g_song.sample[idx].name, 0, 0x10);
    memcpy(g_song.sample[idx].name, b.constData(), (size_t)qMin(b.size(), 0x10));
}

// 0x427978
void MainWindow::selectSample(int idx)
{
    U.curSample = idx;
    EditSampleName->setText(QString::fromLatin1(g_song.sample[U.curSample].name));
    updateSampleInfo();
}

void MainWindow::EditSampleNameChange(QObject *)
{
    setSampleName(U.curSample, EditSampleName->text());
    updateSampleList(U.curSample, true);
}

// 0x4161e4
void MainWindow::paintSample(QPainter &p)
{
    const int w = PaintBoxSample->width(), h = PaintBoxSample->height();
    p.fillRect(0, 0, w, h, Qt::black);
    p.setPen(QColor(0x20, 0x20, 0x20));
    for (float x = 0.0f; x < (float)w; x += 200.0f)
        p.drawLine((int)x, h, (int)x, 0);
    p.setPen(QColor(0x60, 0x60, 0x60));
    p.drawLine(0, h / 2, w, h / 2);
    p.setPen(QColor(0, 0xff, 0xff));
    const Sample *s = &g_song.sample[U.curSample];
    if (s->data < 0 || g_sampleData[s->data].conv == nullptr)
        return;
    const SampleData *d = &g_sampleData[s->data];
    const float step = (float)g_song.sampleRate / 200.0f;   // 200 pixels per second
    float pos = 0.0f;
    for (int x = 0; x < w; x++) {
        if (d->convLen <= (int)pos)
            break;
        int v = abs((int)(int8_t)d->conv[(int)pos]);
        pos = step + pos;
        int y = (v * h) >> 8;
        if (h - y > y)
            p.drawLine(x, y, x, h - y - 1);
    }
}

// 0x427934
void MainWindow::SpeedButtonSampleClearClick(QObject *)
{
    if (messageBox("Clear sample?", "Confirm", 4) != 6)
        return;
    {
        AudioLock lock;
        sample_clear(&g_song.sample[U.curSample]);
    }
    updateSampleList(U.curSample, true);
}

// 0x427bec
void MainWindow::SpeedButtonSampleLoadClick(QObject *Sender)
{
    kbd_clear();
    QStringList files = QFileDialog::getOpenFileNames(this, "Open sample", U.pathSample,
                                                      "Wave file (*.wav);;All files (*)");
    if (files.isEmpty())
        return;
    for (int n = 0; n < files.size(); n++) {
        int slot = U.curSample;
        if (n != 0) {
            slot = -1;
            for (int i = U.curSample; i < MAX_SAMPLES; i++)
                if (sample_is_empty(&g_song.sample[i])) {
                    slot = i;
                    break;
                }
            if (slot < 0) {
                messageBox("No room to load all the samples", "Error", 0);
                break;
            }
        }
        char error[1024] = "";
        bool ok;
        {
            AudioLock lock;
            ok = sample_load_wav(&g_song.sample[slot], files[n].toLocal8Bit().constData(), error);
        }
        if (!ok) {
            messageBox("Can't open sample '" + files[n] + "'\n" + QString::fromLatin1(error), "Error", 0);
            continue;
        }
        setSampleName(slot, QFileInfo(files[n]).completeBaseName());
        updateSampleList(slot, n == 0);
    }
    U.pathSample = QFileInfo(files.first()).absolutePath();
    if (files.size() < 2)
        SpeedButtonSampleTestClick(Sender);
}

void MainWindow::SpeedButtonSampleTestClick(QObject *)
{
    AudioLock lock;
    dac_start(U.curSample);
}

// 0x427ff4: the button caption is the rate
void MainWindow::SpeedButtonRate8000Click(QObject *Sender)
{
    QToolButton *b = qobject_cast<QToolButton *>(Sender);
    if (!b)
        return;
    {
        AudioLock lock;
        g_song.sampleRate = b->text().toUInt();
        samples_convert_all();
    }
    updateSampleInfo();
}

// 0x428074
void MainWindow::SpeedButtonSampleExchangeClick(QObject *)
{
    kbd_clear();
    InsCopyDialog *d = InsCopyDialog::instance(this);
    d->instruments = false;
    d->source = U.curSample;
    d->dest = U.curSample + 1;
    if (d->dest > 0xfe)
        d->dest = U.curSample - 1;
    d->exec();
    if (d->copy) {
        undoPushSamples();
        {
            AudioLock lock;
            sample_copy(&g_song.sample[d->source], &g_song.sample[d->dest]);
        }
        U.curSample = d->dest;
        updateSampleList(U.curSample, true);
        updating = true;
        ListBoxSample->setCurrentRow(U.curSample);
        updating = false;
    }
    if (d->exchange) {
        if (!d->exchangeNumbers)
            undoPushSamples();
        else
            undoPushSong();
        {
            AudioLock lock;
            Sample t = g_song.sample[d->source];
            g_song.sample[d->source] = g_song.sample[d->dest];
            g_song.sample[d->dest] = t;
            if (d->exchangeNumbers)
                sample_swap_refs((unsigned)d->source, (unsigned)d->dest);
        }
        U.curSample = d->dest;
        updateSampleList(d->source, true);
        updateSampleList(d->dest, true);
        updating = true;
        ListBoxSample->setCurrentRow(U.curSample);
        updating = false;
        patternRedraw();
    }
}
