// Main form: construction, settings, file handling, playback control.
#include "mainwindow.h"

#include <cmath>

#include "dialogs.h"

UiState U;
MainWindow *g_mainWindow;

static const char *const kColorKeys[COL_COUNT] = {
    "ColorOutOfRange", "ColorNormalLine", "ColorFourthLine", "ColorNormalText", "ColorInvertedText",
    "ColorActiveLine", "ColorInactiveLine", "ColorSelectActiveLine", "ColorSelectNormalLine",
    "ColorSelectFourthLine", "ColorSeparator", "ColorPrevNextPatterns",
};

// bitmap drawn transparently: the colour of its bottom-left pixel is the colour key
static QPixmap loadKeyed(const char *res)
{
    QImage img = QImage(res).convertToFormat(QImage::Format_ARGB32);
    if (img.isNull())
        return QPixmap();
    QRgb key = img.pixel(0, img.height() - 1);
    for (int y = 0; y < img.height(); y++) {
        QRgb *line = (QRgb *)img.scanLine(y);
        for (int x = 0; x < img.width(); x++)
            if (line[x] == key)
                line[x] = 0;
    }
    return QPixmap::fromImage(img);
}

// "XX: name" list entries: number at x+2, name at x+0x14, empty slots in grey (0x424414)
class SlotDelegate : public QStyledItemDelegate {
public:
    SlotDelegate(QObject *parent, std::function<bool(int)> empty) : QStyledItemDelegate(parent), isEmpty(std::move(empty)) {}
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        bool sel = opt.state & QStyle::State_Selected;
        p->fillRect(opt.rect, opt.palette.color(sel ? QPalette::Highlight : QPalette::Base));
        QColor c = opt.palette.color(sel ? QPalette::HighlightedText : QPalette::Text);
        if (isEmpty(idx.row()))
            c = QColor(0x80, 0x80, 0x80);       // clGrayText
        p->setPen(c);
        QString s = idx.data().toString();
        p->drawText(opt.rect.adjusted(2, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, s.left(3));
        p->drawText(opt.rect.adjusted(0x16, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, s.mid(3));
        if (opt.state & QStyle::State_HasFocus) {
            QStyleOptionFocusRect fo;
            fo.QStyleOption::operator=(opt);
            fo.backgroundColor = opt.palette.color(QPalette::Highlight);
            QApplication::style()->drawPrimitive(QStyle::PE_FrameFocusRect, &fo, p);
        }
    }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return QSize(100, 16); }

private:
    std::function<bool(int)> isEmpty;
};

MainWindow::MainWindow()
{
    g_mainWindow = this;
    setAcceptDrops(true);
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    setupUi(central);
    setMenuBar(MainMenu);
    setWindowIcon(QIcon(":/img/TFORMMAIN_FormMain.ico"));

    pixKeys = QPixmap(":/img/TFORMMAIN_ImageKeys.png");
    pixAlgo = QPixmap(":/img/TFORMMAIN_ImageListAlgo_n8_85x44.png");
    pixEG = QPixmap(":/img/TFORMMAIN_ImageListEG_n9_44x22.png");
    pixAM = QPixmap(":/img/TFORMMAIN_ImageListAM_n2_16x24.png");
    pixKnob = loadKeyed(":/img/TFORMMAIN_ImageKnob.png");
    Knob::bitmap = &pixKnob;

    PageControlMain->setFocusPolicy(Qt::NoFocus);
    PageControlMain->tabBar()->setFocusPolicy(Qt::NoFocus);
    MemoUndoLogT->hide();
    // "Follow song" sits right of Follow-mode; the level meters move over to make room
    SpeedButtonFollowSong = new QToolButton(assc);
    SpeedButtonFollowSong->setObjectName("SpeedButtonFollowSong");
    SpeedButtonFollowSong->setGeometry(590, 10, 86, 22);
    SpeedButtonFollowSong->setText("Follow song");
    SpeedButtonFollowSong->setToolTip("On: the pattern view follows the song while it plays.\n"
                                      "Off: the editor stays where it is and the playing row is marked.");
    SpeedButtonFollowSong->setCheckable(true);
    SpeedButtonFollowSong->setChecked(true);
    SpeedButtonFollowSong->setFocusPolicy(Qt::NoFocus);
    Bevel4->setGeometry(678, 1, 2, 38);
    ScrollBoxLvl->move(684, 4);
    Bevel36->hide();    // leftovers on the form, covered by the panels in the original
    Label2->hide();

    // ---- pattern editor
    PanelPtnHeader->setFrameStyle(QFrame::NoFrame);
    ScrollBoxPtn->setContent(PanelPtnHeader, PaintBoxPtn);
    ScrollBoxPtn->onFocusChange = [this] {
        patternRedraw();
        if (!ScrollBoxPtn->hasFocus()) {
            AudioLock lock;
            player_auto_stop();
        }
    };
    ScrollBoxPtn->onResize = [this] { scrollToCursorChannel(); };
    PaintBoxPtn->onPaint = [this](QPainter &p) { paintPattern(p); };
    PaintBoxPtn->onMouseDown = [this](QMouseEvent *e) { PaintBoxPtnMouseDown(e); };
    PaintBoxPtn->onMouseMove = [this](QMouseEvent *e) { PaintBoxPtnMouseMove(e); };
    PaintBoxPtn->onMouseUp = [this](QMouseEvent *e) { PaintBoxPtnMouseUp(e); };
    PaintBoxPtn->onWheel = [this](QWheelEvent *e) {
        // (the original gets wheel messages through the focused scroll box)
        cursorMoveRows(e->angleDelta().y() < 1 ? 1 : -1, false);
        patternRedraw();
        e->accept();
    };
    PaintBoxPtnPreview->onPaint = [this](QPainter &p) { paintPatternPreview(p); };
    createPatternHeader();

    StringGridOrd->cellText = [](int c) {
        if (c >= g_song.orderLen)
            return QString("...");
        QString s = QString::number(g_song.order[c].pattern);
        if (c == g_song.loopPos)
            s += "L";
        return s;
    };
    StringGridOrd->onMouseDown = [this](int col, QMouseEvent *) { orderMouseDown(col); };
    StringGridOrd->onDrop = [this](int from, int to) { orderDrop(from, to); };
    StringGridOrd->canDrop = [](int from, int to) {
        return from >= 0 && to >= 0 && from < g_song.orderLen && to < g_song.orderLen;
    };
    StringGridOrd->onContextMenu = [this](const QPoint &g) { PopupMenuOrd->popup(g); };

    // ---- level meters
    PaintBoxLvl->onPaint = [this](QPainter &p) { paintLevels(p); };

    // ---- instrument page
    createKnobs();
    PaintBox *env[4] = {PaintBoxEnv1, PaintBoxEnv2, PaintBoxEnv3, PaintBoxEnv4};
    PaintBox *am[4] = {ImageAM1, ImageAM2, ImageAM3, ImageAM4};
    PaintBox *eg[4] = {ImageEG1, ImageEG2, ImageEG3, ImageEG4};
    for (int i = 0; i < 4; i++) {
        PaintBox *e = env[i];
        e->onPaint = [this, i, e](QPainter &p) { paintEnvelope(p, i, e->size()); };
        // dragging one envelope onto another copies (right button) or exchanges (left) operators
        e->onMouseDown = [this, i](QMouseEvent *ev) {
            envDragOp = i;
            envDragButton = ev->button();
        };
        e->onMouseMove = [e](QMouseEvent *) { e->setCursor(Qt::DragMoveCursor); };
        e->onMouseUp = [this, i, e](QMouseEvent *ev) {
            e->unsetCursor();
            int src = envDragOp;
            envDragOp = -1;
            QWidget *w = QApplication::widgetAt(ev->globalPosition().toPoint());
            PaintBox *envs[4] = {PaintBoxEnv1, PaintBoxEnv2, PaintBoxEnv3, PaintBoxEnv4};
            for (int t = 0; t < 4; t++) {
                if (w == envs[t] && src >= 0 && t != src) {
                    OpCopyDialog *d = OpCopyDialog::instance(this);
                    d->srcOp = src;
                    d->dstOp = t;
                    d->copy = envDragButton == Qt::RightButton;
                    d->exchange = envDragButton == Qt::LeftButton;
                    applyOperatorCopy();
                }
            }
            (void)i;
        };
        am[i]->onPaint = [this, i](QPainter &p) {
            const Instrument *ins = &g_song.ins[U.curIns];
            p.drawPixmap(0, 0, sheetFrame(pixAM, ins->op[i].dr & 0x80 ? 1 : 0, 16, 24));
        };
        // 0x42773c
        am[i]->onMouseDown = [this, i](QMouseEvent *) {
            g_song.ins[U.curIns].op[i].dr ^= 0x80;
            updateInstrumentControls();
        };
        eg[i]->onPaint = [this, i](QPainter &p) {
            int ssg = g_song.ins[U.curIns].op[i].ssg;
            p.drawPixmap(0, 0, sheetFrame(pixEG, ssg ? ssg - 7 : 0, 44, 22));
        };
        // 0x427864: list of SSG-EG shapes while the button is held
        eg[i]->onMouseDown = [this, i, eg](QMouseEvent *ev) {
            if (ev->button() != Qt::LeftButton)
                return;
            int ssg = g_song.ins[U.curIns].op[i].ssg;
            delete picker;
            picker = new PickerPopup(this, pixEG, 9, 1, QSize(44, 22), QSize(44, 24), QPoint(4, 4), true);
            picker->resize(52, 222);
            picker->current = ssg ? ssg - 7 : 0;
            pickerOp = i;
            QPoint g = ev->globalPosition().toPoint();
            picker->move(g.x() - 0x1e, g.y() - (picker->current * 0x18 + 0xf));
            picker->show();
        };
        eg[i]->onMouseMove = [this](QMouseEvent *ev) {
            if (picker)
                picker->track(ev->globalPosition().toPoint());
        };
        eg[i]->onMouseUp = [this](QMouseEvent *) {
            if (!picker)
                return;
            int v = picker->current;
            picker->hide();
            picker->deleteLater();
            picker = nullptr;
            setSsgEg(v, pickerOp);
        };
    }
    PaintBoxAlgo->onPaint = [this](QPainter &p) {
        p.drawPixmap(0, 0, sheetFrame(pixAlgo, g_song.ins[U.curIns].alg & 7, 85, 44));
    };
    // 0x4277a8
    PaintBoxAlgo->onMouseDown = [this](QMouseEvent *ev) {
        if (ev->button() != Qt::LeftButton)
            return;
        delete picker;
        picker = new PickerPopup(this, pixAlgo, 8, 4, QSize(85, 44), QSize(0x5a, 0x32), QPoint(4, 4), false);
        picker->resize(365, 105);
        picker->current = g_song.ins[U.curIns].alg & 7;
        QPoint g = ev->globalPosition().toPoint();
        picker->move(g.x() - ((picker->current & 3) * 0x5a + 0x2d), g.y() - ((picker->current >> 2) * 0x32 + 0x19));
        picker->show();
    };
    PaintBoxAlgo->onMouseMove = [this](QMouseEvent *ev) {
        if (picker)
            picker->track(ev->globalPosition().toPoint());
    };
    PaintBoxAlgo->onMouseUp = [this](QMouseEvent *) {
        if (!picker)
            return;
        int v = picker->current;
        picker->hide();
        picker->deleteLater();
        picker = nullptr;
        setAlgorithm(v);
    };

    PaintBoxKeys->onPaint = [this](QPainter &p) { paintKeys(p); };
    ScrollBoxKeys->onFocusChange = [this] { PaintBoxKeys->update(); };
    // 0x423890
    PaintBoxKeys->onMouseDown = [this](QMouseEvent *e) {
        ScrollBoxKeys->setFocus();
        QPoint pt = e->position().toPoint();
        if (previewKeyDown(0xff, keysNoteAt(pt.x(), pt.y()), false))
            PaintBoxKeys->update();
        if (pt.y() < 0x10) {
            int base = U.octave;
            if (base > 1)
                base--;
            if (base > 6)
                base = 6;
            UpDownOctave->setValue(pt.x() / 0xb6 + base);
            PaintBoxKeys->update();
        }
    };
    // 0x423914
    PaintBoxKeys->onMouseUp = [this](QMouseEvent *e) {
        QPoint pt = e->position().toPoint();
        if (previewKeyUp(0xff, keysNoteAt(pt.x(), pt.y())) | previewKeyUp(0xff, -1))
            PaintBoxKeys->update();
    };
    // 0x423948: left button glides between keys, right button bends the held note
    PaintBoxKeys->onMouseMove = [this](QMouseEvent *e) {
        QPoint pt = e->position().toPoint();
        int note = keysNoteAt(pt.x(), pt.y());
        bool redraw = false;
        if (e->buttons() & Qt::LeftButton)
            redraw = previewKeyDown(0xff, note, false);
        if ((e->buttons() & Qt::RightButton) && !(e->buttons() & Qt::LeftButton))
            redraw = previewSlide(note, false);
        if (redraw)
            PaintBoxKeys->update();
    };
    PaintBoxKeys->onWheel = [this](QWheelEvent *e) {
        int o = U.octave + (e->angleDelta().y() < 1 ? 1 : -1);
        UpDownOctave->setValue(qBound(1, o, 8));
        PaintBoxKeys->update();
        e->accept();
    };

    ListBoxIns->setItemDelegate(new SlotDelegate(this, [](int i) { return ins_is_empty(&g_song.ins[i]); }));
    ListBoxSample->setItemDelegate(new SlotDelegate(this, [](int i) { return sample_is_empty(&g_song.sample[i]); }));
    for (QListWidget *lb : {ListBoxIns, ListBoxSample}) {
        lb->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        lb->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    }
    // 0x422228
    connect(ListBoxIns, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!updating && row >= 0)
            selectInstrument(row);
    });
    // 0x427978
    connect(ListBoxSample, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!updating && row >= 0)
            selectSample(row);
    });
    PaintBoxSample->onPaint = [this](QPainter &p) { paintSample(p); };

    // colour swatch opens the colour dialog too (0x425898)
    ShapeColorCtrl->installEventFilter(this);

    connect(PageControlMain, &QTabWidget::currentChanged, this, [this](int) {
        QWidget *page = PageControlMain->currentWidget();
        // leaving the instrument page releases preview notes (0x4258ac)
        if (page != TabSheetIns) {
            if (previewReleaseAll())
                PaintBoxKeys->update();
        }
        // (in the original the tab control itself holds the focus after a page change; here the
        // focus goes to the control that takes the note keys, never to a text field)
        if (page == TabSheetPtn) {
            ScrollBoxPtn->setFocus();                       // 0x421e0c
        } else if (page == TabSheetIns) {
            if (UpDownAIns->value() != 0) {                 // 0x426dbc
                updating = true;
                ListBoxIns->setCurrentRow(UpDownAIns->value() - 1);
                updating = false;
                selectInstrument(UpDownAIns->value() - 1);
            }
            ScrollBoxKeys->setFocus();
        } else if (page == TabSheetSample) {
            ListBoxSample->setFocus();
        } else if (page == TabSheetInfo) {
            updateInfoPage();                               // 0x422220
            page->setFocus();
        } else if (page == TabSheetSetup) {
            PaintBoxPtnPreview->update();
            page->setFocus();
        }
    });
    for (QWidget *page : {TabSheetInfo, TabSheetSetup})
        page->setFocusPolicy(Qt::ClickFocus);

    connectUi(this);
    connect(SpeedButtonFollowSong, &QToolButton::clicked, this, [this] {
        if (following() && Timer1->isActive())
            syncFromPlayer();   // jump back to the song position
        else
            patternRedraw();
    });
    FilePlayVgm = new QAction("Play VGM file...", this);
    MainMenuFile->insertAction(FileSave, FilePlayVgm);
    connect(FilePlayVgm, &QAction::triggered, this, [this] { FilePlayVgmClick(FilePlayVgm); });
    for (QToolButton *b : {SpeedButtonRate8000, SpeedButtonRate11025, SpeedButtonRate16000, SpeedButtonRate22050,
                           SpeedButtonRate32000})
        connect(b, &QToolButton::pressed, this, [this] { undoPushHeader(); });     // 0x428068
    connect(TrackBarFMVol, &QSlider::valueChanged, this, [this] { TrackBarGlobalVolChange(); });
    connect(ComboBoxBufSize, QOverload<int>::of(&QComboBox::activated), this, [this] { ComboBoxBufCountChange(); });
    for (int i = 0; i < 10; i++)
        recent[i] = findChild<QAction *>(QString("Recent%1").arg(i));
    // exclusive groups of the instrument test mode and similar speed buttons
    QHash<QPair<QWidget *, int>, QButtonGroup *> groups;
    for (QToolButton *b : central->findChildren<QToolButton *>()) {
        int g = b->property("groupIndex").toInt();
        if (g == 0 || b->property("allowAllUp").toBool())
            continue;
        QPair<QWidget *, int> key(b->parentWidget(), g);
        if (!groups.contains(key)) {
            groups[key] = new QButtonGroup(b->parentWidget());
            groups[key]->setExclusive(true);
        }
        groups[key]->addButton(b);
    }
    connect(SpeedButtonFMPoly->group(), &QButtonGroup::buttonClicked, this, [this] {
        if (previewReleaseAll())
            PaintBoxKeys->update();
    });

    // ---- program state
    sample_data_init();
    clip_init();
    undoRing.resize(UNDO_MAX);
    for (int i = 0; i < 6; i++)
        U.preview[i] = {-1, -1};
    U.fontName = "Courier";
    loadColors(false);
    loadSettings();
    fillFontList();
    updateFont();

    song_new();
    currentFile = "untitled.vge";
    audioOpen(true);
    loadKeyboardLayout(U.kbdLayout);
    fillMidiList(midiDevice);
    ComboBoxMidiChange();

    updateInsList(-1, true);
    updateSampleList(-1, true);
    resetUi();
    updateInfoPage();
    setTitle(currentFile);
    undoReset();
    ComboBoxColorCtrl->setCurrentIndex(0);
    ComboBoxColorCtrlChange();
    ButtonRegVGE->setEnabled(false);        // shell file association is Windows-only
    ButtonUnregVGE->setEnabled(false);
    MainMenuHelpClick();

    int menuH = MainMenu->sizeHint().height();
    resize(870, 587 + menuH);    // (862 in the original; widened for the Follow song button)
    setMinimumSize(640, 400);
    if (!winGeometry.isEmpty())
        restoreGeometry(winGeometry);

    Timer1->setInterval(15);
    Timer2->setInterval(20);
    Timer2->start();
    qApp->installEventFilter(this);
    PageControlMain->setCurrentWidget(TabSheetPtn);
    ScrollBoxPtn->setFocus();
    layoutForm();
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    midi_close();
    audio_close();
    chips_shutdown();
    g_mainWindow = nullptr;
}

// TPanel alTop / TPageControl alClient / TScrollBox alClient of the original form
void MainWindow::layoutForm()
{
    QWidget *c = centralWidget();
    int w = c->width(), h = c->height();
    assc->setGeometry(0, 0, w, 41);
    PageControlMain->setGeometry(0, 41, w, h - 41);
    QSize page = TabSheetPtn->size();
    PanelPtnCtrl->setGeometry(0, 0, page.width(), 97);
    ScrollBoxPtn->setGeometry(0, 97, page.width(), page.height() - 97);
}

void MainWindow::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);
    layoutForm();
}

// ---------------------------------------------------------------- settings

void MainWindow::loadColors(bool fromSettings)
{
    static const unsigned defaults[COL_COUNT] = {
        0, 0xffffff, 0xffe0e0, 0x000000, 0xffffff, 0xff8080, 0x80c080, 0x00c0a0, 0xe0e0c0, 0xe0e090, 0xc0c0c0, 0x808080,
    };
    QSettings st;
    for (int i = 0; i < COL_COUNT; i++) {
        int v = fromSettings ? st.value(kColorKeys[i], -1).toInt() : -1;
        if (v < 0)
            v = i == 0 ? (int)toColorRef(palette().color(QPalette::Button)) : (int)defaults[i];
        U.colors[i] = (unsigned)v;
    }
}

void MainWindow::loadSettings()
{
    QSettings st;
    ComboBoxBufSize->setCurrentIndex(qBound(0, st.value("WaveBlockSize", 1).toInt(), ComboBoxBufSize->count() - 1));
    ComboBoxBufCount->setCurrentIndex(qBound(0, st.value("WaveBlockCount", 4).toInt(), ComboBoxBufCount->count() - 1));
    TrackBarGlobalVol->setValue(st.value("WaveOutputVolume", 0x80).toInt());
    TrackBarFMVol->setValue(st.value("YM2612Volume", 0x80).toInt());
    UpDownFontSize->setValue(st.value("PatternFontSize", 9).toInt());
    U.fontSize = UpDownFontSize->value();
    UpDownPageStep->setValue(st.value("PatternEditorPageStep", 16).toInt());
    U.fontName = st.value("PatternFontName", "Courier").toString();
    CheckBoxVertSliders->setChecked(st.value("InstrumentHoriziontalSliders", 1).toInt() == 0);
    Knob::verticalSlider = CheckBoxVertSliders->isChecked();
    UpDownRowHighlight->setValue(st.value("RowHighlight", 4).toInt());
    CheckBoxFadeOut->setChecked(st.value("WavExportFadeOut", 1).toInt() != 0);
    int step = st.value("PatternEditorAutostep", 1).toInt();
    SpeedButtonAStep->setChecked(step > 0);
    UpDownAStep->setEnabled(step > 0);
    UpDownAStep->setValue(abs(step));
    U.autoStep = step > 0 ? step : 0;
    CheckBoxPtnAutorepeat->setChecked(st.value("PatternEditorAutorepeat", 0).toInt() != 0);
    CheckBoxPtnKeyoff->setChecked(st.value("PatternEditorKeyoff", 0).toInt() != 0);
    CheckBoxPtnReturnToStart->setChecked(st.value("PatternEditorReturnToStart", 0).toInt() != 0);
    CheckBoxPtnNoteDots->setChecked(st.value("PatternEditorDots", 0).toInt() != 0);
    CheckBoxOneInstance->setChecked(st.value("OneInstance", 0).toInt() != 0);
    ComboBoxShowPrevNext->setCurrentIndex(qBound(0, st.value("PatternShowPrevNext", 0).toInt(), 2));
    CheckBoxPtnAutoHStep->setChecked(st.value("PatternEditorAutoHStep", 0).toInt() != 0);
    CheckBoxBackup->setChecked(st.value("BackupModules", 1).toInt() != 0);
    CheckBoxDefaultIns->setChecked(st.value("DefaultInstrument", 1).toInt() != 0);
    g_defaultInstrument = CheckBoxDefaultIns->isChecked();
    bool v150 = st.value("ExportVGM150", 1).toInt() != 0;
    RadioButtonVGM150->setChecked(v150);
    RadioButtonVGM160->setChecked(!v150);
    winGeometry = st.value("WindowGeometry").toByteArray();
    for (int i = 0; i < 10; i++) {
        QString name = st.value(QString("RecentName%1").arg(i), "none").toString();
        if (recent[i])
            recent[i]->setText(QString("&%1: %2").arg(i).arg(name));
    }
    loadColors(true);
    U.kbdLayout = st.value("KeyboardLayout", "Default").toString();
    U.pathInstrument = st.value("PathInstrument", ".").toString();
    U.pathSample = st.value("PathSample", ".").toString();
    U.pathModule = st.value("PathModule", ".").toString();
    U.pathImport = st.value("PathImport", ".").toString();
    U.pathExport = st.value("PathExport", ".").toString();
    midiDevice = st.value("MidiDeviceID", 0).toInt();
    CheckBoxMidiVelocity->setChecked(st.value("MidiRecordVelocity", 0).toInt() != 0);
    SpeedButtonFollowSong->setChecked(st.value("FollowSong", 1).toInt() != 0);
}

void MainWindow::saveSettings()
{
    QSettings st;
    st.setValue("WaveBlockSize", ComboBoxBufSize->currentIndex());
    st.setValue("WaveBlockCount", ComboBoxBufCount->currentIndex());
    st.setValue("WaveOutputVolume", TrackBarGlobalVol->value());
    st.setValue("YM2612Volume", TrackBarFMVol->value());
    st.setValue("PatternFontSize", U.fontSize);
    st.setValue("PatternEditorPageStep", UpDownPageStep->value());
    st.setValue("PatternFontName", U.fontName);
    st.setValue("InstrumentHoriziontalSliders", CheckBoxVertSliders->isChecked() ? 0 : 1);
    st.setValue("RowHighlight", UpDownRowHighlight->value());
    st.setValue("WavExportFadeOut", CheckBoxFadeOut->isChecked() ? 1 : 0);
    st.setValue("PatternEditorAutostep", SpeedButtonAStep->isChecked() ? UpDownAStep->value() : -UpDownAStep->value());
    st.setValue("PatternEditorAutorepeat", CheckBoxPtnAutorepeat->isChecked() ? 1 : 0);
    st.setValue("PatternEditorKeyoff", CheckBoxPtnKeyoff->isChecked() ? 1 : 0);
    st.setValue("PatternEditorReturnToStart", CheckBoxPtnReturnToStart->isChecked() ? 1 : 0);
    st.setValue("PatternEditorDots", CheckBoxPtnNoteDots->isChecked() ? 1 : 0);
    st.setValue("OneInstance", CheckBoxOneInstance->isChecked() ? 1 : 0);
    st.setValue("PatternShowPrevNext", ComboBoxShowPrevNext->currentIndex());
    st.setValue("PatternEditorAutoHStep", CheckBoxPtnAutoHStep->isChecked() ? 1 : 0);
    st.setValue("BackupModules", CheckBoxBackup->isChecked() ? 1 : 0);
    st.setValue("DefaultInstrument", CheckBoxDefaultIns->isChecked() ? 1 : 0);
    st.setValue("ExportVGM150", RadioButtonVGM150->isChecked() ? 1 : 0);
    st.setValue("WindowGeometry", saveGeometry());
    for (int i = 0; i < 10; i++)
        if (recent[i])
            st.setValue(QString("RecentName%1").arg(i), recent[i]->text().mid(4));
    for (int i = 0; i < COL_COUNT; i++)
        st.setValue(kColorKeys[i], (int)U.colors[i]);
    st.setValue("KeyboardLayout", U.kbdLayout);
    st.setValue("PathInstrument", U.pathInstrument);
    st.setValue("PathSample", U.pathSample);
    st.setValue("PathModule", U.pathModule);
    st.setValue("PathImport", U.pathImport);
    st.setValue("PathExport", U.pathExport);
    st.setValue("MidiDeviceID", ComboBoxMidi->currentIndex());
    st.setValue("MidiRecordVelocity", CheckBoxMidiVelocity->isChecked() ? 1 : 0);
    st.setValue("FollowSong", following() ? 1 : 0);
}

// 0x41f0b4: only fonts whose pattern glyphs all have the same width are offered
void MainWindow::fillFontList()
{
    ComboBoxFont->clear();
    const QString probe = " -|*#0123456789ABCDEF";
    for (const QString &family : QFontDatabase::families()) {
        if (QFontDatabase::isPrivateFamily(family))
            continue;
        QFont f(family);
        f.setPointSize(10);
        QFontMetrics fm(f);
        int w = fm.horizontalAdvance(probe[0]);
        bool fixed = w > 0;
        for (int i = 1; fixed && i < probe.size(); i++)
            fixed = fm.horizontalAdvance(probe[i]) == w;
        if (fixed)
            ComboBoxFont->addItem(family);
    }
    int idx = ComboBoxFont->findText(U.fontName);
    if (idx < 0) {
        for (const char *alt : {"Liberation Mono", "DejaVu Sans Mono", "Monospace"}) {
            idx = ComboBoxFont->findText(alt);
            if (idx >= 0)
                break;
        }
        if (idx < 0)
            idx = 0;
        U.fontName = ComboBoxFont->itemText(idx);
    }
    ComboBoxFont->setCurrentIndex(idx);
}

// 0x41b83c
void MainWindow::fillMidiList(int device)
{
    ComboBoxMidi->clear();
    ComboBoxMidi->addItem("None");
    ComboBoxMidi->addItems(midi_list_inputs());
    if (device < 0 || device >= ComboBoxMidi->count())
        device = 0;
    ComboBoxMidi->setCurrentIndex(device);
}

// 0x426d70
void MainWindow::ComboBoxMidiChange(QObject *)
{
    midi_close();
    midiDevice = ComboBoxMidi->currentIndex();
    if (midiDevice > 0)
        midi_open(midiDevice - 1, [this](unsigned msg) {
            QMetaObject::invokeMethod(this, [this, msg] { midiMessage(msg); }, Qt::QueuedConnection);
        });
}

// 0x41e480
void MainWindow::loadKeyboardLayout(const QString &name)
{
    auto useDefault = [this] {
        QFile f(":/default.kbd");
        f.open(QIODevice::ReadOnly);
        QByteArray text = f.readAll();
        U.kbdLayout = "Default";
        kbd_load(nullptr, text.constData(), (unsigned)text.size());
        StaticTextKBDLayout->setText(QString::fromLatin1(kbd_layout_name()).toLower());
        StaticTextKBDLayout->setToolTip(StaticTextKBDLayout->text());
        SpeedButtonKBDDefault->setEnabled(false);
    };
    if (name != "Default") {
        if (kbd_load(name.toLocal8Bit().constData(), nullptr, 0)) {
            U.kbdLayout = name;
            StaticTextKBDLayout->setText(QString::fromLatin1(kbd_layout_name()).toLower());
            StaticTextKBDLayout->setToolTip(StaticTextKBDLayout->text());
            SpeedButtonKBDDefault->setEnabled(true);
            return;
        }
        messageBox("Can't load keyboard layout '" + name + "'\nDefault layout will be used instead\n", "Error", 0);
    }
    useDefault();
}

void MainWindow::SpeedButtonKBDLoadClick(QObject *)
{
    kbd_clear();
    QString fn = QFileDialog::getOpenFileName(this, "Load keyboard layout", QString(),
                                              "VGM Music Maker keyboard layout (*.kbd)");
    if (!fn.isEmpty())
        loadKeyboardLayout(fn);
}

void MainWindow::SpeedButtonKBDDefaultClick(QObject *)
{
    if (messageBox("Use default keyboard layout?", "Confirm", 1) == 1)
        loadKeyboardLayout("Default");
}

// ---------------------------------------------------------------- helpers

// 0x41bba0. type: 0 = OK, 1 = OK/Cancel (returns 1 for OK), 4 = Yes/No (returns 6 for Yes)
int MainWindow::messageBox(const QString &text, const QString &title, int type)
{
    kbd_clear();
    QMessageBox box(this);
    box.setWindowTitle(title);
    box.setText(text);
    if (type == 4) {
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box.setDefaultButton(QMessageBox::Yes);
        return box.exec() == QMessageBox::Yes ? 6 : 7;
    }
    if (type == 1) {
        box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        return box.exec() == QMessageBox::Ok ? 1 : 2;
    }
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
    return 1;
}

// 0x41d8d0
void MainWindow::setTitle(const QString &name)
{
    if (!name.isEmpty())
        U.fileTitle = name;
    QString t = QString("%1 [%2%3]").arg(g_appVersion, U.modified ? "*" : "", U.fileTitle);
    if (vgmMode)
        t += " - playing " + QFileInfo(vgmFile).fileName();
    setWindowTitle(t);
}

// 0x41d4fc: most recent first; an entry already in the list moves to the top
void MainWindow::addRecent(const QString &name)
{
    QStringList names;
    for (int i = 0; i < 10; i++)
        names << (recent[i] ? recent[i]->text().mid(4) : QString("none"));
    int at = names.indexOf(name);
    if (at < 0)
        at = 9;
    for (int i = at; i > 0; i--)
        names[i] = names[i - 1];
    names[0] = name;
    for (int i = 0; i < 10; i++)
        if (recent[i])
            recent[i]->setText(QString("&%1: %2").arg(i).arg(names[i]));
}

// 0x422d28
void MainWindow::Recent0Click(QObject *Sender)
{
    QAction *a = qobject_cast<QAction *>(Sender);
    if (!a)
        return;
    QString name = a->text().mid(4);
    if (name != "none")
        loadModule(name, Sender);
}

// 0x41be08
void MainWindow::updateStatus(bool calcTotal, bool calcPos)
{
    if (calcTotal) {
        AudioLock lock;
        U.songTicks = song_calc_timing();
    }
    if (calcPos)
        U.posTicks = song_position_tick(U.curOrder, U.curRow);
    unsigned fps = g_song.fps ? g_song.fps : 1;
    int total = (int)((unsigned)U.songTicks / fps);
    int pos = (int)((unsigned)U.posTicks / fps);
    LabelTTimeVal->setText(QString::asprintf("%2.2i:%2.2i of %2.2i:%2.2i", pos / 60, pos % 60, total / 60, total % 60));
    LabelTTimeVal->adjustSize();
    LabelTFrmVal->setText(QString("%1 of %2").arg(U.posTicks).arg(U.songTicks));
    LabelTFrmVal->adjustSize();
}

// 0x41bfe4
void MainWindow::updateOrderList()
{
    StringGridOrd->setSelected(U.curOrder);
    StringGridOrd->refresh();
    updateStatus(true, false);
}

// 0x41c1dc
void MainWindow::updateSpeedControls()
{
    UpDownSpdEven->setValue((int)g_song.speedEven);
    UpDownSpdOdd->setValue((int)g_song.speedOdd);
    UpDownInter->setValue((int)g_song.interleave);
}

// 0x41f394: tempo shown for a beat of "row highlight" rows, averaged over both speeds
void MainWindow::updateBpm()
{
    if (g_song.fps == 0)
        return;
    float fps = (float)g_song.fps;
    float hl = (float)UpDownRowHighlight->value();
    float a = (float)((60.0L / ((long double)UpDownSpdEven->value() / fps)) / hl);
    float b = (float)((60.0L / ((long double)UpDownSpdOdd->value() / fps)) / hl);
    double bpm = (double)((a + b) * 0.5f);
    // FloatToStrF(ffFixed, precision 4, 2 digits): four significant digits, two decimals
    double shown = bpm;
    if (bpm > 0) {
        double mag = pow(10.0, 3 - floor(log10(bpm)));
        shown = round(bpm * mag) / mag;
    }
    LabelBPM->setText(QString::number(shown, 'f', 2) + "BPM");
    LabelBPM->adjustSize();
}

// 0x41c340
void MainWindow::updateClockInfo()
{
    EditClockFM->setText(QString::number(g_song.fmClock));
    EditClockPSG->setText(QString::number(g_song.psgClock));
    EditClockFPS->setText(QString::number(g_song.fps));
    bool pal = g_song.fmClock == 7600489 && g_song.psgClock == 3546894;
    bool ntsc = g_song.fmClock == 7670453 && g_song.psgClock == 3579545;
    if (pal || ntsc)
        LabelClockInfo->setText("Click Custom button to apply custom settings.");
    else
        LabelClockInfo->setText("Warning: custom base clock is not possible on an unmodified console.");
    QButtonGroup *grp = SpeedButtonClockPAL->group();
    if (grp)
        grp->setExclusive(false);
    SpeedButtonClockPAL->setChecked(pal && g_song.fps == 50);
    SpeedButtonClockNTSC->setChecked(ntsc && g_song.fps == 60);
    if (grp)
        grp->setExclusive(true);
    AudioLock lock;
    tables_init();
}

// 0x41bc3c
void MainWindow::updateInfoPage()
{
    char date[16];
    updating = true;
    date_to_str(date, g_song.dateCreated);
    EditTrkCreated->setText(date);
    date_to_str(date, g_song.dateModified);
    EditTrkSaved->setText(date);
    if (g_song.saveCount == 0xffff)
        EditTrkSCount->setText("very many times");
    else
        EditTrkSCount->setText(QString::number(g_song.saveCount));
    EditTrkName->setText(QString::fromLatin1(g_song.name));
    EditTrkAuthor->setText(QString::fromLatin1(g_song.author));
    MemoTrkMessage->setPlainText(QString::fromLatin1(g_song.message));
    updating = false;
}

// 0x41de48 equivalent: the module header holds plain ASCII only
static QString asciiOnly(const QString &s)
{
    QString out;
    for (QChar c : s)
        if (c.unicode() < 0x80)
            out += c;
    return out;
}

static void storeText(char *dst, int size, const QString &s)
{
    QByteArray b = s.toLatin1();
    memset(dst, 0, (size_t)size);
    memcpy(dst, b.constData(), (size_t)qMin(b.size(), size - 1));
}

void MainWindow::EditTrkNameChange(QObject *)
{
    if (updating)
        return;
    QString t = asciiOnly(EditTrkName->text());
    storeText(g_song.name, sizeof(g_song.name), t);
    if (t != EditTrkName->text())
        EditTrkName->setText(t);
}

void MainWindow::EditTrkAuthorChange(QObject *)
{
    if (updating)
        return;
    QString t = asciiOnly(EditTrkAuthor->text());
    storeText(g_song.author, sizeof(g_song.author), t);
    if (t != EditTrkAuthor->text())
        EditTrkAuthor->setText(t);
}

void MainWindow::MemoTrkMessageChange(QObject *)
{
    if (updating)
        return;
    QString t = asciiOnly(MemoTrkMessage->toPlainText());
    storeText(g_song.message, sizeof(g_song.message), QString(t).replace("\n", "\r\n"));
    if (t != MemoTrkMessage->toPlainText()) {
        updating = true;
        MemoTrkMessage->setPlainText(t);
        updating = false;
    }
}

// 0x41c218
void MainWindow::resetUi()
{
    U.selX1 = U.selY1 = U.selX2 = U.selY2 = 0;
    U.selChan = U.selRow = U.selW = U.selH = 0;
    U.selecting = false;
    U.curRow = 0;
    U.curCol = 0;
    U.curOrder = 0;
    U.curIns = 0;
    U.curSample = 0;
    updateInstrumentControls();
    updateSampleInfo();
    U.curPattern = g_song.order[U.curOrder].pattern;
    setCurrentPattern(U.curPattern);
    updateOrderList();
    updateStatus(false, true);
    updating = true;
    ListBoxIns->setCurrentRow(0);
    ListBoxSample->setCurrentRow(0);
    updating = false;
    updateSpeedControls();
    setNoPattern(false);
    cursorColumnChanged();
    for (int i = 0; i < CHANNELS; i++)
        U.fxColumn[i] = 0;
    updatePatternHeader();
    updateBpm();
    updateClockInfo();
    scrollToCursorChannel();
}

// 0x41bbc4: after undo/redo
void MainWindow::refreshAll()
{
    setCurrentPattern(g_song.order[U.curOrder].pattern);
    updateInstrumentControls();
    updateSampleInfo();
    updateOrderList();
    updateStatus(true, true);
    updateInfoPage();
    updateInsList(-1, true);
    updateSampleList(-1, true);
    updateSpeedControls();
    updateBpm();
    cursorColumnChanged();
    updatePatternHeader();
}

// 0x41dd90
void MainWindow::setNoPattern(bool on)
{
    U.noPattern = on;
    patternRedraw();
    UpDownPtn->setEnabled(!on);
    UpDownPtnLen->setEnabled(!on);
}

// 0x416498
void MainWindow::savePosition()
{
    U.savedRow = U.curRow;
    U.savedPattern = U.curPattern;
    U.savedOrder = U.curOrder;
}

// 0x4164fc
void MainWindow::restorePosition()
{
    U.curRow = U.savedRow;
    U.curPattern = U.savedPattern;
    U.curOrder = U.savedOrder;
    setCurrentPattern(U.curPattern);
    updateOrderList();
    updateStatus(false, true);
}

// 0x4164bc
void MainWindow::setCurrentPattern(int pat)
{
    UpDownPtn->setValue(pat);
    U.curPattern = UpDownPtn->value();
    UpDownPtnLen->setValue(g_song.pattern[pat].length);
    patternRedraw();
}

// ---------------------------------------------------------------- playback

void MainWindow::startPlayback(int order, int loopPattern)
{
    savePosition();
    {
        AudioLock lock;
        player_start(order, 0, 0, (uint8_t)loopPattern, 0, -1);
    }
    Timer1->start();
    SpeedButtonPlayAll->setEnabled(false);
    SpeedButtonPlayPos->setEnabled(false);
    SpeedButtonPlayPtn->setEnabled(false);
    SpeedButtonStop->setEnabled(true);
    SpeedButtonReset->setEnabled(false);
    updateBpm();
}

void MainWindow::SpeedButtonPlayAllClick(QObject *) { if (!SpeedButtonStop->isEnabled()) startPlayback(0, 0); }
void MainWindow::SpeedButtonPlayPosClick(QObject *) { if (!SpeedButtonStop->isEnabled()) startPlayback(U.curOrder, 0); }
void MainWindow::SpeedButtonPlayPtnClick(QObject *) { if (!SpeedButtonStop->isEnabled()) startPlayback(U.curOrder, 1); }

// 0x41debc
void MainWindow::stopPlayback()
{
    if (vgmMode) {
        stopVgm();
        return;
    }
    Timer1->stop();
    {
        AudioLock lock;
        player_stop();
    }
    SpeedButtonPlayAll->setEnabled(true);
    SpeedButtonPlayPos->setEnabled(true);
    SpeedButtonPlayPtn->setEnabled(true);
    SpeedButtonStop->setEnabled(false);
    SpeedButtonReset->setEnabled(true);
    bool wasFollowing = playRow < 0;
    playRow = playOrder = -1;
    if (CheckBoxPtnReturnToStart->isChecked() && wasFollowing)
        restorePosition();
    else
        updateStatus(false, true);
    updateSpeedControls();
    patternRedraw();
}

void MainWindow::SpeedButtonStopClick(QObject *)
{
    stopPlayback();
    updateBpm();
}

void MainWindow::SpeedButtonResetClick(QObject *)
{
    AudioLock lock;
    chips_reset();
}

// 0x41dd50
void MainWindow::stopAndReset(QObject *Sender)
{
    if (SpeedButtonStop->isEnabled())
        SpeedButtonStopClick(Sender);
    AudioLock lock;
    chips_reset();
}

// 0x41dbd4: follow the player while the song is running
void MainWindow::syncFromPlayer()
{
    int row, order, pattern, even, odd, inter;
    {
        AudioLock lock;
        row = g_player.row;
        order = g_player.order;
        pattern = g_player.pattern;
        even = g_player.speedEven;
        odd = g_player.speedOdd;
        inter = g_player.interleave;
    }
    if (!following()) {
        // the view stays put; only the time display and a marker show where the song is
        if (row != playRow || order != playOrder) {
            playRow = row;
            playOrder = order;
            U.posTicks = song_position_tick(order, row);
            updateStatus(false, false);
            patternRedraw();
        }
        return;
    }
    playRow = playOrder = -1;
    bool rowCh = row != U.curRow;
    if (rowCh)
        U.curRow = row;
    bool ordCh = order != U.curOrder;
    if (ordCh)
        U.curOrder = order;
    bool patCh = UpDownPtn->value() != pattern;
    if (patCh) {
        UpDownPtn->setValue(pattern);
        U.curPattern = pattern;
    }
    bool evenCh = UpDownSpdEven->value() != even;
    if (evenCh)
        UpDownSpdEven->setValue(even);
    bool oddCh = UpDownSpdOdd->value() != odd;
    if (oddCh)
        UpDownSpdOdd->setValue(odd);
    bool interCh = UpDownInter->value() != inter;
    if (interCh)
        UpDownInter->setValue(inter);
    bool lenCh = UpDownPtnLen->value() != g_song.pattern[pattern & 0xff].length;
    if (lenCh)
        UpDownPtnLen->setValue(g_song.pattern[pattern & 0xff].length);
    if (lenCh || patCh || ordCh || rowCh) {
        patternRedraw();
        updateStatus(false, true);
    }
    if (patCh || ordCh)
        updateOrderList();
    if (interCh || oddCh || evenCh)
        updateBpm();
}

void MainWindow::Timer1Timer(QObject *)
{
    if (vgmMode) {
        bool playing;
        {
            AudioLock lock;
            playing = vgmplay_playing();
        }
        updateVgmStatus();
        if (!playing)
            stopVgm();
        return;
    }
    syncFromPlayer();
}

// 0x425e0c
void MainWindow::Timer2Timer(QObject *)
{
    QWidget *f = QApplication::focusWidget();
    if (f == ScrollBoxKeys || f == PaintBoxKeys) {
        if (!(QApplication::mouseButtons() & (Qt::LeftButton | Qt::RightButton))) {
            if (previewKeyUp(0xff, -1))
                PaintBoxKeys->update();
        }
    }
    updateLevels();
    if (U.samplePending >= 0) {
        {
            AudioLock lock;
            sample_convert(&g_song.sample[U.samplePending]);
        }
        PaintBoxSample->update();
        U.samplePending = -1;
    }
}

// ---------------------------------------------------------------- audio

// 0x41ee48
void MainWindow::audioOpen(bool chips)
{
    if (chips) {
        AudioLock lock;
        chips_init((int)g_song.fmClock, (int)g_song.psgClock, 44100);
        chips_set_balance(TrackBarGlobalVol->value(), TrackBarFMVol->value());
        applyMute();
    }
    // the original queues (index + 2) waveOut blocks of 0x800 << index bytes
    audio_open((0x800 << ComboBoxBufSize->currentIndex()) / 4);
}

// 0x41eec8
void MainWindow::audioClose(bool chips)
{
    SpeedButtonStopClick();
    audio_close();
    if (chips)
        chips_shutdown();
}

void MainWindow::ComboBoxBufCountChange(QObject *)
{
    audioClose(true);
    audioOpen(true);
}

void MainWindow::TrackBarGlobalVolChange(QObject *)
{
    AudioLock lock;
    chips_set_balance(TrackBarGlobalVol->value(), TrackBarFMVol->value());
}

void MainWindow::SpeedButtonTestSignalClick(QObject *)
{
    g_testSignal = SpeedButtonTestSignal->isChecked();
}

// ---------------------------------------------------------------- module files

void MainWindow::FileNewClick(QObject *Sender)
{
    if (messageBox("Are you sure?", "New module", 4) != 6)
        return;
    stopAndReset(Sender);
    {
        AudioLock lock;
        song_new();
    }
    currentFile = "untitled.vge";
    U.modified = false;
    setTitle(currentFile);
    resetUi();
    updateInfoPage();
    updateInsList(-1, true);
    updateSampleList(-1, true);
    undoReset();
}

// 0x428588
void MainWindow::FileClearSongClick(QObject *Sender)
{
    if (messageBox("Clear song but leave instruments and samples intact, are you sure?", "Clear song", 4) != 6)
        return;
    stopAndReset(Sender);
    {
        AudioLock lock;
        song_clear();
    }
    currentFile = "untitled.vge";
    U.modified = false;
    setTitle(currentFile);
    resetUi();
    updateInfoPage();
    updateInsList(-1, true);
    updateSampleList(-1, true);
    undoReset();
}

void MainWindow::FileLoadClick(QObject *Sender)
{
    kbd_clear();
    QString fn = QFileDialog::getOpenFileName(
        this, "Open module", U.pathModule,
        "VGM Music Maker editable module (*.vge);;VGM file, playback only (*.vgm *.vgz);;All files (*)");
    if (!fn.isEmpty())
        loadModule(fn, Sender);
}

// ---------------------------------------------------------------- VGM playback
// The original can only export VGM files. They are register logs, so they cannot be turned back
// into an editable module; the port can play them through the editor's chips, with the mute
// buttons, volume settings and level meters working as usual.

bool MainWindow::isVgmFile(const QString &filename)
{
    QFile f(filename);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QByteArray head = f.read(4);
    if (head == "Vgm ")
        return true;
    QString ext = QFileInfo(filename).suffix().toLower();
    return head.size() >= 2 && (uint8_t)head[0] == 0x1f && (uint8_t)head[1] == 0x8b && (ext == "vgz" || ext == "vgm");
}

void MainWindow::FilePlayVgmClick(QObject *)
{
    kbd_clear();
    QString fn = QFileDialog::getOpenFileName(this, "Play VGM file", U.pathExport,
                                              "VGM file (*.vgm *.vgz);;All files (*)");
    if (!fn.isEmpty())
        playVgm(fn);
}

bool MainWindow::playVgm(const QString &filename)
{
    stopAndReset(nullptr);
    char error[256];
    bool ok;
    {
        AudioLock lock;
        ok = vgmplay_load(filename.toLocal8Bit().constData(), error);
    }
    if (!ok) {
        messageBox("Can't play '" + filename + "':\n" + QString::fromLatin1(error), "Error!", 0);
        return false;
    }
    const VgmInfo *vi = vgmplay_info();
    // the chips run at the clocks given in the file while it plays
    audioClose(true);
    {
        AudioLock lock;
        chips_init(vi->fmClock ? (int)vi->fmClock : 7670453, vi->psgClock ? (int)vi->psgClock : 3579545, 44100);
        chips_set_balance(TrackBarGlobalVol->value(), TrackBarFMVol->value());
        applyMute();
        chips_reset();
        vgmplay_start(2);
    }
    vgmMode = true;
    vgmFile = filename;
    audio_open((0x800 << ComboBoxBufSize->currentIndex()) / 4);
    addRecent(filename);
    U.pathExport = QFileInfo(filename).absolutePath();
    Timer1->start();
    SpeedButtonPlayAll->setEnabled(false);
    SpeedButtonPlayPos->setEnabled(false);
    SpeedButtonPlayPtn->setEnabled(false);
    SpeedButtonStop->setEnabled(true);
    SpeedButtonReset->setEnabled(false);
    setTitle();
    updateVgmStatus();
    return true;
}

// back to the module: its clocks, its time display
void MainWindow::stopVgm()
{
    if (!vgmMode)
        return;
    vgmMode = false;
    Timer1->stop();
    {
        AudioLock lock;
        vgmplay_stop();
    }
    audio_close();
    {
        AudioLock lock;
        vgmplay_free();
        chips_shutdown();
    }
    audioOpen(true);
    SpeedButtonPlayAll->setEnabled(true);
    SpeedButtonPlayPos->setEnabled(true);
    SpeedButtonPlayPtn->setEnabled(true);
    SpeedButtonStop->setEnabled(false);
    SpeedButtonReset->setEnabled(true);
    setTitle();
    updateStatus(false, false);
}

void MainWindow::updateVgmStatus()
{
    uint32_t pos, len;
    {
        AudioLock lock;
        pos = vgmplay_position();
        len = vgmplay_length();
    }
    int ps = (int)(pos / 44100), ls = (int)(len / 44100);
    LabelTTimeVal->setText(QString::asprintf("%2.2i:%2.2i of %2.2i:%2.2i", ps / 60, ps % 60, ls / 60, ls % 60));
    LabelTTimeVal->adjustSize();
    // frames of 1/60 s, the unit VGM files of the Mega Drive are logged in
    LabelTFrmVal->setText(QString("%1 of %2").arg(pos / 735).arg(len / 735));
    LabelTFrmVal->adjustSize();
}

// 0x41e174
bool MainWindow::loadModule(const QString &filename, QObject *Sender)
{
    if (isVgmFile(filename))
        return playVgm(filename);
    stopAndReset(Sender);
    int res;
    {
        AudioLock lock;
        res = song_load(filename.toLocal8Bit().constData());
    }
    if (res == 0 || res == 2) {
        currentFile = filename;
        addRecent(filename);
        U.modified = false;
        setTitle(filename);
        resetUi();
        updateInfoPage();
        updateInsList(-1, true);
        updateSampleList(-1, true);
        updateBpm();
        updateClockInfo();
    }
    QString msg;
    if (res == 1)
        msg = "File '" + filename + "' not found!";
    else if (res == 2)
        msg = "File '" + filename + "' has wrong format!";
    else if (res == 3)
        msg = "Can't alloc memory to load file!";
    if (res == 0) {
        undoReset();
        U.pathModule = QFileInfo(filename).absolutePath();
        return true;
    }
    messageBox(msg, "Error!", 0);
    return false;
}

// 0x41f4e4: keep the previous version of a module that is about to be overwritten
void MainWindow::backupModule(const QString &filename)
{
    if (!CheckBoxBackup->isChecked() || !QFile::exists(filename))
        return;
    QFileInfo fi(filename);
    QDir dir(fi.absolutePath());
    dir.mkdir("backup");
    QString name = QString("%1/backup/%2_%3.vge")
                       .arg(fi.absolutePath(), fi.completeBaseName(),
                            QDateTime::currentDateTime().toString("ddMMyy_HHmmss"));
    QFile::copy(filename, name);
}

// 0x421f84
void MainWindow::FileSaveClick(QObject *)
{
    kbd_clear();
    QString start = QFileInfo(currentFile).isAbsolute() ? currentFile : QDir(U.pathModule).filePath(currentFile);
    QString fn = QFileDialog::getSaveFileName(this, "Save module", start, "VGM Music Maker editable module (*.vge)");
    if (fn.isEmpty())
        return;
    if (QFileInfo(fn).suffix().isEmpty())
        fn += ".vge";
    backupModule(fn);
    bool ok;
    {
        AudioLock lock;
        ok = song_save(fn.toLocal8Bit().constData());
    }
    if (!ok) {
        messageBox("Can't save file '" + fn + "'", "Error", 0);
        return;
    }
    currentFile = fn;
    addRecent(fn);
    U.modified = false;
    setTitle(fn);
    updateInfoPage();
    U.pathModule = QFileInfo(fn).absolutePath();
}

void MainWindow::FileCloseClick(QObject *) { close(); }
void MainWindow::SpeedButtonModNewClick(QObject *s) { FileNewClick(s); }
void MainWindow::SpeedButtonModOpenClick(QObject *s) { FileLoadClick(s); }
void MainWindow::SpeedButtonModSaveClick(QObject *s) { FileSaveClick(s); }

// 0x424590 / 0x420cfc
void MainWindow::closeEvent(QCloseEvent *e)
{
    if (messageBox("Are you sure?", "Exit confirm", 4) != 6) {
        e->ignore();
        return;
    }
    closing = true;
    Timer1->stop();
    Timer2->stop();
    saveSettings();
    audioClose(false);
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    const QList<QUrl> urls = e->mimeData()->urls();
    if (urls.isEmpty())
        return;
    QString fn = urls.first().toLocalFile();
    if (isVgmFile(fn)) {
        playVgm(fn);
        return;
    }
    if (QFile::exists(fn) && messageBox("Open new module? Current module will be lost.\n", "Warning", 4) == 6)
        loadModule(fn);
}

// ---------------------------------------------------------------- import / export

void MainWindow::ImportTFEClick(QObject *Sender)
{
    stopAndReset(Sender);
    kbd_clear();
    QString fn = QFileDialog::getOpenFileName(this, "Import TFM Music Maker module", U.pathImport,
                                              "TFM Music Maker module (*.tfe)");
    if (fn.isEmpty())
        return;
    bool ok;
    {
        AudioLock lock;
        ok = import_tfm(fn.toLocal8Bit().constData());
    }
    if (!ok)
        return;
    QFileInfo fi(fn);
    currentFile = fi.absolutePath() + "/" + fi.completeBaseName() + ".vge";
    U.modified = false;
    setTitle(currentFile);
    resetUi();
    updateInfoPage();
    updateInsList(-1, true);
    updateSampleList(-1, true);
    undoReset();
    U.pathImport = fi.absolutePath();
}

void MainWindow::ImportMODClick(QObject *Sender)
{
    stopAndReset(Sender);
    kbd_clear();
    QString fn = QFileDialog::getOpenFileName(this, "Import music data from MOD file", U.pathImport,
                                              "MOD music module (*.mod)");
    if (fn.isEmpty())
        return;
    bool ok;
    {
        AudioLock lock;
        ok = import_mod(fn.toLocal8Bit().constData());
    }
    if (!ok)
        return;
    QFileInfo fi(fn);
    currentFile = fi.absolutePath() + "/" + fi.completeBaseName() + ".vge";
    U.modified = false;
    setTitle(currentFile);
    resetUi();
    updateInfoPage();
    updateInsList(-1, true);
    updateSampleList(-1, true);
    undoReset();
    U.pathImport = fi.absolutePath();
}

QString MainWindow::exportName(const QString &ext) const
{
    return QDir(U.pathExport).filePath(QFileInfo(currentFile).completeBaseName() + ext);
}

// 0x41f754
void MainWindow::exportVgm(QObject *Sender, int system, bool gzip)
{
    stopAndReset(Sender);
    kbd_clear();
    QString fn = gzip ? QFileDialog::getSaveFileName(this, "Export VGZ file", exportName(".vgz"), "VGZ file (*.vgz)")
                      : QFileDialog::getSaveFileName(this, "Export VGM file", exportName(".vgm"), "VGM file (*.vgm)");
    if (fn.isEmpty())
        return;
    if (QFileInfo(fn).suffix().isEmpty())
        fn += gzip ? ".vgz" : ".vgm";
    audioClose(false);
    if (!export_vgm(fn.toLocal8Bit().constData(), system, gzip, RadioButtonVGM150->isChecked()))
        messageBox("Can't export file", "Error", 0);
    U.pathExport = QFileInfo(fn).absolutePath();
    audioOpen(false);
}

void MainWindow::ExportVGMGenesisClick(QObject *s) { exportVgm(s, 0, false); }
void MainWindow::ExportVGMSMSClick(QObject *s) { exportVgm(s, 1, false); }
void MainWindow::ExportRegGGClick(QObject *s) { exportVgm(s, 2, false); }
void MainWindow::ExportVGZGenesisClick(QObject *s) { exportVgm(s, 0, true); }
void MainWindow::ExportVGZSMSClick(QObject *s) { exportVgm(s, 1, true); }
void MainWindow::ExportVGZGGClick(QObject *s) { exportVgm(s, 2, true); }

// 0x4258b4
void MainWindow::ExportVGMSMDClick(QObject *Sender)
{
    stopAndReset(Sender);
    kbd_clear();
    QString fn = QFileDialog::getSaveFileName(this, "Export TFD registers dump for SMD", exportName(".tfd"),
                                              "TFM registers dump (*.tfd)");
    if (fn.isEmpty())
        return;
    if (QFileInfo(fn).suffix().isEmpty())
        fn += ".tfd";
    audioClose(false);
    export_tfd(fn.toLocal8Bit().constData());
    U.pathExport = QFileInfo(fn).absolutePath();
    audioOpen(false);
}

// 0x428308
void MainWindow::ExportTextClick(QObject *Sender)
{
    stopAndReset(Sender);
    kbd_clear();
    QString fn = QFileDialog::getSaveFileName(this, "Export module as a text file", exportName(".txt"),
                                              "Text file (*.txt)");
    if (fn.isEmpty())
        return;
    if (QFileInfo(fn).suffix().isEmpty())
        fn += ".txt";
    audioClose(false);
    export_text(fn.toLocal8Bit().constData());
    U.pathExport = QFileInfo(fn).absolutePath();
    audioOpen(false);
}

// 0x4245b8: tag 0 = all channels, 1 = only the channels that are not muted
void MainWindow::ExportWaveAllClick(QObject *Sender)
{
    int tag = Sender ? Sender->property("tag").toInt() : 0;
    stopAndReset(Sender);
    Timer2->stop();
    kbd_clear();
    QString title = QString("Export wave file (%1 channels)").arg(tag == 0 ? "all" : "unmuted");
    QString fn = QFileDialog::getSaveFileName(this, title, exportName(".wav"), "Windows wave file (*.wav)");
    if (!fn.isEmpty()) {
        if (QFileInfo(fn).suffix().isEmpty())
            fn += ".wav";
        audioClose(false);
        uint8_t test = g_testSignal;
        g_testSignal = 0;
        g_wavFadeOut = CheckBoxFadeOut->isChecked();
        RenderWavDialog dlg(this);
        dlg.run(fn, tag == 0);
        audioOpen(false);
        U.pathExport = QFileInfo(fn).absolutePath();
        g_testSignal = test;
    }
    Timer2->start();
}

// ---------------------------------------------------------------- information page: clocks

void MainWindow::SpeedButtonClockPALClick(QObject *)
{
    if (g_song.fmClock != 7600489 || g_song.psgClock != 3546894 || g_song.fps != 50) {
        audioClose(true);
        g_song.fmClock = 7600489;
        g_song.psgClock = 3546894;
        g_song.fps = 50;
        updateClockInfo();
        audioOpen(true);
        updateStatus(true, true);
        updateBpm();
    }
    updateClockInfo();
}

void MainWindow::SpeedButtonClockNTSCClick(QObject *)
{
    if (g_song.fmClock != 7670453 || g_song.psgClock != 3579545 || g_song.fps != 60) {
        audioClose(true);
        g_song.fmClock = 7670453;
        g_song.psgClock = 3579545;
        g_song.fps = 60;
        updateClockInfo();
        audioOpen(true);
        updateStatus(true, true);
        updateBpm();
    }
    updateClockInfo();
}

// 0x428744
void MainWindow::SpeedButtonClockCustomClick(QObject *)
{
    bool ok;
    int fps = EditClockFPS->text().toInt(&ok);
    if (!ok)
        fps = -1;
    int fm = EditClockFM->text().toInt(&ok);
    if (!ok)
        fm = -1;
    int psg = EditClockPSG->text().toInt(&ok);
    if (!ok)
        psg = -1;
    if (fps >= 0)
        fps = qBound(10, fps, 400);
    if (fm >= 0)
        fm = qBound(1000000, fm, 10000000);
    if (psg >= 0)
        psg = qBound(1000000, psg, 4000000);
    SpeedButtonClockCustom->setChecked(false);
    if (fps > 0 && fm > 0 && psg > 0) {
        audioClose(true);
        g_song.psgClock = (uint32_t)psg;
        g_song.fmClock = (uint32_t)fm;
        g_song.fps = (uint32_t)fps;
        audioOpen(true);
        updateStatus(true, true);
        updateBpm();
    }
    updateClockInfo();
}

// ---------------------------------------------------------------- preferences page

void MainWindow::updateFont()
{
    QFont f(U.fontName);
    f.setPointSize(U.fontSize);
    f.setStyleHint(QFont::TypeWriter);
    f.setFixedPitch(true);
    PaintBoxPtn->setFont(f);
    PaintBoxPtnPreview->setFont(f);
    QFontMetrics fm(f);
    U.rowH = fm.height();
    U.charW = fm.horizontalAdvance('1');
    if (U.charW < 1)
        U.charW = 1;
    if (U.rowH < 1)
        U.rowH = 1;
    layoutPatternHeader();
    PaintBoxPtnPreview->update();
    patternRedraw();
    scrollToCursorChannel();
}

// 0x422894
void MainWindow::EditFontSizeChange(QObject *)
{
    U.fontSize = UpDownFontSize->value();
    updateFont();
}

void MainWindow::ComboBoxFontChange(QObject *)
{
    U.fontName = ComboBoxFont->currentText();
    updateFont();
}

// 0x4257d8
void MainWindow::ComboBoxColorCtrlChange(QObject *)
{
    int i = ComboBoxColorCtrl->currentIndex();
    QPalette p = ShapeColorCtrl->palette();
    p.setColor(QPalette::Window, i >= 0 && i < COL_COUNT ? colorRef(U.colors[i]) : QColor(Qt::black));
    ShapeColorCtrl->setPalette(p);
}

// 0x4256e8
void MainWindow::SpeedButtonColorCtrlClick(QObject *)
{
    int i = ComboBoxColorCtrl->currentIndex();
    if (i < 0 || i >= COL_COUNT)
        return;
    kbd_clear();
    QColor c = QColorDialog::getColor(colorRef(U.colors[i]), this);
    if (!c.isValid())
        return;
    U.colors[i] = toColorRef(c);
    ComboBoxColorCtrlChange();
    PaintBoxPtnPreview->update();
    patternRedraw();
}

// 0x426094. Besides the original defaults there is a second preset with the dark colours seen
// in most screenshots of the program (the original only offers OK/Cancel here).
void MainWindow::SpeedButtonColorsDefaultClick(QObject *)
{
    kbd_clear();
    QMessageBox box(this);
    box.setWindowTitle("Confirm");
    box.setText("Reset all colors to default?");
    QPushButton *ok = box.addButton(QMessageBox::Ok);
    QPushButton *dark = box.addButton("Dark preset", QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == ok) {
        loadColors(false);
    } else if (box.clickedButton() == dark) {
        static const QColor preset[COL_COUNT] = {
            QColor(0, 0, 0),       QColor(0, 0, 0),       QColor(72, 32, 168),   QColor(255, 255, 0),
            QColor(255, 255, 255), QColor(255, 0, 128),   QColor(128, 192, 128), QColor(255, 0, 0),
            QColor(192, 224, 224), QColor(144, 224, 224), QColor(160, 160, 80),  QColor(128, 128, 128),
        };
        for (int i = 0; i < COL_COUNT; i++)
            U.colors[i] = toColorRef(preset[i]);
    } else {
        return;
    }
    PaintBoxPtnPreview->update();
    ComboBoxColorCtrlChange();
    patternRedraw();
}

void MainWindow::ComboBoxShowPrevNextChange(QObject *) { PaintBoxPtnPreview->update(); patternRedraw(); }
void MainWindow::CheckBoxPtnNoteDotsClick(QObject *) { PaintBoxPtnPreview->update(); patternRedraw(); }

void MainWindow::EditRowHighlightChange(QObject *)
{
    PaintBoxPtnPreview->update();
    patternRedraw();
    updateBpm();
}

void MainWindow::EditPageStepChange(QObject *) {}

void MainWindow::CheckBoxVertSlidersClick(QObject *)
{
    Knob::verticalSlider = CheckBoxVertSliders->isChecked();
}

void MainWindow::ButtonRegVGEClick(QObject *) {}
void MainWindow::ButtonUnregVGEClick(QObject *) {}

void MainWindow::CheckBoxOneInstanceClick(QObject *)
{
    QSettings().setValue("OneInstance", CheckBoxOneInstance->isChecked() ? 1 : 0);
}

void MainWindow::CheckBoxDefaultInsClick(QObject *)
{
    g_defaultInstrument = CheckBoxDefaultIns->isChecked();
}

// ---------------------------------------------------------------- help

static QString helpFile()
{
    const QString app = QCoreApplication::applicationDirPath();
    for (const QString &dir : {app, app + "/..", app + "/../..", QDir::currentPath()}) {
        QString f = QDir(dir).filePath("help.chm");
        if (QFile::exists(f))
            return f;
    }
    return QString();
}

// 0x424dfc
void MainWindow::MainMenuHelpClick(QObject *)
{
    HelpShow->setVisible(!helpFile().isEmpty());
}

void MainWindow::HelpShowClick(QObject *)
{
    QString f = helpFile();
    if (!f.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(f));
}

// 0x423a78
void MainWindow::HelpAboutClick(QObject *)
{
    kbd_clear();
    QString text = QString("%1 %2\n\n").arg(g_appVersion, "15.11.11");
    text += "Editor code by Shiru, 2006-2011, QT port by Drozerix, 2026\n\n";
    text += QString::fromLatin1(chips_about_fm()) + "\n\n";
    text += QString::fromLatin1(chips_about_psg());
    text += "\n\n" + QString::fromLatin1(export_about_vgmcmp());
    AboutDialog dlg(this, text);
    dlg.exec();
}
