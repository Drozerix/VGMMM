// The editor's dialogs. As in the original (auto-created forms) each dialog exists once and keeps
// its settings between uses; results are read from its public fields after exec().
#pragma once
#include <QtWidgets>

#include "gen/ui_forms.h"

class DialogBase : public QDialog {
public:
    explicit DialogBase(QWidget *parent);

protected:
    void prepare();     // fixed size, dialog flags; call after setupUi
    void makeGroups();  // exclusive speed-button groups
};

template <class T> T *dialogInstance(QWidget *parent)
{
    static QPointer<T> inst;
    if (!inst)
        inst = new T(parent);
    return inst;
}

class AboutDialog : public DialogBase, public Ui_FormAbout {
public:
    AboutDialog(QWidget *parent, const QString &text);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
};

// TFormCopyExOp
class OpCopyDialog : public DialogBase, public Ui_FormCopyExOp {
public:
    explicit OpCopyDialog(QWidget *parent);
    static OpCopyDialog *instance(QWidget *parent) { return dialogInstance<OpCopyDialog>(parent); }
    bool copy = false, exchange = false;
    int dstOp = 1;      // original field +0x39c
    int srcOp = 0;      // original field +0x3a0
    void SpeedButton1Click(QObject *s = nullptr);
    void Button1Click(QObject *s = nullptr);
    void Button2Click(QObject *s = nullptr);
    void Button3Click(QObject *s = nullptr);

protected:
    void showEvent(QShowEvent *) override;

private:
    void readButtons();
};

// TFormCopyExIns (instruments or samples)
class InsCopyDialog : public DialogBase, public Ui_FormCopyExIns {
public:
    explicit InsCopyDialog(QWidget *parent);
    static InsCopyDialog *instance(QWidget *parent) { return dialogInstance<InsCopyDialog>(parent); }
    bool copy = false, exchange = false, exchangeNumbers = false;
    bool instruments = true;
    int source = 0, dest = 0;
    void SpeedButtonFindEmptyClick(QObject *s = nullptr);
    void ButtonCopyClick(QObject *s = nullptr);
    void ButtonExchangeClick(QObject *s = nullptr);
    void ButtonCancelClick(QObject *s = nullptr);

protected:
    void showEvent(QShowEvent *) override;

private:
    QString slotName(int idx) const;
    void refresh();
};

class DuplicateDialog : public DialogBase, public Ui_FormDuplicate {
public:
    explicit DuplicateDialog(QWidget *parent);
    static DuplicateDialog *instance(QWidget *parent) { return dialogInstance<DuplicateDialog>(parent); }
    bool ok = false;
    int count = 0;
    void EditCountChange(QObject *s = nullptr);
    void ButtonOKClick(QObject *s = nullptr);
    void ButtonCancelClick(QObject *s = nullptr);
};

class ScaleDialog : public DialogBase, public Ui_FormScale {
public:
    explicit ScaleDialog(QWidget *parent);
    static ScaleDialog *instance(QWidget *parent) { return dialogInstance<ScaleDialog>(parent); }
    bool ok = false;
    int from = 100, to = 100;   // percent
    void ButtonOKClick(QObject *s = nullptr);
    void ButtonCancelClick(QObject *s = nullptr);
    void TrackBarStartChange(QObject *s = nullptr);
    void TrackBarEndChange(QObject *s = nullptr);
    void CheckBoxLockClick(QObject *s = nullptr);
};

class TransposeDialog : public DialogBase, public Ui_FormTranspose {
public:
    enum { WHOLE_TRACK = 0x100, CUR_CHANNEL = 0x200, CUR_INSTRUMENT = 0x400, CHECK = 0x800 };
    explicit TransposeDialog(QWidget *parent);
    static TransposeDialog *instance(QWidget *parent) { return dialogInstance<TransposeDialog>(parent); }
    bool ok = false;
    unsigned flags = 0;
    void ButtonOKClick(QObject *s = nullptr);
    void ButtonCancelClick(QObject *s = nullptr);
    void ButtonOctUpClick(QObject *s = nullptr);
    void ButtonOctDownClick(QObject *s = nullptr);
    void ButtonCheckClick(QObject *s = nullptr);
    void EditAmountChange(QObject *s = nullptr);
    void RadioButtonRangeBlockClick(QObject *s = nullptr);

private:
    void readFlags();
};

class ChnExchangeDialog : public DialogBase, public Ui_FormChnExchange {
public:
    explicit ChnExchangeDialog(QWidget *parent);
    static ChnExchangeDialog *instance(QWidget *parent) { return dialogInstance<ChnExchangeDialog>(parent); }
    bool ok = false;
    int range = 0;      // 0 selected block, 1 current pattern, 2 whole track
    int chanA = 0, chanB = 1;
    void SpeedButtonFM1SClick(QObject *s = nullptr);
    void ButtonOKClick(QObject *s = nullptr);
    void ButtonCancelClick(QObject *s = nullptr);
    void RadioButtonRangeBlockClick(QObject *s = nullptr);
};

class ChangeVolumeDialog : public DialogBase, public Ui_FormChangeVolume {
public:
    explicit ChangeVolumeDialog(QWidget *parent);
    static ChangeVolumeDialog *instance(QWidget *parent) { return dialogInstance<ChangeVolumeDialog>(parent); }
    bool ok = false;
    int offset = 0;
    void EditOffsetChange(QObject *s = nullptr);
    void ButtonOKClick(QObject *s = nullptr);
    void ButtonCancelClick(QObject *s = nullptr);
};

class RenderWavDialog : public DialogBase, public Ui_FormRenderWav {
public:
    explicit RenderWavDialog(QWidget *parent);
    bool run(const QString &filename, bool allChannels);
    void ButtonCancelClick(QObject *s = nullptr);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    bool cancel = false;
    bool running = false;
};
