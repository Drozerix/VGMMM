#include "dialogs.h"

#include "export.h"
#include "song.h"

DialogBase::DialogBase(QWidget *parent) : QDialog(parent) {}

void DialogBase::prepare()
{
    setFixedSize(size());
    setWindowFlags((windowFlags() & ~Qt::WindowContextHelpButtonHint) | Qt::MSWindowsFixedSizeDialogHint);
    setModal(true);
    makeGroups();
}

void DialogBase::makeGroups()
{
    QHash<int, QButtonGroup *> groups;
    for (QToolButton *b : findChildren<QToolButton *>()) {
        int g = b->property("groupIndex").toInt();
        if (g == 0)
            continue;
        if (!groups.contains(g)) {
            groups[g] = new QButtonGroup(this);
            groups[g]->setExclusive(true);
        }
        groups[g]->addButton(b);
    }
}

// ---------------------------------------------------------------- about

AboutDialog::AboutDialog(QWidget *parent, const QString &text) : DialogBase(parent)
{
    setupUi(this);
    setWindowTitle("About");
    ImageLogo->setPixmap(QPixmap(":/img/TFORMABOUT_ImageLogo.png"));
    // white text with a one pixel black shadow over the logo (0x429b3c)
    for (QLabel *l : {LabelShadow, LabelText}) {
        l->setText(text);
        l->setAttribute(Qt::WA_TransparentForMouseEvents);
        l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        l->setWordWrap(true);
    }
    QPalette pal = LabelText->palette();
    pal.setColor(QPalette::WindowText, Qt::white);
    LabelText->setPalette(pal);
    pal.setColor(QPalette::WindowText, Qt::black);
    LabelShadow->setPalette(pal);
    LabelShadow->setGeometry(LabelText->geometry().translated(1, 1));
    LabelShadow->lower();
    ImageLogo->lower();
    LabelText->raise();
    ImageLogo->onMouseDown = [this](QMouseEvent *) { close(); };
    for (QLabel *l : {LabelUrl, LabelEMail, LabelDonate}) {
        QPalette p = l->palette();
        p.setColor(QPalette::WindowText, QColor(0, 0, 0xff));
        l->setPalette(p);
        l->installEventFilter(this);
    }
    prepare();
}

bool AboutDialog::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::MouseButtonRelease) {
        if (o == LabelUrl)
            QDesktopServices::openUrl(QUrl("http://shiru.untergrund.net"));
        else if (o == LabelEMail)
            QDesktopServices::openUrl(QUrl("mailto:shiru@mail.ru?subject=VGM Music Maker"));
        else if (o == LabelDonate)
            QDesktopServices::openUrl(QUrl("http://shiru.untergrund.net/donate.shtml"));
    }
    return QDialog::eventFilter(o, e);
}

// ---------------------------------------------------------------- copy/exchange operators

OpCopyDialog::OpCopyDialog(QWidget *parent) : DialogBase(parent)
{
    setupUi(this);
    connectUi(this);
    prepare();
    SpeedButton1->setChecked(true);
    SpeedButton6->setChecked(true);
    SpeedButton1Click();
}

// 0x42a4a4
void OpCopyDialog::showEvent(QShowEvent *)
{
    QToolButton *src[4] = {SpeedButton1, SpeedButton2, SpeedButton3, SpeedButton4};
    QToolButton *dst[4] = {SpeedButton5, SpeedButton6, SpeedButton7, SpeedButton8};
    if (srcOp >= 0 && srcOp < 4)
        src[srcOp]->setChecked(true);
    if (dstOp >= 0 && dstOp < 4)
        dst[dstOp]->setChecked(true);
    SpeedButton1Click();
}

// 0x42a548: an operator cannot be both source and destination
void OpCopyDialog::SpeedButton1Click(QObject *)
{
    QToolButton *src[4] = {SpeedButton1, SpeedButton2, SpeedButton3, SpeedButton4};
    QToolButton *dst[4] = {SpeedButton5, SpeedButton6, SpeedButton7, SpeedButton8};
    for (int i = 0; i < 4; i++) {
        src[i]->setEnabled(!dst[i]->isChecked());
        dst[i]->setEnabled(!src[i]->isChecked());
    }
}

// 0x42a2a8
void OpCopyDialog::readButtons()
{
    QToolButton *src[4] = {SpeedButton1, SpeedButton2, SpeedButton3, SpeedButton4};
    QToolButton *dst[4] = {SpeedButton5, SpeedButton6, SpeedButton7, SpeedButton8};
    for (int i = 0; i < 4; i++) {
        if (src[i]->isChecked())
            srcOp = i;
        if (dst[i]->isChecked())
            dstOp = i;
    }
}

void OpCopyDialog::Button1Click(QObject *)
{
    readButtons();
    copy = true;
    exchange = false;
    close();
}

void OpCopyDialog::Button2Click(QObject *)
{
    readButtons();
    copy = false;
    exchange = true;
    close();
}

void OpCopyDialog::Button3Click(QObject *)
{
    readButtons();
    copy = false;
    exchange = false;
    close();
}

// ---------------------------------------------------------------- copy/exchange instruments or samples

InsCopyDialog::InsCopyDialog(QWidget *parent) : DialogBase(parent)
{
    setupUi(this);
    connectUi(this);
    for (QSpinBox *sb : {UpDownSource, UpDownDestination}) {
        sb->setRange(0, 254);
        sb->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        if (QLineEdit *le = sb->findChild<QLineEdit *>())
            le->setVisible(false);
    }
    connect(UpDownSource, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        source = v;
        refresh();
    });
    connect(UpDownDestination, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        dest = v;
        refresh();
    });
    prepare();
}

QString InsCopyDialog::slotName(int idx) const
{
    if (idx < 0 || idx >= 255)
        return QString();
    return QString::fromLatin1(instruments ? g_song.ins[idx].name : g_song.sample[idx].name);
}

void InsCopyDialog::refresh()
{
    StaticTextSource->setText(QString("%1").arg(source + 1, 2, 16, QChar('0')).toUpper());
    StaticTextSourceName->setText(slotName(source));
    StaticTextDestination->setText(QString("%1").arg(dest + 1, 2, 16, QChar('0')).toUpper());
    StaticTextDestinationName->setText(slotName(dest));
}

// 0x42aa28
void InsCopyDialog::showEvent(QShowEvent *)
{
    copy = exchange = exchangeNumbers = false;
    setWindowTitle(instruments ? "Copy/Exchange instruments" : "Copy/Exchange samples");
    CheckBoxExchangeNumbers->setText(instruments ? "Exchange with instrument numbers in patterns"
                                                 : "Exchange with sample numbers in patterns");
    QSignalBlocker b1(UpDownSource), b2(UpDownDestination);
    UpDownSource->setValue(source);
    UpDownDestination->setValue(dest);
    refresh();
}

void InsCopyDialog::SpeedButtonFindEmptyClick(QObject *)
{
    int idx = instruments ? ins_find_empty() : sample_find_empty();
    if (idx >= 0)
        UpDownDestination->setValue(idx);
}

void InsCopyDialog::ButtonCopyClick(QObject *)
{
    copy = true;
    exchange = false;
    exchangeNumbers = false;
    close();
}

void InsCopyDialog::ButtonExchangeClick(QObject *)
{
    copy = false;
    exchange = true;
    exchangeNumbers = CheckBoxExchangeNumbers->isChecked();
    close();
}

void InsCopyDialog::ButtonCancelClick(QObject *)
{
    copy = exchange = exchangeNumbers = false;
    close();
}

// ---------------------------------------------------------------- duplicate block

DuplicateDialog::DuplicateDialog(QWidget *parent) : DialogBase(parent)
{
    setupUi(this);
    connectUi(this);
    ButtonOK->setDefault(true);
    count = UpDownCount->value();
    prepare();
}

void DuplicateDialog::EditCountChange(QObject *) { count = UpDownCount->value(); }

void DuplicateDialog::ButtonOKClick(QObject *)
{
    count = UpDownCount->value();
    ok = true;
    close();
}

void DuplicateDialog::ButtonCancelClick(QObject *)
{
    ok = false;
    close();
}

// ---------------------------------------------------------------- volume / effect scale

// 0x42af50: above 100% the slider moves in steps of 10%
static int scalePercent(int pos)
{
    return pos > 100 ? pos * 10 - 900 : pos;
}

ScaleDialog::ScaleDialog(QWidget *parent) : DialogBase(parent)
{
    setupUi(this);
    for (QSlider *s : {TrackBarStart, TrackBarEnd})
        s->setTickPosition(QSlider::NoTicks);
    connectUi(this);
    CheckBoxLock->setChecked(true);
    TrackBarStart->setValue(100);
    TrackBarEnd->setValue(100);
    TrackBarStartChange();
    TrackBarEndChange();
    CheckBoxLockClick();
    prepare();
}

void ScaleDialog::ButtonOKClick(QObject *)
{
    ok = true;
    close();
}

void ScaleDialog::ButtonCancelClick(QObject *)
{
    ok = false;
    close();
}

void ScaleDialog::TrackBarStartChange(QObject *)
{
    from = scalePercent(TrackBarStart->value());
    LabelStartVal->setText(QString("%1%").arg(from));
    LabelStartVal->adjustSize();
    if (CheckBoxLock->isChecked())
        TrackBarEnd->setValue(TrackBarStart->value());
}

void ScaleDialog::TrackBarEndChange(QObject *)
{
    to = scalePercent(TrackBarEnd->value());
    LabelEndVal->setText(QString("%1%").arg(to));
    LabelEndVal->adjustSize();
}

void ScaleDialog::CheckBoxLockClick(QObject *)
{
    bool locked = CheckBoxLock->isChecked();
    TrackBarEnd->setEnabled(!locked);
    LabelEnd->setEnabled(!locked);
    LabelEndVal->setEnabled(!locked);
    if (locked)
        TrackBarEnd->setValue(TrackBarStart->value());
}

// ---------------------------------------------------------------- advanced transpose

TransposeDialog::TransposeDialog(QWidget *parent) : DialogBase(parent)
{
    setupUi(this);
    connectUi(this);
    UpDownAmount->setValue(0);
    ButtonOK->setDefault(true);
    prepare();
}

// 0x429d28
void TransposeDialog::readFlags()
{
    flags = (flags & ~(unsigned)(WHOLE_TRACK | CUR_CHANNEL | CUR_INSTRUMENT));
    if (RadioButtonRangeTrack->isChecked())
        flags |= WHOLE_TRACK;
    if (RadioButtonChnCur->isChecked())
        flags |= CUR_CHANNEL;
    if (RadioButtonInsCur->isChecked())
        flags |= CUR_INSTRUMENT;
}

void TransposeDialog::ButtonCancelClick(QObject *)
{
    flags &= ~(unsigned)CHECK;
    ok = false;
    close();
}

void TransposeDialog::ButtonOKClick(QObject *)
{
    readFlags();
    ok = true;
    flags &= ~(unsigned)CHECK;
    close();
}

// "Check" reports whether all notes would stay in range, then the dialog comes back
void TransposeDialog::ButtonCheckClick(QObject *)
{
    readFlags();
    ok = true;
    flags |= CHECK;
    close();
}

void TransposeDialog::ButtonOctUpClick(QObject *) { UpDownAmount->setValue(UpDownAmount->value() - 12); }
void TransposeDialog::ButtonOctDownClick(QObject *) { UpDownAmount->setValue(UpDownAmount->value() + 12); }
void TransposeDialog::EditAmountChange(QObject *) {}

void TransposeDialog::RadioButtonRangeBlockClick(QObject *)
{
    readFlags();
    bool track = RadioButtonRangeTrack->isChecked();
    RadioButtonChnAll->setEnabled(track);
    RadioButtonChnCur->setEnabled(track);
}

// ---------------------------------------------------------------- exchange channels

ChnExchangeDialog::ChnExchangeDialog(QWidget *parent) : DialogBase(parent)
{
    setupUi(this);
    connectUi(this);
    ButtonOK->setDefault(true);
    prepare();
    SpeedButtonFM1SClick();
    RadioButtonRangeBlockClick();
}

// 0x42bee4
void ChnExchangeDialog::SpeedButtonFM1SClick(QObject *)
{
    QToolButton *src[10] = {SpeedButtonFM1S, SpeedButtonFM2S, SpeedButtonFM3S, SpeedButtonFM4S, SpeedButtonFM5S,
                            SpeedButtonFM6S, SpeedButtonPSG1S, SpeedButtonPSG2S, SpeedButtonPSG3S, SpeedButtonPSG4S};
    QToolButton *dst[10] = {SpeedButtonFM1D, SpeedButtonFM2D, SpeedButtonFM3D, SpeedButtonFM4D, SpeedButtonFM5D,
                            SpeedButtonFM6D, SpeedButtonPSG1D, SpeedButtonPSG2D, SpeedButtonPSG3D, SpeedButtonPSG4D};
    for (int i = 0; i < 10; i++) {
        src[i]->setEnabled(!dst[i]->isChecked());
        dst[i]->setEnabled(!src[i]->isChecked());
    }
    for (int i = 0; i < 10; i++) {
        if (src[i]->isChecked())
            chanA = i;
        if (dst[i]->isChecked())
            chanB = i;
    }
}

void ChnExchangeDialog::ButtonOKClick(QObject *)
{
    ok = true;
    close();
}

void ChnExchangeDialog::ButtonCancelClick(QObject *)
{
    ok = false;
    close();
}

void ChnExchangeDialog::RadioButtonRangeBlockClick(QObject *)
{
    if (RadioButtonRangeBlock->isChecked())
        range = 0;
    if (RadioButtonRangePattern->isChecked())
        range = 1;
    if (RadioButtonRangeTrack->isChecked())
        range = 2;
}

// ---------------------------------------------------------------- change instrument volume

ChangeVolumeDialog::ChangeVolumeDialog(QWidget *parent) : DialogBase(parent)
{
    setupUi(this);
    connectUi(this);
    ButtonOK->setDefault(true);
    prepare();
}

// operator levels are attenuations, so a louder instrument needs a lower TL
void ChangeVolumeDialog::EditOffsetChange(QObject *) { offset = -UpDownOffset->value(); }

void ChangeVolumeDialog::ButtonOKClick(QObject *)
{
    offset = -UpDownOffset->value();
    ok = true;
    close();
}

void ChangeVolumeDialog::ButtonCancelClick(QObject *)
{
    ok = false;
    close();
}

// ---------------------------------------------------------------- WAV rendering progress

RenderWavDialog::RenderWavDialog(QWidget *parent) : DialogBase(parent)
{
    setupUi(this);
    connectUi(this);
    ProgressBar->setRange(0, 100);
    prepare();
}

// The original renders in a worker thread behind this modal form; here the exporter runs in
// place and keeps the dialog alive through its progress callback.
bool RenderWavDialog::run(const QString &filename, bool allChannels)
{
    cancel = false;
    running = true;
    ButtonCancel->setText("Cancel");
    ProgressBar->setValue(0);
    show();
    QApplication::processEvents();
    bool ok = export_wav(
        filename.toLocal8Bit().constData(), allChannels,
        [](int percent, void *user) {
            RenderWavDialog *d = (RenderWavDialog *)user;
            d->ProgressBar->setValue(percent);
            QApplication::processEvents();
            return d->cancel;
        },
        this);
    running = false;
    if (ok && !cancel) {
        ProgressBar->setValue(100);
        ButtonCancel->setText("Done");
        QApplication::beep();
        QApplication::processEvents();
        QThread::msleep(250);
    }
    hide();
    return ok;
}

void RenderWavDialog::ButtonCancelClick(QObject *)
{
    cancel = true;
}

void RenderWavDialog::closeEvent(QCloseEvent *e)
{
    if (running) {
        cancel = true;
        e->ignore();
    }
}
