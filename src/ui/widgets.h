// Custom controls of the editor. Most are thin event-forwarding surfaces (the VCL original used
// TPaintBox/TImage with handlers in the main form); TKnob was a real component there too.
#pragma once
#include <QtWidgets>
#include <functional>

// 0x00BBGGRR (Windows COLORREF, as stored in the settings) -> QColor
inline QColor colorRef(unsigned c)
{
    return QColor((int)(c & 0xff), (int)(c >> 8 & 0xff), (int)(c >> 16 & 0xff));
}
inline unsigned toColorRef(const QColor &c)
{
    return (unsigned)c.red() | (unsigned)c.green() << 8 | (unsigned)c.blue() << 16;
}

void setGlyph(QToolButton *b, const char *resource);

// TPaintBox / TImage
class PaintBox : public QWidget {
public:
    explicit PaintBox(QWidget *parent = nullptr);
    std::function<void(QPainter &)> onPaint;
    std::function<void(QMouseEvent *)> onMouseDown, onMouseMove, onMouseUp;
    std::function<void(QWheelEvent *)> onWheel;
    void setPixmap(const QPixmap &p) { pixmap = p; update(); }
    QPixmap pixmap;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override { if (onMouseDown) onMouseDown(e); else e->ignore(); }
    void mouseMoveEvent(QMouseEvent *e) override { if (onMouseMove) onMouseMove(e); else e->ignore(); }
    void mouseReleaseEvent(QMouseEvent *e) override { if (onMouseUp) onMouseUp(e); else e->ignore(); }
    void wheelEvent(QWheelEvent *e) override { if (onWheel) onWheel(e); else e->ignore(); }
};

// Focusable sunken box (the TScrollBox around the piano keyboard)
class FocusFrame : public QFrame {
public:
    explicit FocusFrame(QWidget *parent = nullptr);
    std::function<void()> onFocusChange;

protected:
    void focusInEvent(QFocusEvent *) override { if (onFocusChange) onFocusChange(); }
    void focusOutEvent(QFocusEvent *) override { if (onFocusChange) onFocusChange(); }
    void keyPressEvent(QKeyEvent *e) override { e->accept(); }
};

// The pattern editor's scroll box: header panel + paint box, scrolled horizontally.
class PatternScroll : public QFrame {
public:
    explicit PatternScroll(QWidget *parent = nullptr);
    void setContent(QWidget *header, QWidget *view);
    void setContentWidth(int w);
    int scrollPos() const { return hbar->value(); }
    void setScrollPos(int x) { hbar->setValue(x); }
    int viewWidth() const { return clip->width(); }
    std::function<void()> onFocusChange;
    std::function<void()> onResize;

protected:
    void resizeEvent(QResizeEvent *) override;
    void focusInEvent(QFocusEvent *) override { if (onFocusChange) onFocusChange(); }
    void focusOutEvent(QFocusEvent *) override { if (onFocusChange) onFocusChange(); }
    void keyPressEvent(QKeyEvent *e) override { e->accept(); }

private:
    void relayout();
    QWidget *clip, *content, *header = nullptr, *view = nullptr;
    QScrollBar *hbar;
    int contentWidth = 1200;
};

// One-row string grid holding the order list
class OrderGrid : public QWidget {
public:
    enum { COLS = 256, COL_W = 32, ROW_H = 24 };
    explicit OrderGrid(QWidget *parent = nullptr);
    std::function<QString(int)> cellText;
    std::function<void(int col, QMouseEvent *)> onMouseDown;
    std::function<void(int from, int to)> onDrop;
    std::function<bool(int from, int to)> canDrop;
    std::function<void(const QPoint &global)> onContextMenu;
    int selected() const { return sel; }
    void setSelected(int col);      // also scrolls the cell into view
    int leftCol() const { return hbar->value(); }
    int visibleCols() const;
    void refresh() { update(); }

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void focusInEvent(QFocusEvent *) override { update(); }
    void focusOutEvent(QFocusEvent *) override { update(); }
    void keyPressEvent(QKeyEvent *e) override { e->accept(); }

private:
    int colAt(const QPoint &p) const;
    QScrollBar *hbar;
    int sel = 0;
    int dragFrom = -1;
    bool dragging = false;
};

// TKnob
class SliderPopup;
class Knob : public QWidget {
public:
    Knob(QWidget *parent, int x, int y, int min, int max, int ticks, int tag);
    int value() const { return val; }
    bool setValue(int v);           // returns true if v had to be clamped; always notifies
    bool step(int delta);
    bool handlesKey(int vk) const;  // 0x42df0c
    void keyDown(int vk);
    int minimum() const { return vmin; }
    int maximum() const { return vmax; }
    int tag;
    std::function<void(int tag, int value)> onChange;
    std::function<void(Knob *)> onBeginEdit;    // undo snapshot hook
    static Knob *active;            // knob currently driven by the slider popup
    static bool verticalSlider;
    static QPixmap *bitmap;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void focusInEvent(QFocusEvent *) override { update(); }
    void focusOutEvent(QFocusEvent *) override { update(); }
    void keyPressEvent(QKeyEvent *e) override;

private:
    bool clamp(int *v) const;
    int vmin, vmax, ticks, val;
    int labelH;
    SliderPopup *popup = nullptr;
};

// TFormTBarPop: slider shown while a knob is held down
class SliderPopup : public QWidget {
public:
    SliderPopup(Knob *knob, bool vertical);
    int position() const;               // 0..127 for the knob's value (0x42b634)
    void setPosition(int pos);          // 0x42b78c
    void track(const QPoint &global);   // 0x42b9e4
    QRect guide;
    bool vertical;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Knob *knob;
};

// TFormSetAlgo / TFormSetEG: picture list shown while the mouse button is held
class PickerPopup : public QWidget {
public:
    PickerPopup(QWidget *parent, const QPixmap &sheet, int count, int perRow, const QSize &item, const QSize &cell,
                const QPoint &origin, bool eg);
    void track(const QPoint &global);
    int current = 0;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QPixmap sheet;
    int count, perRow;
    QSize item, cell;
    QPoint origin;
    bool eg;
};

// Frame of `index` from an image-list sheet saved by extract_images.py (4 images per row)
QPixmap sheetFrame(const QPixmap &sheet, int index, int w, int h);
