#include "widgets.h"

#include <cmath>

// TSpeedButton glyphs use the colour of their bottom-left pixel as the transparent colour
void setGlyph(QToolButton *b, const char *resource)
{
    QImage img = QImage(resource).convertToFormat(QImage::Format_ARGB32);
    if (img.isNull())
        return;
    QRgb key = img.pixel(0, img.height() - 1);
    for (int y = 0; y < img.height(); y++) {
        QRgb *line = (QRgb *)img.scanLine(y);
        for (int x = 0; x < img.width(); x++)
            if (line[x] == key)
                line[x] = 0;
    }
    // disabled look: grey silhouette, as the VCL draws it
    QImage dis = img;
    for (int y = 0; y < dis.height(); y++) {
        QRgb *line = (QRgb *)dis.scanLine(y);
        for (int x = 0; x < dis.width(); x++)
            if (qAlpha(line[x]))
                line[x] = qGray(line[x]) < 200 ? qRgb(0x80, 0x80, 0x80) : qRgba(0, 0, 0, 0);
    }
    QIcon icon;
    icon.addPixmap(QPixmap::fromImage(img), QIcon::Normal);
    icon.addPixmap(QPixmap::fromImage(dis), QIcon::Disabled);
    b->setIcon(icon);
    b->setIconSize(img.size());
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
}

QPixmap sheetFrame(const QPixmap &sheet, int index, int w, int h)
{
    return sheet.copy((index % 4) * w, (index / 4) * h, w, h);
}

// ---------------------------------------------------------------- PaintBox

PaintBox::PaintBox(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void PaintBox::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (onPaint)
        onPaint(p);
    else if (!pixmap.isNull())
        p.drawPixmap(0, 0, pixmap);
}

FocusFrame::FocusFrame(QWidget *parent) : QFrame(parent)
{
    setFocusPolicy(Qt::StrongFocus);
}

// ---------------------------------------------------------------- PatternScroll

PatternScroll::PatternScroll(QWidget *parent) : QFrame(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    clip = new QWidget(this);
    content = new QWidget(clip);
    hbar = new QScrollBar(Qt::Horizontal, this);
    hbar->setFocusPolicy(Qt::NoFocus);
    connect(hbar, &QScrollBar::valueChanged, this, [this](int v) { content->move(-v, 0); });
}

void PatternScroll::setContent(QWidget *h, QWidget *v)
{
    header = h;
    view = v;
    header->setParent(content);
    view->setParent(content);
    header->show();
    view->show();
    relayout();
}

void PatternScroll::setContentWidth(int w)
{
    contentWidth = w;
    relayout();
}

void PatternScroll::resizeEvent(QResizeEvent *)
{
    relayout();
    if (onResize)
        onResize();
}

void PatternScroll::relayout()
{
    int fw = frameWidth();
    int w = width() - fw * 2;
    int h = height() - fw * 2;
    int sb = hbar->sizeHint().height();
    bool need = contentWidth > w;
    hbar->setVisible(need);
    if (need)
        h -= sb;
    clip->setGeometry(fw, fw, w, h);
    hbar->setGeometry(fw, fw + h, w, sb);
    hbar->setRange(0, need ? contentWidth - w : 0);
    hbar->setPageStep(w);
    hbar->setSingleStep(16);
    int cw = contentWidth > w ? contentWidth : w;
    content->setGeometry(-hbar->value(), 0, cw, h);
    if (header && view) {
        int hh = header->height();
        header->setGeometry(0, 0, cw, hh);
        view->setGeometry(0, hh, cw, h - hh);
    }
}

// ---------------------------------------------------------------- OrderGrid

OrderGrid::OrderGrid(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    hbar = new QScrollBar(Qt::Horizontal, this);
    hbar->setFocusPolicy(Qt::NoFocus);
    hbar->setRange(0, COLS - 1);
    connect(hbar, &QScrollBar::valueChanged, this, [this] { update(); });
}

int OrderGrid::visibleCols() const
{
    return (width() - 4) / (COL_W + 1);
}

void OrderGrid::resizeEvent(QResizeEvent *)
{
    int sb = hbar->sizeHint().height();
    hbar->setGeometry(2, height() - 2 - sb, width() - 4, sb);
    hbar->setPageStep(visibleCols());
    hbar->setRange(0, COLS - visibleCols());
}

void OrderGrid::setSelected(int col)
{
    sel = col;
    if (col < hbar->value())
        hbar->setValue(col);
    if (col >= hbar->value() + visibleCols())
        hbar->setValue(col - visibleCols() + 1);
    update();
}

int OrderGrid::colAt(const QPoint &p) const
{
    if (p.y() < 2 || p.y() >= 2 + ROW_H + 1 || p.x() < 2)
        return -1;
    int c = (p.x() - 2) / (COL_W + 1) + hbar->value();
    return c < COLS ? c : -1;
}

void OrderGrid::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    QStyleOptionFrame opt;
    opt.initFrom(this);
    opt.rect = rect();
    opt.lineWidth = 2;
    opt.midLineWidth = 0;
    opt.state |= QStyle::State_Sunken;
    opt.frameShape = QFrame::WinPanel;
    p.fillRect(rect(), palette().color(QPalette::Base));
    const QRect inner = rect().adjusted(2, 2, -2, -2);
    p.setClipRect(inner);
    const QColor grid(0xc0, 0xc0, 0xc0);
    int x = 2;
    for (int c = hbar->value(); c < COLS && x < width(); c++, x += COL_W + 1) {
        QRect cell(x, 2, COL_W, ROW_H);
        bool selected = c == sel;
        if (selected) {
            p.fillRect(cell, palette().color(QPalette::Highlight));
            p.setPen(palette().color(QPalette::HighlightedText));
        } else {
            p.setPen(palette().color(QPalette::Text));
        }
        if (cellText)
            p.drawText(cell.adjusted(2, 2, 0, 0), Qt::AlignLeft | Qt::AlignTop, cellText(c));
        if (selected && hasFocus()) {
            QStyleOptionFocusRect fo;
            fo.initFrom(this);
            fo.rect = cell;
            fo.backgroundColor = palette().color(QPalette::Highlight);
            style()->drawPrimitive(QStyle::PE_FrameFocusRect, &fo, &p, this);
        }
        p.setPen(grid);
        p.drawLine(x + COL_W, 2, x + COL_W, 2 + ROW_H);
    }
    p.setPen(grid);
    p.drawLine(2, 2 + ROW_H, width() - 2, 2 + ROW_H);
    p.setClipping(false);
    qDrawShadePanel(&p, rect(), palette(), true, 1);
    qDrawShadePanel(&p, rect().adjusted(1, 1, -1, -1), palette(), true, 1);
}

void OrderGrid::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    int c = colAt(e->pos());
    if (e->button() == Qt::LeftButton) {
        dragFrom = c;
        dragging = false;
    }
    if (c >= 0 && onMouseDown)
        onMouseDown(c, e);
}

void OrderGrid::mouseMoveEvent(QMouseEvent *e)
{
    if (!(e->buttons() & Qt::LeftButton) || dragFrom < 0)
        return;
    int c = colAt(e->pos());
    if (c != dragFrom)
        dragging = true;
    if (dragging)
        setCursor(c >= 0 && (!canDrop || canDrop(dragFrom, c)) ? Qt::DragMoveCursor : Qt::ForbiddenCursor);
}

void OrderGrid::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    unsetCursor();
    int c = colAt(e->pos());
    if (dragging && dragFrom >= 0 && c >= 0 && c != dragFrom && onDrop && (!canDrop || canDrop(dragFrom, c)))
        onDrop(dragFrom, c);
    dragFrom = -1;
    dragging = false;
}

void OrderGrid::wheelEvent(QWheelEvent *e)
{
    hbar->setValue(hbar->value() + (e->angleDelta().y() < 0 ? 1 : -1));
    e->accept();
}

void OrderGrid::contextMenuEvent(QContextMenuEvent *e)
{
    if (onContextMenu)
        onContextMenu(e->globalPos());
}

// ---------------------------------------------------------------- Knob

Knob *Knob::active = nullptr;
bool Knob::verticalSlider = false;
QPixmap *Knob::bitmap = nullptr;

Knob::Knob(QWidget *parent, int x, int y, int min, int max, int nticks, int t)
    : QWidget(parent), tag(t), vmin(min), vmax(max), ticks(nticks), val(min)
{
    setFocusPolicy(Qt::StrongFocus);
    labelH = QFontMetrics(font()).height();
    setGeometry(x, y, 32, labelH + 0x21);
}

// 0x42dc8c
bool Knob::clamp(int *v) const
{
    if (vmax < vmin) {
        if (vmin < *v) { *v = vmin; return true; }
        if (*v < vmax) { *v = vmax; return true; }
    } else {
        if (*v < vmin) { *v = vmin; return true; }
        if (vmax < *v) { *v = vmax; return true; }
    }
    return false;
}

// 0x42dd54
bool Knob::setValue(int v)
{
    int old = val;
    bool clamped = clamp(&v);
    val = v;
    if (old != val)
        update();
    if (onChange)
        onChange(tag, val);
    return clamped;
}

// 0x42dce0
bool Knob::step(int delta)
{
    int old = val;
    int v = vmin < vmax ? val + delta : val - delta;
    bool clamped = clamp(&v);
    val = v;
    if (old != val)
        update();
    if (onChange)
        onChange(tag, val);
    return clamped;
}

bool Knob::handlesKey(int vk) const
{
    if (vk >= 0x30 && vk <= 0x39)
        return true;
    return vk == 0x2e || vk == 8 || vk == 0x26 || vk == 0x28 || vk == 0x6b || vk == 0x6d || vk == 0xbd;
}

// 0x42ddc0 (WM_KEYDOWN part)
void Knob::keyDown(int vk)
{
    if (vk == 0x2e) {
        setValue(0);
    } else if (vk == 8) {
        setValue(val / 10);
    } else if (vk == 0x26 || vk == 0x6b) {
        step(1);
    } else if (vk == 0x28 || vk == 0x6d) {
        step(-1);
    } else if (vk == 0xbd) {
        if (!(vmin >= 0 && vmax >= 0))
            setValue(-val);
    } else if (vk >= 0x30 && vk <= 0x39) {
        if (setValue(val * 10 + vk - 0x30))
            setValue(vk - 0x30);
    }
}

extern int qtKeyToVk(const QKeyEvent *e);

void Knob::keyPressEvent(QKeyEvent *e)
{
    int vk = qtKeyToVk(e);
    if (handlesKey(vk)) {
        keyDown(vk);
        e->accept();
    } else {
        e->ignore();
    }
}

void Knob::wheelEvent(QWheelEvent *e)
{
    step(e->angleDelta().y() / 120);
    e->accept();
}

// 0x42d754
void Knob::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int cx = width() >> 1;
    const int cy = labelH + cx;
    const bool focused = hasFocus() || this == active;
    p.setPen(Qt::black);
    const float r1 = (float)(width() >> 1);
    const float r2 = r1 - 4.0f;
    float angle = 225.0f;
    const float inc = 270.0f / (float)(ticks - 1);
    for (int i = 0; i < ticks; i++) {
        float a = angle * 0.017453292f;
        int x1 = (int)((long double)cx + (long double)(float)(sinl(a) * r1));
        int y1 = (int)((long double)cy + (long double)(float)(-r1 * cosl(a)));
        int x2 = (int)((long double)cx + (long double)(float)(sinl(a) * r2));
        int y2 = (int)((long double)cy + (long double)(float)(-r2 * cosl(a)));
        p.drawLine(x1, y1, x2, y2);
        angle = inc + angle;
    }
    if (bitmap)
        p.drawPixmap(cx - (bitmap->width() >> 1), cy - (bitmap->height() >> 1), *bitmap);
    int delta = vmin < vmax ? val - vmin : vmin - val;
    const float r3 = (float)((width() >> 1) - 7);
    float a = ((270.0f / (float)abs(vmax - vmin)) * (float)delta + 225.0f) * 0.017453292f;
    float dx = (float)(sinl(a) * r3);
    float dy = (float)(-r3 * cosl(a));
    QColor dot = focused ? QColor(Qt::white) : QColor(0xc0, 0xc0, 0xc0);
    p.setPen(dot);
    p.setBrush(dot);
    int ex1 = (int)(((long double)cx + dx) - 2.0L), ey1 = (int)(((long double)cy + dy) - 2.0L);
    int ex2 = (int)((long double)cx + dx + 2.0L), ey2 = (int)((long double)cy + dy + 2.0L);
    p.drawEllipse(ex1, ey1, ex2 - ex1 - 1, ey2 - ey1 - 1);
    p.setPen(focused ? QColor(0x80, 0, 0) : palette().color(QPalette::WindowText));
    p.drawText(QRect(0, 0, width(), labelH), Qt::AlignHCenter | Qt::AlignTop, QString::number(val));
}

// 0x42db58: a left click opens the slider, which stays up while the button is held
void Knob::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    if (e->button() != Qt::LeftButton)
        return;
    if (onBeginEdit)
        onBeginEdit(this);
    active = this;
    delete popup;
    popup = new SliderPopup(this, verticalSlider);
    QPoint org = mapToGlobal(QPoint(0, 0));
    if (verticalSlider)
        popup->move(org.x() - popup->width(), org.y() + height() / 2 - popup->height() / 2);
    else
        popup->move(org.x() + width() / 2 - popup->width() / 2, org.y() + height());
    // the original warps the cursor onto the slider handle; here the slider is shifted so
    // that its handle starts under the cursor along the slider's axis
    QPoint g = e->globalPosition().toPoint();
    if (verticalSlider)
        popup->move(popup->x(), g.y() - popup->guide.top() - (0x7f - popup->position()));
    else
        popup->move(g.x() - popup->guide.left() - popup->position(), popup->y());
    popup->show();
    update();
}

void Knob::mouseMoveEvent(QMouseEvent *e)
{
    if (popup && (e->buttons() & Qt::LeftButton))
        popup->track(e->globalPosition().toPoint());
}

void Knob::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (popup) {
        popup->hide();
        popup->deleteLater();
        popup = nullptr;
    }
    active = nullptr;
    update();
}

// ---------------------------------------------------------------- SliderPopup

SliderPopup::SliderPopup(Knob *k, bool vert)
    : QWidget(k->window(), Qt::ToolTip | Qt::FramelessWindowHint), vertical(vert), knob(k)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    if (vertical) {
        resize(0x16, 0x91);
        guide = QRect(7, 9, 8, 0x80);
    } else {
        resize(0x91, 0x16);
        guide = QRect(9, 7, 0x80, 8);
    }
}

// round half away from zero, the way the original does it before its truncating _ftol
static int round_away(float v)
{
    float t = truncf(v);
    if (0.0f <= v) {
        if (0.5f <= v - t)
            v = v + 1.0f;
    } else if (v - t <= -0.5f) {
        v = v - 1.0f;
    }
    return (int)v;
}

// 0x42b634
int SliderPopup::position() const
{
    int lo = knob->minimum(), hi = knob->maximum();
    int range = abs(lo) + abs(hi);
    float v;
    if (!(hi < lo))
        v = ((float)(knob->value() - lo) * 127.0f) / (float)range;
    else
        v = 127.0f - ((float)(knob->value() - hi) * 127.0f) / (float)range;
    return round_away(v);
}

// 0x42b78c
void SliderPopup::setPosition(int pos)
{
    int lo = knob->minimum(), hi = knob->maximum();
    int range = abs(lo) + abs(hi);
    long double k = 1.0L / 127.0L;
    float v;
    if (!(hi < lo))
        v = (float)((long double)lo + k * (long double)range * (long double)pos);
    else
        v = (float)((long double)lo - k * (long double)range * (long double)pos);
    knob->setValue(round_away(v));
    update();
}

void SliderPopup::track(const QPoint &global)
{
    QPoint p = mapFromGlobal(global);
    int d = vertical ? p.y() - guide.top() : p.x() - guide.left();
    if (d < 0)
        d = 0;
    if (d > 0x7f)
        d = 0x7f;
    setPosition(vertical ? 0x7f - d : d);
}

void SliderPopup::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window));
    qDrawShadePanel(&p, rect(), palette(), false, 2);
    p.fillRect(guide, palette().color(QPalette::Base));
    qDrawShadePanel(&p, guide, palette(), true, 2);
    QRect handle;
    if (vertical)
        handle = QRect(guide.left() + guide.width() / 2 - 8, (0x7f - position()) + guide.top() - 4, 16, 8);
    else
        handle = QRect(position() + guide.left() - 4, guide.top() + guide.height() / 2 - 8, 8, 16);
    p.fillRect(handle, palette().color(QPalette::Window));
    qDrawShadePanel(&p, handle, palette(), false, 1);
    qDrawShadePanel(&p, handle.adjusted(1, 1, -1, -1), palette(), false, 1);
}

// ---------------------------------------------------------------- PickerPopup

PickerPopup::PickerPopup(QWidget *parent, const QPixmap &s, int n, int per, const QSize &it, const QSize &c,
                         const QPoint &org, bool isEg)
    : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint), sheet(s), count(n), perRow(per), item(it), cell(c),
      origin(org), eg(isEg)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
}

void PickerPopup::track(const QPoint &global)
{
    QPoint p = mapFromGlobal(global) - origin;
    int rows = (count + perRow - 1) / perRow;
    int areaW = eg ? item.width() : width() - origin.x() * 2;
    int areaH = height() - origin.y() * 2;
    // the original confines the cursor to the list; here positions outside are clamped
    int x = qBound(0, p.x(), areaW - 1);
    int y = qBound(0, p.y(), areaH - 1);
    int idx = (y / cell.height()) * perRow + x / cell.width();
    if (y / cell.height() >= rows)
        idx = count - 1;
    if (idx >= count)
        idx = count - 1;
    if (idx != current) {
        current = idx;
        update();
    }
}

void PickerPopup::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window));
    qDrawShadePanel(&p, rect(), palette(), false, 1);
    p.translate(origin);
    for (int i = 0; i < count; i++) {
        int x = (i % perRow) * cell.width();
        int y = (i / perRow) * cell.height();
        p.drawPixmap(x, y, sheetFrame(sheet, i, item.width(), item.height()));
        if (i != current)
            continue;
        p.setBrush(Qt::NoBrush);
        if (eg) {
            // 0x42cdc4: four nested frames, the middle two in cyan
            QRect r(0, y, 0x2c, 0x16);
            for (int k = 0; k < 4; k++) {
                p.setPen(k >= 1 && k <= 2 ? QColor(0, 0xff, 0xff) : QColor(Qt::black));
                p.drawRect(r.adjusted(0, 0, -1, -1));
                r.adjust(1, 1, -1, -1);
            }
        } else {
            // 0x42c71c
            QRect r(x + 1, y + 1, 0x53, 0x2a);
            p.setPen(QColor(0, 0xff, 0xff));
            for (int k = 0; k < 2; k++) {
                p.drawRect(r.adjusted(0, 0, -1, -1));
                r.adjust(1, 1, -1, -1);
            }
        }
    }
}
