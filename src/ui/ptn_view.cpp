// Pattern editor: drawing, channel header, mouse handling.
#include "mainwindow.h"

static const char *const kNoteNames[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
static const char *const kNoiseNames[4] = {"Hi ", "Mid", "Low", "Ch3"};
static const char kFiller[] =
    "   |             |             |             |             |             |             |             |"
    "             |             |             |   ";
static const char kNoPattern[] =
    "                   No pattern number selected               No pattern number selected                "
    "No pattern number selected                   ";
// M button of each channel in the header button array
static const int kMuteBtn[CHANNELS] = {1, 5, 9, 13, 17, 21, 25, 29, 33, 37};

static QString hex(int v, int digits)
{
    return QString("%1").arg((unsigned)v & (digits == 1 ? 0xfu : 0xffu), digits, 16, QChar('0')).toUpper();
}

// 0x4163d0: effect type character (0-9, A-Z)
static QString fxChar(int type)
{
    if (type < 0x10)
        return hex(type, 1);
    return QString(QChar(type + '7'));
}

static QString noteText(const Cell *c, int chan, bool dots)
{
    if (c->note == NOTE_OFF)
        return "***";
    if (c->note == NOTE_EMPTY)
        return dots ? "..." : "---";
    if (chan < 9)
        return QString(kNoteNames[(unsigned)c->note % 12]) + hex(c->octave, 1);
    return kNoiseNames[c->note & 3];
}

// the 13 characters of a cell: "C-4 1F 02 A10"
static QString cellText(const Cell *c, int chan, int fxCol, bool dots)
{
    QString s = noteText(c, chan, dots) + " ";
    s += c->volume == 0 ? QString(".. ") : hex(c->volume, 2) + " ";
    s += c->ins == 0 ? QString(".. ") : hex(c->ins, 2) + " ";
    int type = (int8_t)c->fx[fxCol];
    int param = c->param[fxCol];
    if (type == 0 && param == 0)
        s += "...";
    else
        s += fxChar(type) + hex(param, 2);
    return s;
}

namespace {
struct TextOut {
    QPainter &p;
    int cw, rh, ascent;
    QColor fg, bg;
    // like TCanvas::TextOut with an opaque brush
    void operator()(int x, int y, const QString &s)
    {
        p.fillRect(x, y, cw * s.size(), rh, bg);
        p.setPen(fg);
        for (int i = 0; i < s.size(); i++)
            if (s[i] != ' ')
                p.drawText(x + i * cw, y + ascent, QString(s[i]));
    }
};
} // namespace

// 0x41653c
void MainWindow::paintPattern(QPainter &p)
{
    const int cw = U.charW, rh = U.rowH;
    const int h = PaintBoxPtn->height();
    p.fillRect(PaintBoxPtn->rect(), palette().color(QPalette::Window));
    p.setFont(PaintBoxPtn->font());
    TextOut out{p, cw, rh, QFontMetrics(PaintBoxPtn->font()).ascent(), Qt::white, Qt::black};
    auto col = [](int i) { return colorRef(U.colors[i]); };

    int rows = h / rh + 1;
    if (rows > 0x400)
        rows = 0x400;
    const bool focused = ScrollBoxPtn->hasFocus();
    const bool dots = CheckBoxPtnNoteDots->isChecked();
    const int half = rows / 2;
    const int hl = UpDownRowHighlight->value();

    if (U.noPattern) {
        for (int i = 0, y = 0; i < rows; i++, y += rh) {
            if (i == half) {
                out.fg = col(COL_INVERTED_TEXT);
                out.bg = col(focused ? COL_ACTIVE_LINE : COL_INACTIVE_LINE);
                out(0, y, kNoPattern);
            } else {
                out.bg = col(COL_OUT_OF_RANGE);
                out.fg = col(COL_PREV_NEXT);
                out(0, y, kFiller);
            }
        }
        return;
    }

    const int cursor = focused ? U.curCol : -1;
    struct Line { int row, order; };
    QVector<Line> lines(rows);
    int show = ComboBoxShowPrevNext->currentIndex();
    if (show == 2 || (show == 1 && !SpeedButtonFollowOrder->isChecked())) {
        int len = g_song.pattern[g_song.order[U.curOrder].pattern].length;
        int top = U.curRow - half;
        for (int i = 0; i < rows; i++) {
            lines[i].order = U.curOrder;
            lines[i].row = (i + top < 0 || len <= i + top) ? -1 : i + top;
        }
    } else {
        // rows of the neighbouring order positions are shown above and below
        int row = U.curRow, order = U.curOrder;
        for (int i = half; i >= 0; i--) {
            lines[i] = {row, order};
            if (--row < 0) {
                if (--order < 0)
                    row = -1;
                else
                    row = g_song.pattern[g_song.order[order].pattern].length - 1;
            }
        }
        row = U.curRow;
        order = U.curOrder;
        for (int i = half; i < rows; i++) {
            if (order < g_song.orderLen) {
                lines[i] = {row, order};
                if (g_song.pattern[g_song.order[order].pattern].length <= ++row) {
                    row = 0;
                    order++;
                }
            } else {
                lines[i] = {-1, 0};
            }
        }
    }

    for (int i = 0, y = 0; i < rows; i++, y += rh) {
        const int row = lines[i].row;
        const int pat = g_song.order[lines[i].order & 0xff].pattern;
        if (row < 0) {
            out.bg = col(COL_OUT_OF_RANGE);
            out.fg = col(COL_PREV_NEXT);
            out(0, y, kFiller);
            continue;
        }
        const Cell *cells = g_song.pattern[pat].cell[row];
        if (lines[i].order != U.curOrder) {
            // a neighbouring pattern: one string in the "previous/next" colours
            QString s = hex(row, 2) + " ";
            for (int c = 0; c < CHANNELS; c++)
                s += "|" + cellText(&cells[c], c, U.fxColumn[c], dots);
            s += "| " + hex(row, 2);
            out.bg = col(COL_OUT_OF_RANGE);
            out.fg = col(COL_PREV_NEXT);
            out(0, y, s);
            continue;
        }
        auto selected = [&](int c) {
            return U.selRow <= row && row < U.selRow + U.selH && U.selChan <= c && c < U.selChan + U.selW;
        };
        if (row == U.curRow) {
            // cursor row, drawn field by field so the field under the cursor can be inverted
            const QColor rowBg = col(cursor < 0 ? COL_INACTIVE_LINE : COL_ACTIVE_LINE);
            const QColor text = col(COL_INVERTED_TEXT);
            const QColor cur = Qt::black;
            out.fg = text;
            out.bg = rowBg;
            out(0, y, hex(row, 2) + " ");
            int x = cw * 3;
            for (int c = 0; c < CHANNELS; c++) {
                const Cell *cell = &cells[c];
                const QColor cellBg = selected(c) ? col(COL_SELECT_ACTIVE) : rowBg;
                const int f = c * 8;
                auto field = [&](int idx, const QString &s) {
                    out.bg = cursor == f + idx ? cur : cellBg;
                    out(x, y, s);
                    x += cw * s.size();
                };
                auto gap = [&] {
                    out.bg = cellBg;
                    out(x, y, " ");
                    x += cw;
                };
                out.bg = rowBg;
                out.fg = col(COL_SEPARATOR);
                out(x, y, "|");
                x += cw;
                out.fg = text;
                field(0, noteText(cell, c, dots));
                gap();
                field(1, cell->volume == 0 ? QString(".") : hex(cell->volume >> 4, 1));
                field(2, cell->volume == 0 ? QString(".") : hex(cell->volume, 1));
                gap();
                field(3, cell->ins == 0 ? QString(".") : hex(cell->ins >> 4, 1));
                field(4, cell->ins == 0 ? QString(".") : hex(cell->ins, 1));
                gap();
                int type = (int8_t)cell->fx[U.fxColumn[c]];
                int param = cell->param[U.fxColumn[c]];
                bool none = param == 0 && type == 0;
                field(5, none ? QString(".") : fxChar(type));
                field(6, none ? QString(".") : hex(param >> 4, 1));
                field(7, none ? QString(".") : hex(param, 1));
            }
            out.fg = text;
            out.bg = rowBg;
            out(x, y, "| " + hex(row, 2));
            continue;
        }
        const bool fourth = hl > 0 && row % hl == 0;
        const QColor rowBg = col(fourth ? COL_FOURTH_LINE : COL_NORMAL_LINE);
        const QColor text = col(COL_NORMAL_TEXT);
        out.fg = text;
        out.bg = rowBg;
        out(0, y, hex(row, 2) + " ");
        if (lines[i].order == playOrder && row == playRow) {
            // playing row while the editor does not follow the song
            out.fg = col(COL_INVERTED_TEXT);
            out.bg = col(COL_INACTIVE_LINE);
            out(0, y, hex(row, 2));
            out.fg = text;
        }
        int x = cw * 3;
        for (int c = 0; c < CHANNELS; c++) {
            out.bg = rowBg;
            out.fg = col(COL_SEPARATOR);
            out(x, y, "|");
            x += cw;
            out.fg = text;
            out.bg = selected(c) ? col(fourth ? COL_SELECT_FOURTH : COL_SELECT_NORMAL) : rowBg;
            out(x, y, cellText(&cells[c], c, U.fxColumn[c], dots));
            x += cw * 13;
        }
        out.fg = text;
        out.bg = rowBg;
        out(x, y, "| " + hex(row, 2));
    }
}

// 0x424fb8: sample of the pattern view on the preferences page
void MainWindow::paintPatternPreview(QPainter &p)
{
    const int cw = U.charW, rh = U.rowH;
    auto col = [](int i) { return colorRef(U.colors[i]); };
    p.fillRect(PaintBoxPtnPreview->rect(), palette().color(QPalette::Window));
    p.setFont(PaintBoxPtnPreview->font());
    TextOut out{p, cw, rh, QFontMetrics(PaintBoxPtnPreview->font()).ascent(), Qt::white, Qt::black};
    const bool dots = CheckBoxPtnNoteDots->isChecked();
    const int hl = UpDownRowHighlight->value();
    const int rows = PaintBoxPtnPreview->height() / rh + 1;
    const int selChan = 1, selRow = 2, selW = 100, selH = 100;
    int y = 0;
    for (int v = -2; v < rows; v++, y += rh) {
        QColor rowBg = col(hl > 0 && v % hl == 0 ? COL_FOURTH_LINE : COL_NORMAL_LINE);
        QColor text = col(COL_NORMAL_TEXT);
        QColor cur(0, 0xff, 0);
        if (v == 0 || v == 2) {
            rowBg = col(v == 0 ? COL_ACTIVE_LINE : COL_INACTIVE_LINE);
            cur = Qt::black;
            text = col(COL_INVERTED_TEXT);
        }
        if (v < 0) {
            out.bg = col(COL_OUT_OF_RANGE);
            int show = ComboBoxShowPrevNext->currentIndex();
            if (show == 2 || (show == 1 && !SpeedButtonFollowOrder->isChecked())) {
                out.fg = Qt::white;
                out(0, y, kFiller);
            } else {
                out.fg = col(COL_PREV_NEXT);
                out(0, y, hex(v + 0x40, 2) + " ");
                int x = cw * 3;
                for (int c = 0; c < 3; c++) {
                    out(x, y, dots ? "|..." : "|---");
                    x += cw * 4;
                    out(x, y, " .. .. ...");
                    x += cw * 10;
                }
            }
            continue;
        }
        out.fg = text;
        out.bg = rowBg;
        out(0, y, hex(v, 2) + " ");
        int x = cw * 3;
        for (int c = 0; c < 3; c++) {
            QColor cellBg = rowBg;
            if (selRow <= v && v < selRow + selH && selChan <= c && c < selChan + selW) {
                if (v == 0 || v == 2)
                    cellBg = col(COL_SELECT_ACTIVE);
                else if ((v & 3) == 0)
                    cellBg = col(COL_SELECT_FOURTH);
                else
                    cellBg = col(COL_SELECT_NORMAL);
            }
            out.bg = rowBg;
            out.fg = col(COL_SEPARATOR);
            out(x, y, "|");
            x += cw;
            out.fg = text;
            out.bg = v == 0 && c == 0 ? cur : cellBg;
            out(x, y, dots ? "..." : "---");
            x += cw * 3;
            out.bg = cellBg;
            out(x, y, " ");
            x += cw;
            out(x, y, ".. .. ...");
            x += cw * 9;
        }
    }
}

// ---------------------------------------------------------------- channel header

// 0x41ac1c
void MainWindow::createPatternHeader()
{
    QFont f = font();
    f.setPixelSize(11);
    for (int i = 0; i < 42; i++) {
        QToolButton *b = new QToolButton(PanelPtnHeader);
        hdr[i] = b;
        b->setFont(f);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCheckable(true);
        b->setStyleSheet("QToolButton { padding: 0px; margin: 0px; }");
        if (i > 0 && i < 0x29) {
            int ch = (i - 1) / 4;
            switch ((i - 1) & 3) {
            case 0:
                b->setText("M");
                b->setToolTip("Mute channel");
                break;
            case 1:
                b->setText("S");
                b->setToolTip("Solo toggle");
                break;
            case 2:
                b->setText(ch < 6 ? QString("FM %1").arg(ch + 1) : QString("PSG %1").arg(ch - 5));
                b->setToolTip("Solo mode");
                b->setCheckable(false);
                break;
            case 3:
                b->setText("fx0");
                b->setToolTip("Change effect column");
                b->setCheckable(false);
                break;
            }
        }
        connect(b, &QToolButton::clicked, this, [this, i] { headerButtonClick(i); });
    }
    hdr[0]->setText("*");
    hdr[0]->setToolTip("Mute/unmute all");
    hdr[0]->setChecked(true);
    hdr[41]->setChecked(true);
    for (int i = 0; i < CHANNELS; i++)
        U.fxColumn[i] = 0;
}

// 0x41b010
void MainWindow::layoutPatternHeader()
{
    static const int widths[42] = {3, 2, 2, 7, 3, 2, 2, 7, 3, 2, 2, 7, 3, 2, 2, 7, 3, 2, 2, 7, 3,
                                   2, 2, 7, 3, 2, 2, 7, 3, 2, 2, 7, 3, 2, 2, 7, 3, 2, 2, 7, 3, 1000};
    if (!hdr[0])
        return;
    int x = 0;
    int h = PanelPtnHeader->height();
    for (int i = 0; i < 42; i++) {
        int w = widths[i] * U.charW;
        if (i == 0)
            w += U.charW / 2;
        if (i == 0x28)
            w -= U.charW / 2;
        hdr[i]->setGeometry(x, 0, w, h);
        x += w;
    }
    ScrollBoxPtn->setContentWidth((int)(sizeof(kFiller) - 1) * U.charW);
}

// 0x41b090: a channel sounds while its M button is up
void MainWindow::applyMute()
{
    unsigned mask = 0;
    for (int c = 0; c < CHANNELS; c++)
        if (!hdr[kMuteBtn[c]]->isChecked())
            mask |= 1u << c;
    AudioLock lock;
    chips_set_mute(mask);
}

// 0x41b0d4: in solo mode the solo channel follows the cursor
void MainWindow::cursorColumnChanged()
{
    if (U.soloChan < 0)
        return;
    U.soloChan = U.curCol / 8;
    for (int c = 0; c < CHANNELS; c++) {
        hdr[kMuteBtn[c]]->setChecked(c != U.soloChan);
        hdr[kMuteBtn[c] + 1]->setChecked(c == U.soloChan);
    }
    applyMute();
}

// 0x41b150
void MainWindow::updatePatternHeader()
{
    for (int c = 0; c < CHANNELS; c++)
        hdr[kMuteBtn[c] + 3]->setText(QString("fx%1").arg(U.fxColumn[c]));
    patternRedraw();
}

// 0x41b200
void MainWindow::headerButtonClick(int idx)
{
    int ch = (idx - 1) / 4;
    if (idx == 0 || idx == 0x29) {
        bool down = hdr[idx]->isChecked();
        hdr[0]->setChecked(down);
        hdr[41]->setChecked(down);
        for (int c = 0; c < CHANNELS; c++)
            hdr[kMuteBtn[c]]->setChecked(!down);
        if (down) {
            U.soloChan = -1;
            for (int c = 0; c < CHANNELS; c++)
                hdr[kMuteBtn[c] + 1]->setChecked(false);
        }
    } else {
        switch ((idx - 1) & 3) {
        case 0: {
            unsigned mask = 0;
            for (int c = 0; c < CHANNELS; c++)
                if (!hdr[kMuteBtn[c]]->isChecked())
                    mask |= 1u << c;
            // (the original compares with 0x3f here, which can never mean "all ten channels")
            hdr[0]->setChecked(mask == 0x3ff);
            hdr[41]->setChecked(hdr[0]->isChecked());
            U.soloChan = -1;
            break;
        }
        case 1:
            if (!hdr[kMuteBtn[ch] + 1]->isChecked()) {
                U.soloChan = -1;
                for (int c = 0; c < CHANNELS; c++)
                    hdr[kMuteBtn[c]]->setChecked(false);
                hdr[0]->setChecked(true);
                hdr[41]->setChecked(true);
            } else {
                U.soloChan = ch;
                hdr[0]->setChecked(false);
                hdr[41]->setChecked(false);
            }
            break;
        case 2:
            for (int c = 0; c < CHANNELS; c++)
                hdr[kMuteBtn[c]]->setChecked(c != ch);
            hdr[0]->setChecked(false);
            hdr[41]->setChecked(false);
            U.soloChan = -1;
            break;
        case 3:
            U.fxColumn[ch] = (U.fxColumn[ch] + 1) & 3;
            updatePatternHeader();
            break;
        }
    }
    if (U.soloChan < 0) {
        for (int c = 0; c < CHANNELS; c++)
            hdr[kMuteBtn[c] + 1]->setChecked(false);
    } else {
        for (int c = 0; c < CHANNELS; c++) {
            hdr[kMuteBtn[c] + 1]->setChecked(c == U.soloChan);
            hdr[kMuteBtn[c]]->setChecked(c != U.soloChan);
        }
    }
    applyMute();
    PaintBoxLvl->update();
}

// 0x418fb4: keep the cursor channel in the middle of the view
void MainWindow::scrollToCursorChannel()
{
    int chan = U.curCol / 8;
    ScrollBoxPtn->setScrollPos((chan * 14 + 4) * U.charW - (ScrollBoxPtn->viewWidth() - U.charW * 14) / 2);
}

// ---------------------------------------------------------------- mouse

// 0x41d468
bool MainWindow::mouseToCell(int x, int y, int *chan, int *row)
{
    int col = x / U.charW;
    if (col >= 0x8f)
        return false;
    int rows = PaintBoxPtn->height() / U.rowH + 1;
    int r = y / U.rowH + U.curRow - rows / 2;
    if (col > 3 && r >= 0 && r < g_song.pattern[U.curPattern].length) {
        *chan = (col - 4) / 14;
        *row = r;
        return true;
    }
    return false;
}

// 0x41da90
void MainWindow::mouseSetCursor(int x, int y)
{
    int chan, row;
    if (!mouseToCell(x, y, &chan, &row) || x / U.charW <= 3)
        return;
    int pos = x / U.charW - 4;
    int base = (pos / 14) * 8;
    bool half = false;
    switch (pos % 14) {
    case 0: case 1: case 2: U.curCol = base; break;
    case 3: half = true; U.curCol = base + 1; break;
    case 4: U.curCol = base + 1; break;
    case 5: U.curCol = base + 2; break;
    case 6: half = true; U.curCol = base + 3; break;
    case 7: U.curCol = base + 3; break;
    case 8: U.curCol = base + 4; break;
    case 9: half = true; U.curCol = base + 5; break;
    case 10: U.curCol = base + 5; break;
    case 11: U.curCol = base + 6; break;
    case 12: U.curCol = base + 7; break;
    case 13: half = true; U.curCol = base + 8; break;
    }
    // on the gap before a field the left half still belongs to the previous field
    if (half && x % U.charW < U.charW / 2)
        U.curCol--;
    if (U.curCol < base)
        U.curCol = base + 7;
    if (U.curCol > 0x4f)
        U.curCol = 0x4f;
    if (!tracking()) {
        U.curRow = row;
        updateStatus(false, true);
    }
    patternRedraw();
    cursorColumnChanged();
    scrollToCursorChannel();
}

// 0x426180
void MainWindow::PaintBoxPtnMouseDown(QMouseEvent *e)
{
    ScrollBoxPtn->setFocus();
    if (U.noPattern)
        return;
    if (e->type() == QEvent::MouseButtonDblClick) {
        // select the whole channel under the cursor
        U.selChan = U.curCol / 8;
        U.selRow = 0;
        U.selW = 1;
        U.selH = g_song.pattern[U.curPattern].length;
        U.selX1 = U.selChan;
        U.selY1 = U.selRow;
        U.selX2 = U.selX1;
        U.selY2 = U.selH;
        patternRedraw();
    } else if (e->button() == Qt::RightButton) {
        kbd_clear();
        PopupMenuPtn->popup(e->globalPosition().toPoint());
    } else if (e->button() == Qt::LeftButton) {
        U.selX1 = U.selY1 = U.selX2 = U.selY2 = 0;
        U.selChan = U.selRow = U.selW = U.selH = 0;
        patternRedraw();
    }
}

// 0x4262d4: dragging with the left button selects a block
void MainWindow::PaintBoxPtnMouseMove(QMouseEvent *e)
{
    if (U.noPattern || !(e->buttons() & Qt::LeftButton))
        return;
    QPoint pt = e->position().toPoint();
    int chan, row;
    if (!mouseToCell(pt.x(), pt.y(), &chan, &row))
        return;
    if (pt.y() < U.rowH && U.curRow > 0) {
        U.curRow--;
        updateStatus(false, true);
    }
    if (PaintBoxPtn->height() - U.rowH < pt.y() && U.curRow < g_song.pattern[U.curPattern].length - 1) {
        U.curRow++;
        updateStatus(false, true);
    }
    if (U.selH < 1) {
        U.selX1 = chan;
        U.selY1 = row;
        U.selX2 = chan;
        U.selY2 = row;
        U.selChan = chan;
        U.selRow = row;
        U.selW = 1;
        U.selH = 1;
    }
    selectionSetEnd(chan, row);
    patternRedraw();
}

// 0x42640c
void MainWindow::PaintBoxPtnMouseUp(QMouseEvent *e)
{
    QPoint pt = e->position().toPoint();
    if (!U.noPattern && e->button() == Qt::LeftButton && U.selH < 1)
        mouseSetCursor(pt.x(), pt.y());
}
